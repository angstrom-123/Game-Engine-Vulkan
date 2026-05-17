#include "renderSystem.h"
#include "Component/camera.h"
#include "Component/material.h"
#include "ResourceManager/fontResource.h"
#include "ResourceManager/imageResource.h"
#include "ResourceManager/materialResource.h"
#include "ResourceManager/modelResource.h"
#include "Util/allocator.h"
#include "Util/myAssert.h"
#include "Util/profiler.h"
#include "Util/flags.h"

RenderSystem::~RenderSystem()
{
    for (auto it = m_Deleter.rbegin(); it != m_Deleter.rend(); it++) {
        (*it)();
    }
    m_Deleter.clear();
}

void RenderSystem::Init(ECS *ecs, const RenderSystemInfo& info, const GraphicsFrontendInfo& frontendInfo, VulkanBackend *backend)
{
    m_LightSystem = ecs->RegisterSystem<LightSystem>();
    m_ShadowSystem = ecs->RegisterSystem<ShadowSystem>();

    backend->InitFrontend(m_Frontend, frontendInfo);
    m_Deleter.push_back([=, this] {
        backend->CleanupFrontend(m_Frontend);
    });
}

void RenderSystem::Update(ECS *ecs, VulkanBackend *backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::Update");
    m_LightSystem->Update(ecs);
    m_ShadowSystem->Update(ecs);
    backend->Draw(ecs, m_Frontend, entities);
}

void RenderSystem::RequestResize(VulkanBackend *backend)
{
    backend->RequestResize(m_Frontend);
}

void RenderSystem::UploadResources(std::vector<Resource> resources, const fs::path& sceneDir, ResourceManager *manager, VulkanBackend *backend)
{
    backend->WaitForIdle();

    ResourceManifest manifest = manager->GetManifest(sceneDir);

    m_SceneDir = fs::path(sceneDir);

    // Upload default textures
    const DefaultTextures& defaultTextures = manager->GetDefaultTextures();
    m_DefaultColor = manager->GetPath(defaultTextures.white);
    m_DefaultNormal = manager->GetPath(defaultTextures.normal);
    UploadTexture(defaultTextures.white, manager, backend);
    UploadTexture(defaultTextures.gray, manager, backend);
    UploadTexture(defaultTextures.black, manager, backend);
    UploadTexture(defaultTextures.normal, manager, backend);

    // Upload default fonts
    const DefaultFonts& defaultFonts = manager->GetDefaultFonts();
    m_DefaultFont = manager->GetPath(defaultFonts.robotoRegular);
    UploadFont(defaultFonts.robotoRegular, manager, backend);

    // Process type by type because some resources depend on others - e.g. materials depend on textures. 
    for (const Resource resource : manager->FilterHasData<ImageResource>(resources)) {
        UploadTexture(resource, manager, backend);
    }
    for (const Resource resource : manager->FilterHasData<FontResource>(resources)) {
        UploadFont(resource, manager, backend);
    }
    for (const Resource resource : manager->FilterHasData<MaterialResource>(resources)) {
        UploadMaterial(resource, manager, backend);
    }
    for (const Resource resource : manager->FilterHasData<ModelResource>(resources)) {
        UploadModel(resource, manager, backend);
    }
}

std::pair<Entity, std::vector<Entity>> RenderSystem::GetModel(ECS *ecs, const fs::path& modelRelativePath)
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    const fs::path fullPath = m_SceneDir / modelRelativePath;
    Entity origin = ecs->CreateEntity();

    auto it = m_ModelParts.find(fullPath.generic_string());
    ASSERT(it != m_ModelParts.end() && "Getting invalid model");
    ASSERT(!it->second.empty() && "Getting model with no meshes");
    const std::set<PathString>& partPaths = it->second;

    std::vector<Entity> parts;
    parts.reserve(partPaths.size());

    for (const PathString& partPathString : partPaths) {
        const fs::path partPath(partPathString);
        Entity part = GetModelPart(ecs, modelRelativePath, partPath.filename().generic_string());
        ecs->GetComponent<Transform>(part).InheritFrom(origin);
        parts.push_back(part);
    }

    return std::make_pair(origin, parts);
}

Entity RenderSystem::GetModelPart(ECS *ecs, const fs::path& modelRelativePath, const std::string& partName)
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    Entity part = ecs->CreateEntity();

    const fs::path partPath = m_SceneDir / modelRelativePath / partName;
    auto meshIt = m_SubModels.find(partPath.generic_string());
    ASSERT(meshIt != m_SubModels.end() && "Getting mesh for invalid submodel");
    ecs->AddComponent<Mesh>(part, meshIt->second);

    auto subIt = m_SubModelMaterials.find(partPath.generic_string());
    ASSERT(subIt != m_SubModelMaterials.end() && "Getting submaterial name for invalid submodel");
    auto materialIt = m_AllocatedMaterials.find(subIt->second);
    ASSERT(materialIt != m_AllocatedMaterials.end() && "Getting submaterial for invalid submodel");
    ecs->AddComponent<Material>(part, materialIt->second);

    return part;
}

