#include "renderSystem.h"
#include "Component/camera.h"
#include "Component/material.h"
#include "ResourceManager/fontResource.h"
#include "ResourceManager/imageResource.h"
#include "ResourceManager/materialResource.h"
#include "ResourceManager/modelResource.h"
#include "System/Render/textureArrayHandler.h"
#include "Util/allocator.h"
#include "Util/myAssert.h"
#include "Util/profiler.h"
#include "Util/flags.h"
#include "vulkan_core.h"

RenderSystem::~RenderSystem()
{
    m_DeletionQueue.Flush();
}

void RenderSystem::Init(ECS *ecs, const TextureArraySizes& arraySizes, bool allowShadows, VulkanBackend *backend)
{
    m_AllowShadows = allowShadows;

    m_LightSystem = ecs->RegisterSystem<LightSystem>();
    m_ShadowSystem = ecs->RegisterSystem<ShadowSystem>();

    m_ArrayHandler.Init(arraySizes, backend);

    if (m_AllowShadows) {
        m_ShadowArrayHandler.Init(backend);
    }

    VkDevice device = backend->device;
    VmaAllocator allocator = backend->allocator;
    m_DeletionQueue.PushFunction([this, device, allocator] {
        m_ArrayHandler.Cleanup(device, allocator);
        m_ShadowArrayHandler.Cleanup(device, allocator);
    });
}

void RenderSystem::Update(ECS *ecs, VulkanBackend *backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::Update");
    m_LightSystem->Update(ecs);
    m_ShadowSystem->Update(ecs);
    backend->Draw(ecs, m_Camera, m_AllowShadows ? &m_ShadowArrayHandler : nullptr, entities);
}

void RenderSystem::RequestResize(VulkanBackend *backend)
{
    backend->RequestResize(m_Camera);
}

void RenderSystem::SetCamera(Entity camera)
{
    m_Camera = camera;
}

void RenderSystem::UploadResources(std::vector<Resource> resources, const fs::path& sceneDir, ResourceManager *manager, VulkanBackend *backend)
{
    VulkanBackend::WaitForIdle(backend->device);

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

std::pair<Entity, std::vector<Entity>> RenderSystem::GetModel(ECS *ecs, const fs::path& modelRelativePath) const
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    const fs::path fullPath = m_SceneDir / modelRelativePath;
    Entity origin = ecs->CreateEntity();

    auto it = m_ModelParts.find(fullPath);
    ASSERT(it != m_ModelParts.end() && "Getting invalid model");
    ASSERT(!it->second.empty() && "Getting model with no meshes");
    const std::set<fs::path>& partPaths = it->second;

    std::vector<Entity> parts;
    parts.reserve(partPaths.size());

    for (const fs::path& partPath : partPaths) {
        Entity part = GetModelPart(ecs, modelRelativePath, partPath.filename());
        ecs->GetComponent<Transform>(part).InheritFrom(origin);
        parts.push_back(part);
    }

    return std::make_pair(origin, parts);
}

Entity RenderSystem::GetModelPart(ECS *ecs, const fs::path& modelRelativePath, const std::string& partName) const
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    Entity part = ecs->CreateEntity();

    const fs::path partPath = m_SceneDir / modelRelativePath / partName;
    auto meshIt = m_SubModels.find(partPath);
    ASSERT(meshIt != m_SubModels.end() && "Getting mesh for invalid submodel");
    ecs->AddComponent<Mesh>(part, meshIt->second);

    auto subIt = m_SubModelMaterials.find(partPath);
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
        light.direction.w = info.shadowBias;
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
        light.direction.w = info.shadowBias;
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
    ASSERT(m_Fonts.contains(fontPath) && "Specified font not loaded");
    auto it = m_Fonts[fontPath].lower_bound(info.fontSize);
    ASSERT(it != m_Fonts[fontPath].end() && "Requested font size too large");
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

        INFO("Generating mesh for line: " << line);

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
            float x1 = x0 + (g.uv1.x - g.uv0.x) * static_cast<float>(FONT_ATLAS_RESOLUTION) * METRES_PER_PIXEL;
            float y1 = y0 - (g.uv1.y - g.uv0.y) * static_cast<float>(FONT_ATLAS_RESOLUTION) * METRES_PER_PIXEL;

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
        .vertexBuffer = AllocateVertexBuffer(vertices.data(), vertices.size(), backend),
        .bounds = boundingBox,
        .allocated = true,
    };
    ecs->AddComponent<Mesh>(e, mesh);
    INFO("Mesh has: " << mesh.vertexCount << " vertices");

    ASSERT(m_Textures.contains(text.fontPath) && "Font atlas not allocated");
    Material material = {
        .baseColor = text.color,
        .ambientTexture = m_Textures[text.fontPath],
        .diffuseTexture = m_Textures[m_DefaultColor],
        .normalTexture = m_Textures[m_DefaultNormal],
        .flags = MATERIAL_FLAG_TRANSPARENT,
    };
    ecs->AddComponent<Material>(e, material);

    return e;
}

