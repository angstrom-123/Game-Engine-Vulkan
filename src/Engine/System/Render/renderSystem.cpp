#include "renderSystem.h"
#include "Component/material.h"
#include "System/Render/textureArrayHandler.h"
#include "Util/profiler.h"
#include "vulkan_core.h"

RenderSystem::~RenderSystem()
{
    m_DeletionQueue.Flush();
}

void RenderSystem::Init(ECS *ecs, const TextureArraySizes& arraySizes, bool allowShadows, VulkanBackend& backend)
{
    m_AllowShadows = allowShadows;

    m_LightSystem = ecs->RegisterSystem<LightSystem>();
    m_ShadowSystem = ecs->RegisterSystem<ShadowSystem>();

    ecs->SetSystemSignature<LightSystem>(m_LightSystem->GetSignature(ecs));
    ecs->SetSystemSignature<ShadowSystem>(m_ShadowSystem->GetSignature(ecs));

    m_ArrayHandler.Init(arraySizes, &backend);

    if (m_AllowShadows) {
        m_ShadowArrayHandler.Init(&backend);
    }

    VkDevice device = backend.device;
    VmaAllocator allocator = backend.allocator;
    m_DeletionQueue.PushFunction([this, device, allocator] {
        m_ArrayHandler.Cleanup(device, allocator);
        m_ShadowArrayHandler.Cleanup(device, allocator);
    });
}

void RenderSystem::Update(ECS *ecs, VulkanBackend& backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::Update");
    m_LightSystem->Update(ecs);
    m_ShadowSystem->Update(ecs);
    backend.Draw(ecs, m_Camera, m_AllowShadows ? &m_ShadowArrayHandler : nullptr, entities);
}

void RenderSystem::RequestResize(VulkanBackend& backend)
{
    backend.RequestResize(m_Camera);
}

void RenderSystem::SetCamera(Entity camera)
{
    m_Camera = camera;
}

void RenderSystem::CreateMaterial(const MtlData& data, Material& material, VulkanBackend& backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::CreateMaterial");

    material.baseColor = data.diffuseColor;
    material.specularExponent = data.specularExponent;
    material.flags = MATERIAL_FLAG_NONE;

    MaterialTextureInfo materialInfo = {
        .ambientTextureData = nullptr,
        .diffuseTextureData = nullptr,
        .normalTextureData = nullptr,
    };

    ImageData ambientTexture;
    if (!data.ambientTexture.empty()) {
        if (ambientTexture.LoadImage(data.ambientTexture, false, IMAGE_KIND_COLOR)) {
            materialInfo.ambientTextureData = &ambientTexture;
        }
    }

    // NOTE: Getting the alpha from diffuse texture, so we check transparency here
    ImageData diffuseTexture;
    if (!data.diffuseTexture.empty()) {
        if (diffuseTexture.LoadImage(data.diffuseTexture, true, IMAGE_KIND_COLOR)) {
            materialInfo.diffuseTextureData = &diffuseTexture;
            if (diffuseTexture.hasTransparency) material.flags |= MATERIAL_FLAG_TRANSPARENT;
        }
    }

    ImageData normalTexture;
    if (!data.normalTexture.empty()) {
        if (normalTexture.LoadImage(data.normalTexture, false, IMAGE_KIND_NORMAL)) {
            materialInfo.normalTextureData = &normalTexture;
        }
    }

    AllocateMaterialTextures(materialInfo, material, backend);
}