Entity RenderSystem::CreatePointLight(ECS *ecs, const LightCreateInfo& info)
{
    Entity e = ecs->CreateEntity();

    ecs->GetComponent<Transform>(e).Translate(info.position);
    ecs->AddComponent(e, Light(POINT, info.color, info.intensity, info.radius));

    return e;
}

Entity RenderSystem::CreateSpotLight(ECS *ecs, const LightCreateInfo& info, VulkanBackend *backend)
{
    Entity e = ecs->CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(SPOT, info.color, info.intensity, info.radius, info.innerConeRadians, info.outerConeRadians);
    if (info.shadowcaster) {
        Shadowcaster shadowcaster = Shadowcaster(CAMERA_PERSPECTIVE, info.outerConeRadians * 2.0, 0.1, info.radius);
        AllocateShadowcaster(light, shadowcaster, backend);
        ecs->AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs->AddComponent<Light>(e, light);
    ecs->GetComponent<Transform>(e).Translate(info.position).Rotate(angle, axis);

    return e;
}

Entity RenderSystem::CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, VulkanBackend *backend)
{
    Entity e = ecs->CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(DIRECTIONAL, info.color, info.intensity);
    if (info.shadowcaster) {
        Shadowcaster shadowcaster = Shadowcaster(CAMERA_ORTHOGRAPHIC, info.projectionLeft, info.projectionRight, info.projectionBottom, info.projectionTop, 0.1, info.radius);
        AllocateShadowcaster(light, shadowcaster, backend);
        ecs->AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs->AddComponent<Light>(e, light);
    ecs->GetComponent<Transform>(e).Rotate(angle, axis).Translate(info.position - glm::normalize(info.direction) * info.distance);

    return e;
}

Entity RenderSystem::CreateText(ECS *ecs, const TextCreateInfo& info, VulkanBackend *backend)
{
    Entity e = ecs->CreateEntity();

    const fs::path fontPath = info.fontPath.empty() ? m_DefaultFont : info.fontPath;
    ASSERT(m_Fonts.contains(fontPath.generic_string()) && "Specified font not loaded");
    auto it = m_Fonts[fontPath.generic_string()].lower_bound(info.fontSize);
    ASSERT(it != m_Fonts[fontPath.generic_string()].end() && "Requested font size too large");
    const auto& [fontSize, glyphs] = *it;
    if (fontSize != info.fontSize) {
        WARN("Desired font size not available (" << info.fontSize << "). Using " << fontSize);
        HINT("Select a multiple of 4 between or including 12 and 68");
    }

    Text text = {
        .fontSize = fontSize,
        .color = info.color,
        .text = info.text,
        .fontPath = fontPath
    };

    ecs->AddComponent<Text>(e, text);
    ecs->GetComponent<Transform>(e).Translate(info.position);

    // Break into lines
    std::vector<std::string> lines(5);
    if (info.maxWidth > 0.0) {
        std::string remaining = text.text;
        while (!remaining.empty()) {
            size_t split = 0;
            float width = 0.0;
            for (uint32_t i = 0; i < remaining.size(); i++) {
                char c = remaining[i];
                const GlyphInfo& g = glyphs.GetGlyph(c);
                if (width + g.advance > info.maxWidth) {
                    size_t lastSpace = remaining.rfind(' ', i);
                    if (lastSpace != std::string::npos && lastSpace > 0) {
                        split = lastSpace;
                    } else {
                        split = i;
                    }
                } else {
                    width += g.advance;
                    if (i == remaining.size() - 1) {
                        split = remaining.size();
                    }
                }
            }

            // Fallback
            if (split == 0) split += remaining.size();

            std::string line = remaining.substr(0, split);
            lines.push_back(line);

            // Skip a space if we split cleanly
            if (split < remaining.size() && remaining[split] == ' ') {
                split++;
            }

            remaining = remaining.substr(split);
        }
    } else {
        lines.push_back(text.text);
    }

    // Create mesh

    // Bounding box padded on z axis because it is flat
    AABB boundingBox = {
        .min = glm::vec3(0.0, 0.0, -0.05),
        .max = glm::vec3(0.0, 0.0, 0.05)
    };
    std::vector<Vertex> vertices(6 * text.text.size());

    float baseline = 0.0;
    for (const std::string& line : lines) {
        if (line.empty()) continue;

        // Alignment
        float lineWidth = 0.0;
        float startX = 0.0;
        for (char c : line) {
            lineWidth += glyphs.GetGlyph(c).advance;
        }
        switch (info.align) {
            case TEXT_ALIGN_CENTRE: startX = (info.maxWidth - lineWidth) * 0.5 * METRES_PER_PIXEL; break;
            case TEXT_ALIGN_RIGHT: startX = (info.maxWidth - lineWidth); break;
            default: break;
        }

        // Create quads
        float penX = startX;
        float penY = baseline;
        for (char c : line) {
            const GlyphInfo& g = glyphs.GetGlyph(c);

            float x0 = penX + g.offset.x * METRES_PER_PIXEL;
            float y0 = penY - g.offset.y * METRES_PER_PIXEL;
            float x1 = x0 + (g.uv1.x - g.uv0.x) * static_cast<float>(FONT_RESOLUTION) * METRES_PER_PIXEL;
            float y1 = y0 - (g.uv1.y - g.uv0.y) * static_cast<float>(FONT_RESOLUTION) * METRES_PER_PIXEL;

            vertices.emplace_back(glm::vec3(x0, y0, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv0.x, g.uv0.y), glm::vec4(1.0, glm::vec3(0.0))); // 0
            vertices.emplace_back(glm::vec3(x0, y1, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv0.x, g.uv1.y), glm::vec4(1.0, glm::vec3(0.0))); // 1
            vertices.emplace_back(glm::vec3(x1, y1, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv1.x, g.uv1.y), glm::vec4(1.0, glm::vec3(0.0))); // 2
            vertices.emplace_back(glm::vec3(x0, y0, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv0.x, g.uv0.y), glm::vec4(1.0, glm::vec3(0.0))); // 0
            vertices.emplace_back(glm::vec3(x1, y1, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv1.x, g.uv1.y), glm::vec4(1.0, glm::vec3(0.0))); // 2
            vertices.emplace_back(glm::vec3(x1, y0, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec2(g.uv1.x, g.uv0.y), glm::vec4(1.0, glm::vec3(0.0))); // 3

            boundingBox.Update(vertices[vertices.size() - 2]);
            boundingBox.Update(vertices[vertices.size() - 6]);

            penX += g.advance * METRES_PER_PIXEL;
        }

        // Move down a line (origin top left)
        baseline -= info.lineHeight * METRES_PER_PIXEL;
    }

    Mesh mesh = {
        .vertexCount = static_cast<int32_t>(vertices.size()),
        .vertexBuffer = backend->AllocateVertexBuffer(vertices.data(), vertices.size()),
        .bounds = boundingBox,
        .allocated = true,
    };
    ecs->AddComponent<Mesh>(e, mesh);

    ASSERT(m_Textures.contains(text.fontPath.generic_string()) && "Font atlas not allocated");
    Material material = {
        .albedo = glm::vec4(text.color, 1.0),
        .diffuseTexture = m_Textures[m_DefaultColor.generic_string()],
        .normalTexture = m_Textures[m_DefaultNormal.generic_string()],
        .flags = MATERIAL_FLAG_TRANSPARENT | MATERIAL_FLAG_DOUBLE_SIDED,
    };
    ecs->AddComponent<Material>(e, material);

    m_Deleter.push_back([=] {
        vmaDestroyBuffer(backend->allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
    });

    return e;
}

void RenderSystem::AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend *backend)
{
    uint32_t allocation = backend->AllocateShadowcaster(m_Frontend);
    light.shadowIndex = allocation;
    shadowcaster.shadowIndex = allocation;
}

void RenderSystem::UploadTexture(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    if (m_Textures.contains(path.generic_string())) {
        return;
    }

    ImageResource& imageData = manager->GetData<ImageResource>(resource);
    m_Textures.insert({path.generic_string(), backend->AllocateTexture(imageData, m_Frontend)});
}

void RenderSystem::UploadMaterial(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    const MaterialResource& data = manager->GetData<MaterialResource>(resource);
    for (const SubMaterialResource& subMaterial : data.subMaterials) {
        if (m_AllocatedMaterials.contains((path / subMaterial.name).generic_string())) {
            WARN("Uploading submaterial that is already uploaded");
            continue;
        }

        ASSERT(m_Textures.contains(subMaterial.diffuseTexture.generic_string()) && "Material uses unspecified texture");
        ASSERT(m_Textures.contains(subMaterial.normalTexture.generic_string()) && "Material uses unspecified texture");
        Material material = {
            .albedo = glm::vec4(subMaterial.diffuseColor, 1.0),
            .roughness = (subMaterial.specularExponent > 0.0) ? std::max(std::sqrt(2.0f / (subMaterial.specularExponent + 2.0f)), 0.04f) : 1.0f,            
            .diffuseTexture = m_Textures[subMaterial.diffuseTexture.generic_string()],
            .normalTexture = m_Textures[subMaterial.normalTexture.generic_string()],
        };

        const ImageResource& diffuseImage = manager->GetData<ImageResource>(manager->GetResource(subMaterial.diffuseTexture));
        if (FLAGS_CONTAIN(diffuseImage.flags, IMAGE_FLAG_TRANSPARENT)) {
            material.flags |= MATERIAL_FLAG_TRANSPARENT;
        }

        if (FLAGS_CONTAIN(diffuseImage.flags, IMAGE_FLAG_CUTOUT)) {
            material.flags |= MATERIAL_FLAG_CUTOUT;
        }

        m_AllocatedMaterials.insert({(path / subMaterial.name).generic_string(), material});
    }
}

void RenderSystem::UploadModel(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    if (m_ModelParts.contains(path.generic_string())) {
        WARN("Uploading model that is already uploaded");
        return;
    }
    const ModelResource& modelData = manager->GetData<ModelResource>(resource);
    const MaterialResource& materialData = manager->GetData<MaterialResource>(manager->GetResource(modelData.materialFilePath));

    std::set<PathString> subModels;
    for (const SubModelResource& subModel : modelData.subModels) {
        if (m_SubModels.contains((path / subModel.name).generic_string()) || m_SubModelMaterials.contains((path / subModel.name).generic_string())) {
            WARN("Uploading submodel that is already uploaded");
            continue;
        }

        m_SubModelMaterials.insert({(path / subModel.name).generic_string(), (modelData.materialFilePath / subModel.materialName).generic_string()});

        Mesh mesh;
        mesh.vertexCount = subModel.indices.size();
        std::vector<Vertex> vertices(mesh.vertexCount);
        for (int32_t i = 0; i < mesh.vertexCount; i++)  {
            // NOTE: the indices stored start from 1
            vertices[i] = (Vertex) {
                .position = modelData.positions[subModel.indices[i][0] - 1],
                .normal = modelData.normals[subModel.indices[i][2] - 1],
                .uv = modelData.uvs[subModel.indices[i][1] - 1]
            };
            mesh.bounds.Update(vertices[i]);

            // Calculate tangents for every tri
            if ((i + 1) % 3 == 0) {
                Vertex& v1 = vertices[i - 2];
                Vertex& v2 = vertices[i - 1];
                Vertex& v3 = vertices[i];
                glm::vec3 edge1 = v2.position - v1.position;
                glm::vec3 edge2 = v3.position - v1.position;
                glm::vec2 deltaUV1 = v2.uv - v1.uv;
                glm::vec2 deltaU = v3.uv - v1.uv;

                float f = (deltaUV1.x * deltaU.y - deltaU.x * deltaUV1.y);
                float fInv = 1.0 / f;
                glm::vec4 tangent(
                    fInv * (deltaU.y * edge1.x - deltaUV1.y * edge2.x),
                    fInv * (deltaU.y * edge1.y - deltaUV1.y * edge2.y),
                    fInv * (deltaU.y * edge1.z - deltaUV1.y * edge2.z),
                    f < 0.0 ? -1.0 : 1.0
                );

                v1.tangent = tangent;
                v2.tangent = tangent;
                v3.tangent = tangent;
            }
        }

        mesh.vertexBuffer = backend->AllocateVertexBuffer(vertices.data(), vertices.size());
        m_Deleter.push_back([=] {
            vmaDestroyBuffer(backend->allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
        });

        mesh.allocated = true;

        m_SubModels.insert({(path / subModel.name).generic_string(), mesh});
        subModels.insert({(path / subModel.name).generic_string()});
    }

    m_ModelParts.insert({path.generic_string(), subModels});
}

void RenderSystem::UploadFont(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    if (m_Fonts.contains(path.generic_string())) {
        WARN("Uploading font that is already uploaded");
        return;
    }

    FontResource& fontResource = manager->GetData<FontResource>(resource);
    m_Fonts.insert({path.generic_string(), fontResource.Pack({12, 20, 28, 36, 44, 52, 60, 68, 76})});
    m_Textures.insert({path.generic_string(), backend->AllocateTexture(fontResource.bitmap, glm::ivec2(FONT_RESOLUTION), IMAGE_FLAG_FONT_ATLAS, 1, m_Frontend)});
}
