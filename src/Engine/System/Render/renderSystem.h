#pragma once 

#include "Component/material.h"
#include "Component/mesh.h"
#include "ECS/system.h"
#include "ResourceManager/resourceManager.h"
#include "ResourceManager/resourceTypes.h"
#include "System/Render/vulkanBackend.h"
#include "System/lightSystem.h"
#include "System/shadowSystem.h"

class RenderSystem : public System {
public:
    ~RenderSystem();
    void Init(ECS *ecs, const TextureArraySizes& arraySizes, bool allowShadows, VulkanBackend *backend);
    void Update(ECS *ecs, VulkanBackend *backend);
    void RequestResize(VulkanBackend *backend);
    void SetCamera(Entity camera);

    void UploadResources(std::vector<Resource> resources, const fs::path& sceneDir, ResourceManager *manager, VulkanBackend *backend);

    // Resource based game objects
    std::pair<Entity, std::vector<Entity>> GetModel(ECS *ecs, const fs::path& modelRelativePath);
    Entity GetModelPart(ECS *ecs, const fs::path& modelRelativePath, const std::string& partName);

    // Programmatic game objects
    void CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend *backend);
    void CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result, VulkanBackend *backend);

    Signature GetSignature(ECS *ecs) override { return ecs->GetBit<Transform>() | ecs->GetBit<Mesh>() | ecs->GetBit<Material>(); };

private:
    void AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend *backend);

private:
    fs::path m_SceneDir = "";
    std::unordered_map<fs::path, TextureAllocation> m_Textures;     // Image path to texture allocation indices
    std::unordered_map<fs::path, Material> m_AllocatedMaterials;    // Material path to material component
    std::unordered_map<std::string, fs::path> m_SubModelMaterials;  // Submodel / Group name to material path
    std::unordered_map<fs::path, Mesh> m_SubModels;                 // SubModel / Group path to mesh component
    std::unordered_map<fs::path, std::set<fs::path>> m_ModelParts;  // Model path to set of constituent submodel names

    DeletionQueue m_DeletionQueue;

    Entity m_Camera = INVALID_HANDLE;
    TextureArrayHandler m_ArrayHandler;
    ShadowArrayHandler m_ShadowArrayHandler;
    bool m_AllowShadows;

    // Subsystems
    LightSystem *m_LightSystem = nullptr;
    ShadowSystem *m_ShadowSystem = nullptr;
};
