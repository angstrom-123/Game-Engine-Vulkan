#pragma once 

#include "Component/material.h"
#include "Component/mesh.h"
#include "ECS/system.h"
#include "System/Render/vulkanBackend.h"
#include "System/lightSystem.h"
#include "System/shadowSystem.h"
#include "Util/objLoader.h"
#include <future>

struct ResourceFuture {
    std::future<void> future;

};

class RenderSystem : public System {
public:
    ~RenderSystem();
    void Init(ECS *ecs, const TextureArraySizes& arraySizes, bool allowShadows, VulkanBackend& backend);
    void Update(ECS *ecs, VulkanBackend& backend);
    void RequestResize(VulkanBackend& backend);
    void SetCamera(Entity camera);
    void CreateMaterial(const MtlData& data, Material& material, VulkanBackend& backend);
    void CreateMesh(ECS *ecs, const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results, VulkanBackend& backend);

    void CreateMaterialAsync(const MtlData& data, Material& material, VulkanBackend& backend);

    void CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend& backend);
    void CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend& backend);

    Signature GetSignature(ECS *ecs) { return ecs->GetBit<Transform>() | ecs->GetBit<Mesh>() | ecs->GetBit<Material>(); };

private:
    void AllocateMesh(Mesh& mesh, VulkanBackend& backend);
    void AllocateMaterialTextures(const MaterialTextureInfo& info, Material& material, VulkanBackend& backend);
    void AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend& backend);
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);

private:
    DeletionQueue m_DeletionQueue;

    Entity m_Camera = INVALID_HANDLE;
    TextureArrayHandler m_ArrayHandler;
    ShadowArrayHandler m_ShadowArrayHandler;
    bool m_AllowShadows;

    // Subsystems
    LightSystem *m_LightSystem = nullptr;
    ShadowSystem *m_ShadowSystem = nullptr;
};