void RenderSystem::CreateMesh(ECS *ecs, const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results, VulkanBackend& backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::CreateMesh");

    ObjLoader loader;
    std::vector<Shape> shapes;
    std::unordered_map<std::string, MtlData> materialData;
    INFO("Loading Mesh: " << objPath);
    if (!loader.LoadObj(objPath, mtlPath, materialData, shapes)) {
        ERROR("Failed to load mesh: " << objPath);
        return;
    }

    std::unordered_map<std::string, Material> materials(materialData.size());

    uint32_t materialCounter = 1;
    for (const auto& [name, data] : materialData) {
        INFO("Loading Material: " << name << " (" << materialCounter++ << "\\" << materialData.size() << ")");
        Material mat;
        CreateMaterial(data, mat, backend);
        materials.insert({name, mat});
    }

    // TODO: Optimise vertex size
    //       SNORM for the normals and uvs
    results.reserve(shapes.size());
    for (const Shape& s : shapes) {
        Entity e = ecs->CreateEntity();

        Mesh mesh;
        mesh.allocated = false;
        mesh.vertexCount = s.indices.size();
        mesh.vertices = new Vertex[mesh.vertexCount];
        // for (const auto& [index, triple] : s.indices | std::ranges::views::enumerate) {
        for (uint32_t i = 0; i < s.indices.size(); i++)  {
            Vertex vertex = loader.GetVertex(s.indices[i]);
            mesh.vertices[i] = vertex;
            mesh.bounds.Update(vertex);

            if ((i + 1) % 3 == 0) {
                CalculateTangents(mesh.vertices[i - 2], mesh.vertices[i - 1], mesh.vertices[i]);
            }
        }
        AllocateMesh(mesh, backend);
        ecs->AddComponent<Mesh>(e, mesh);

        Material mat = materials[s.materialName];
        ecs->AddComponent<Material>(e, mat);

        results.push_back(e);
    }
}

void RenderSystem::CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result)
{
    Entity e = ecs->CreateEntity();

    ecs->GetComponent<Transform>(e).Translate(info.position);
    ecs->AddComponent(e, Light(POINT, info.color, info.intensity, info.radius));

    result = e;
}

void RenderSystem::CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend& backend)
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

    result = e;
}

void RenderSystem::CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend& backend)
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

    result = e;
}

void RenderSystem::AllocateMesh(Mesh& mesh, VulkanBackend& backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::AllocateMesh");

    const size_t bufferSize = mesh.vertexCount * sizeof(Vertex);

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT // Only for data transfer, not rendering
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(backend.allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, stagingBuffer.allocation, "Mesh_Staging_Buffer");

    vmaMapMemory(backend.allocator, stagingBuffer.allocation, &stagingBuffer.data);
    memcpy(stagingBuffer.data, mesh.vertices, bufferSize);
    vmaUnmapMemory(backend.allocator, stagingBuffer.allocation);

    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT // Data transfer destination
    };

    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // Target buffer is only for the gpu (faster)
    vmaCreateBuffer(backend.allocator, &vertexBufferInfo, &allocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, mesh.vertexBuffer.allocation, "Mesh_Vertex_Buffer");

    // Copy the buffer to the GPU
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = bufferSize,
        };
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
    });

    // Clean up mesh and staging buffer
    delete[] mesh.vertices;
    vmaDestroyBuffer(backend.allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    VmaAllocator allocator = backend.allocator;
    VkBuffer vertexBuffer = mesh.vertexBuffer.buffer; 
    VmaAllocation vertexAllocation = mesh.vertexBuffer.allocation; 
    m_DeletionQueue.PushFunction([allocator, vertexBuffer, vertexAllocation] {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
    });

    mesh.allocated = true;
}

void RenderSystem::AllocateMaterialTextures(const MaterialTextureInfo& info, Material& mat, VulkanBackend& backend)
{
    PROFILER_PROFILE_SCOPE("RenderSystem::AllocateMaterialTextures");

    mat.ambientTexture = (info.ambientTextureData) 
                       ? m_ArrayHandler.AllocateTexture(info.ambientTextureData, &backend)
                       : m_ArrayHandler.GetFallbackTexture(info.ambientTextureData, IMAGE_KIND_COLOR);
    mat.diffuseTexture = (info.diffuseTextureData) 
                       ? m_ArrayHandler.AllocateTexture(info.diffuseTextureData, &backend)
                       : m_ArrayHandler.GetFallbackTexture(info.diffuseTextureData, IMAGE_KIND_COLOR);
    mat.normalTexture = (info.normalTextureData) 
                      ? m_ArrayHandler.AllocateTexture(info.normalTextureData, &backend)
                      : m_ArrayHandler.GetFallbackTexture(info.normalTextureData, IMAGE_KIND_NORMAL);
}

void RenderSystem::AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend& backend)
{
    ShadowAllocation allocation = m_ShadowArrayHandler.AllocateTexture(&backend);
    light.shadowIndex = allocation.textureID;
    shadowcaster.shadowIndex = allocation.textureID;
}

void RenderSystem::CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3)
{
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
