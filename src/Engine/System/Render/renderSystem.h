#pragma once 

#include "Component/material.h"
#include "Component/mesh.h"
#include "Component/text.h"
#include "ECS/system.h"
#include "ResourceManager/fontResource.h"
#include "ResourceManager/resourceManager.h"
#include "ResourceManager/resourceTypes.h"
#include "vulkanBackend.h"
#include "System/lightSystem.h"
#include "System/shadowSystem.h"

const float METRES_PER_PIXEL = 0.01;

class RenderSystem : public System {
public:
    ~RenderSystem();
    void Init(ECS *ecs, const TextureArraySizes& arraySizes, bool allowShadows, VulkanBackend *backend);
    void Update(ECS *ecs, VulkanBackend *backend);
    void RequestResize(VulkanBackend *backend);
    void SetCamera(Entity camera);

    void UploadResources(std::vector<Resource> resources, const fs::path& sceneDir, ResourceManager *manager, VulkanBackend *backend);

    // Resource based game objects
    std::pair<Entity, std::vector<Entity>> GetModel(ECS *ecs, const fs::path& modelRelativePath) const;
    Entity GetModelPart(ECS *ecs, const fs::path& modelRelativePath, const std::string& partName) const;

    // Programmatic game objects
    Entity CreatePointLight(ECS *ecs, const LightCreateInfo& info);
    Entity CreateSpotLight(ECS *ecs, const LightCreateInfo& info, VulkanBackend *backend);
    Entity CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, VulkanBackend *backend);
    Entity CreateText(ECS *ecs, const TextCreateInfo& info, VulkanBackend *backend);

    Signature GetSignature(ECS *ecs) override { return ecs->GetBit<Transform>() | ecs->GetBit<Mesh>() | ecs->GetBit<Material>(); };

private:
    void AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster, VulkanBackend *backend);
    void UploadTexture(Resource resource, ResourceManager *manager, VulkanBackend *backend);
    void UploadMaterial(Resource resource, ResourceManager *manager, VulkanBackend *backend);
    void UploadModel(Resource resource, ResourceManager *manager, VulkanBackend *backend);
    void UploadFont(Resource resource, ResourceManager *manager, VulkanBackend *backend);
    AllocatedBuffer AllocateVertexBuffer(const Vertex *const vertices, uint32_t count, VulkanBackend *backend);

private:
    fs::path m_SceneDir = "";
    fs::path m_DefaultFont = "";
    fs::path m_DefaultColor = "";
    fs::path m_DefaultNormal = "";

    std::unordered_map<fs::path, FontMetrics> m_Fonts;              // Font path to glyph metrics
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
