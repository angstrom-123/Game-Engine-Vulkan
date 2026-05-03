#include "renderSystem.h"
#include "Component/material.h"
#include "ResourceManager/imageResource.h"
#include "ResourceManager/materialResource.h"
#include "ResourceManager/modelResource.h"
#include "System/Render/textureArrayHandler.h"
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

    ImageResource& whiteData = manager->GetData<ImageResource>(defaultTextures.white);
    ImageResource& grayData = manager->GetData<ImageResource>(defaultTextures.gray);
    ImageResource& blackData = manager->GetData<ImageResource>(defaultTextures.black);
    ImageResource& normalData = manager->GetData<ImageResource>(defaultTextures.normal);

    m_Textures.insert({manager->GetPath(defaultTextures.white), m_ArrayHandler.AllocateTexture(whiteData, backend)});
    m_Textures.insert({manager->GetPath(defaultTextures.gray), m_ArrayHandler.AllocateTexture(grayData, backend)});
    m_Textures.insert({manager->GetPath(defaultTextures.black), m_ArrayHandler.AllocateTexture(blackData, backend)});
    m_Textures.insert({manager->GetPath(defaultTextures.normal), m_ArrayHandler.AllocateTexture(normalData, backend)});

    // Textures first
    for (const Resource resource : resources) {
        if (manager->HasData<ImageResource>(resource)) {
            fs::path path = manager->GetPath(resource);
            if (!m_Textures.contains(path)) {
                ImageResource& imageData = manager->GetData<ImageResource>(resource);
                m_Textures.insert({path, m_ArrayHandler.AllocateTexture(imageData, backend)});
                imageData.Cleanup();
            }
        } 
    }

    // Materials second
    for (const Resource resource : resources) {
        if (manager->HasData<MaterialResource>(resource)) {
            fs::path path = manager->GetPath(resource);
            const MaterialResource& data = manager->GetData<MaterialResource>(resource);
            for (const SubMaterialResource& subMaterial : data.subMaterials) {
                if (!m_AllocatedMaterials.contains(path / subMaterial.name)) {
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
        }
    }

    // Models third
    for (const Resource resource : resources) {
        if (manager->HasData<ModelResource>(resource)) {
            fs::path path = manager->GetPath(resource);
            if (!m_ModelParts.contains(path)) {
                const ModelResource& modelData = manager->GetData<ModelResource>(resource);
                const MaterialResource& materialData = manager->GetData<MaterialResource>(manager->GetResource(modelData.materialFilePath));

                std::set<fs::path> subModels;
                for (const SubModelResource& subModel : modelData.subModels) {
                    if (!m_SubModelMaterials.contains(path / subModel.name)) {
                        m_SubModelMaterials.insert({path / subModel.name, modelData.materialFilePath / subModel.materialName});
                    }

                    if (!m_SubModels.contains(path / subModel.name)) {
                        Mesh mesh;
                        mesh.vertexCount = subModel.indices.size();
                        mesh.vertices = static_cast<Vertex *>(calloc(mesh.vertexCount, sizeof(Vertex)));
                        for (int32_t i = 0; i < mesh.vertexCount; i++)  {
                            // NOTE: the indices stored start from 1
                            mesh.vertices[i] = (Vertex) {
                                .position = modelData.positions[subModel.indices[i][0] - 1],
                                .normal = modelData.normals[subModel.indices[i][2] - 1],
                                .uv = modelData.uvs[subModel.indices[i][1] - 1]
                            };
                            mesh.bounds.Update(mesh.vertices[i]);

                            // Calculate tangents for every tri
                            if ((i + 1) % 3 == 0) {
                                Vertex& v1 = mesh.vertices[i - 2];
                                Vertex& v2 = mesh.vertices[i - 1];
                                Vertex& v3 = mesh.vertices[i];
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

                        const size_t bufferSize = mesh.vertexCount * sizeof(Vertex);
                        ASSERT(bufferSize > 0 && "Mesh with no vertices");

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
                        vmaCreateBuffer(backend->allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
                        VMA_NAME_ALLOCATION(backend->allocator, stagingBuffer.allocation, "Mesh_Staging_Buffer");

                        vmaMapMemory(backend->allocator, stagingBuffer.allocation, &stagingBuffer.data);
                        memcpy(stagingBuffer.data, mesh.vertices, bufferSize);
                        vmaUnmapMemory(backend->allocator, stagingBuffer.allocation);

                        // Don't need them on the CPU anymore
                        free(mesh.vertices);
                        mesh.vertices = nullptr;

                        VkBufferCreateInfo vertexBufferInfo = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = bufferSize,
                            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT // Data transfer destination
                        };

                        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // Target buffer is only for the gpu (faster)
                        vmaCreateBuffer(backend->allocator, &vertexBufferInfo, &allocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr);
                        VMA_NAME_ALLOCATION(backend->allocator, mesh.vertexBuffer.allocation, "Mesh_Vertex_Buffer");

                        // Copy the buffer to the GPU
                        backend->submitter.ImmediateSubmit(backend->device, backend->graphicsQueue, [&](VkCommandBuffer commandBuffer) {
                            VkBufferCopy copy = {
                                .srcOffset = 0,
                                .dstOffset = 0,
                                .size = bufferSize,
                            };
                            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
                        });

                        // Clean up mesh and staging buffer
                        vmaDestroyBuffer(backend->allocator, stagingBuffer.buffer, stagingBuffer.allocation);

                        VmaAllocator allocator = backend->allocator;
                        VkBuffer vertexBuffer = mesh.vertexBuffer.buffer; 
                        VmaAllocation vertexAllocation = mesh.vertexBuffer.allocation; 
                        m_DeletionQueue.PushFunction([allocator, vertexBuffer, vertexAllocation] {
                            vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
                        });

                        mesh.allocated = true;

                        m_SubModels.insert({path / subModel.name, mesh});
                        subModels.insert({path / subModel.name});
                    }
                }

                m_ModelParts.insert({path, subModels});
            } 
        }
    }
}

std::pair<Entity, std::vector<Entity>> RenderSystem::GetModel(ECS *ecs, const fs::path& modelRelativePath)
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    const fs::path fullPath = m_SceneDir / modelRelativePath;
    Entity origin = ecs->CreateEntity();

    ASSERT(m_ModelParts.contains(fullPath) && "Getting invalid model");
    ASSERT(!m_ModelParts[fullPath].empty() && "Getting model with no meshes");
    const std::set<fs::path> partPaths = m_ModelParts[fullPath];

    std::vector<Entity> parts;
    parts.reserve(partPaths.size());

    for (const fs::path& partPath : partPaths) {
        Entity part = ecs->CreateEntity();
        // Mesh
        ASSERT(m_SubModels.contains(partPath) && "Getting mesh for invalid submodel");
        const Mesh& mesh = m_SubModels[partPath];
        ASSERT(mesh.allocated && "Unallocated mesh");
        ecs->AddComponent<Mesh>(part, mesh);
        // Material
        ASSERT(m_SubModelMaterials.contains(partPath) && "Getting submaterial name for invalid submodel");
        ASSERT(m_AllocatedMaterials.contains(m_SubModelMaterials[partPath]) && "Getting submaterial for invalid submodel");
        const Material& material = m_AllocatedMaterials[m_SubModelMaterials[partPath]];
        ecs->AddComponent<Material>(part, material);
        // Transform
        ecs->GetComponent<Transform>(part).InheritFrom(origin);
        // Insert
        parts.push_back(part);
    }

    return std::make_pair(origin, parts);
}

Entity RenderSystem::GetModelPart(ECS *ecs, const fs::path& modelRelativePath, const std::string& partName)
{
    ASSERT(!m_SceneDir.empty() && "Resources are not loaded");

    Entity part = ecs->CreateEntity();

    const fs::path partPath = m_SceneDir / modelRelativePath / partName;
    ASSERT(m_SubModels.contains(partPath) && "Getting mesh for invalid submodel");
    // Mesh
    const Mesh& mesh = m_SubModels[partPath];
    ASSERT(mesh.allocated && "Unallocated mesh");
    ecs->AddComponent<Mesh>(part, mesh);
    // Material
    ASSERT(m_SubModelMaterials.contains(partPath) && "Getting submaterial name for invalid submodel");
    ASSERT(m_AllocatedMaterials.contains(m_SubModelMaterials[partPath]) && "Getting submaterial for invalid submodel");
    const Material& material = m_AllocatedMaterials[m_SubModelMaterials[partPath]];
    ecs->AddComponent<Material>(part, material);

    return part;
}

void RenderSystem::CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result)
{
    Entity e = ecs->CreateEntity();

    ecs->GetComponent<Transform>(e).Translate(info.position);
    ecs->AddComponent(e, Light(POINT, info.color, info.intensity, info.radius));

    result = e;
}

void RenderSystem::CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend *backend)
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

void RenderSystem::CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend *backend)
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

void RenderSystem::AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend *backend)
{
    ShadowAllocation allocation = m_ShadowArrayHandler.AllocateTexture(backend);
    light.shadowIndex = allocation.textureID;
    shadowcaster.shadowIndex = allocation.textureID;
}