void RenderSystem::AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend *backend)
{
    ShadowAllocation allocation = m_ShadowArrayHandler.AllocateTexture(backend);
    light.shadowIndex = allocation.textureID;
    shadowcaster.shadowIndex = allocation.textureID;
}

void RenderSystem::UploadTexture(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    if (m_Textures.contains(path)) {
        return;
    }

    ImageResource& imageData = manager->GetData<ImageResource>(resource);
    m_Textures.insert({path, m_ArrayHandler.AllocateTexture(imageData, backend)});
}

void RenderSystem::UploadMaterial(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    const MaterialResource& data = manager->GetData<MaterialResource>(resource);
    for (const SubMaterialResource& subMaterial : data.subMaterials) {
        if (m_AllocatedMaterials.contains(path / subMaterial.name)) {
            WARN("Uploading submaterial that is already uploaded");
            continue;
        }

        ASSERT(m_Textures.contains(subMaterial.ambientTexture) && "Material uses unspecified texture");
        ASSERT(m_Textures.contains(subMaterial.diffuseTexture) && "Material uses unspecified texture");
        ASSERT(m_Textures.contains(subMaterial.normalTexture) && "Material uses unspecified texture");
        Material material = {
            .baseColor = subMaterial.diffuseColor,
            .specularExponent = subMaterial.specularExponent,
            .ambientTexture = m_Textures[subMaterial.ambientTexture],
            .diffuseTexture = m_Textures[subMaterial.diffuseTexture],
            .normalTexture = m_Textures[subMaterial.normalTexture],
        };

        const ImageResource& ambientImage = manager->GetData<ImageResource>(manager->GetResource(subMaterial.ambientTexture));
        const ImageResource& diffuseImage = manager->GetData<ImageResource>(manager->GetResource(subMaterial.diffuseTexture));
        if (FLAGS_HAVE_BIT(ambientImage.flags, IMAGE_FLAG_TRANSPARENT) || FLAGS_HAVE_BIT(diffuseImage.flags, IMAGE_FLAG_TRANSPARENT)) {
            material.flags |= MATERIAL_FLAG_TRANSPARENT;
        }
        m_AllocatedMaterials.insert({path / subMaterial.name, material});
    }
}

void RenderSystem::UploadModel(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    if (m_ModelParts.contains(path)) {
        WARN("Uploading model that is already uploaded");
        return;
    }
    const ModelResource& modelData = manager->GetData<ModelResource>(resource);
    const MaterialResource& materialData = manager->GetData<MaterialResource>(manager->GetResource(modelData.materialFilePath));

    std::set<fs::path> subModels;
    for (const SubModelResource& subModel : modelData.subModels) {
        if (m_SubModels.contains(path / subModel.name) || m_SubModelMaterials.contains(path / subModel.name)) {
            WARN("Uploading submodel that is already uploaded");
            continue;
        }

        m_SubModelMaterials.insert({path / subModel.name, modelData.materialFilePath / subModel.materialName});

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
                glm::vec2 deltaUV2 = v3.uv - v1.uv;

                float f = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
                float fInv = 1.0 / f;
                glm::vec4 tangent(
                    fInv * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
                    fInv * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
                    fInv * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z),
                    f < 0.0 ? -1.0 : 1.0
                );

                v1.tangent = tangent;
                v2.tangent = tangent;
                v3.tangent = tangent;
            }
        }

        mesh.vertexBuffer = AllocateVertexBuffer(vertices.data(), vertices.size(), backend);
        mesh.allocated = true;

        m_SubModels.insert({path / subModel.name, mesh});
        subModels.insert({path / subModel.name});
    }

    m_ModelParts.insert({path, subModels});
}

void RenderSystem::UploadFont(Resource resource, ResourceManager *manager, VulkanBackend *backend)
{
    fs::path path = manager->GetPath(resource);
    INFO("path: " << path);
    if (m_Fonts.contains(path)) {
        WARN("Uploading font that is already uploaded");
        return;
    }

    FontResource& fontResource = manager->GetData<FontResource>(resource);
    m_Fonts.insert({path, fontResource.Pack({12, 20, 28, 36, 44, 52, 60, 68, 76})});
    m_Textures.insert({path, m_ArrayHandler.AllocateRaw(fontResource.bitmap, glm::ivec2(FONT_ATLAS_RESOLUTION), IMAGE_FLAG_FONT_ATLAS, 1, backend)});
}

AllocatedBuffer RenderSystem::AllocateVertexBuffer(const Vertex *const vertices, uint32_t count, VulkanBackend *backend)
{
    AllocatedBuffer res = backend->CreateBuffer<Vertex>(vertices, count);
    VmaAllocator allocator = backend->allocator;
    m_DeletionQueue.PushFunction([allocator, res] {
        vmaDestroyBuffer(allocator, res.buffer, res.allocation);
    });
    return res;
}
