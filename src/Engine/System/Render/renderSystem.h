#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "Component/material.h"
#include "Component/mesh.h"
#include "ECS/ecs.h"
#include "System/Render/commandSubmitter.h"
#include "System/Render/textureArrayHandler.h"
#include "Util/allocator.h"
#include "config.h"
#include "renderTypes.h"

struct DeletionQueue {
    std::deque<std::function<void ()>> deletors;

    void PushFunction(std::function<void ()>&& function) { deletors.push_back(function); }
    void Clear() { deletors.clear(); }
    void Flush()
    {
        // Reverse iterator
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }
        deletors.clear();
    }
};

class RenderSystem : public System {
public:
    void Init(struct GLFWwindow *window, Config *config);
    void SetCamera(Entity camera);
    void Cleanup();
    void Update(ECS& ecs);
    void RequestResize();
    void AllocateMesh(Mesh& mesh);
    void AllocateMaterialTextures(const MaterialTextureInfo& info, Material& material);

    Signature GetSignature(ECS& ecs) { return ecs.GetBit<Transform>() | ecs.GetBit<Mesh>() | ecs.GetBit<Material>(); };
    size_t GetFrameNumber() { return m_FrameNum; };

private:
    void Resize(ECS& ecs);
    void InitVulkan(struct GLFWwindow *window);
    void InitMSAA(uint64_t requestedSamples);
    void InitSwapchain();
    void InitCommands();
    void InitDefaultRenderPass();
    void InitFramebuffers();
    void InitSyncStructures();
    void InitResolveResources();
    void InitLightCulling();
    void InitResolve();
    void InitLightCullingResources();
    void InitDepthRenderPass();
    void InitDepthPipeline();
    void InitLightCullingPipeline();
    void InitResolvePipeline();
    void InitMainPipelines();
    void InitGlobalDescriptor();
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module);

private:
    // For Drawing
    Entity m_Camera;
    std::vector<Entity> m_DrawOrder;

    // Global Descriptor
    VkDescriptorSetLayout m_DescriptorLayout;

    // Flags
    bool m_IsInitialized;
    bool m_DidResize;
    VkSampleCountFlagBits m_MSAASamples;

    // Counters
    size_t m_FrameNum;

    // Cleanup
    DeletionQueue m_MainDeletionQueue;
    DeletionQueue m_DynamicDeletionQueue;

    // Data
    VkPresentModeKHR m_PresentMode;
    VkExtent2D m_Extent;
    VkViewport m_Viewport;
    VkRect2D m_Scissor;

    // Main Pipelines
    VkPipelineLayout m_PipelineLayout;
    VkPipeline m_Pipeline;
    VkPipeline m_TransparencyPipeline;

    // Depth Pre-Pass Pipeline
    VkPipelineLayout m_DepthPipelineLayout;
    VkPipeline m_DepthPipeline;
    VkRenderPass m_DepthRenderPass;

    // Light Culling Compute Pipeline
    uint32_t m_TilesX;
    uint32_t m_TilesY;
    VkPipelineLayout m_LightCullingPipelineLayout;
    VkPipeline m_LightCullingPipeline;
    VkDescriptorSetLayout m_LightCullingDescriptorLayout;;
    VkSampler m_DepthSampler;

    // Resolve Compute Pipeline
    VkPipelineLayout m_ResolvePipelineLayout;
    VkPipeline m_ResolvePipeline;
    VkDescriptorSetLayout m_ResolveDescriptorLayout;

    struct VmaAllocator_T *m_Allocator;
    CommandSubmitter m_Submitter;
    TextureArrayHandler m_ArrayHandler;

    // Vulkan Primitives
    VkInstance m_Instance;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;
    VkSurfaceKHR m_Surface;
    VkDebugUtilsMessengerEXT m_DebugMessenger;
    VkRenderPass m_RenderPass;

    // Swapchain
    VkSwapchainKHR m_Swapchain;
    VkFormat m_SwapchainFormat;
    VkFormat m_DepthFormat;
    std::vector<SwapchainImageData> m_Images;

    // Synchronization
    FrameData m_Frames[FRAMES_IN_FLIGHT];
    VkQueue m_GraphicsQueue;
    uint32_t m_GraphicsQueueFamily;
};
