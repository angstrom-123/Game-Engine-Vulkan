#pragma once

#include <filesystem>
#include <set>

#include <vulkan/vulkan_core.h>

#include "config.h"
#include "commandSubmitter.h"
#include "renderTypes.h"
#include "textureArrayHandler.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"
#include "Component/shadowcaster.h"
#include "ECS/ecsTypes.h"
#include "Util/allocator.h"

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

class RenderBackend {
public:
    void Init(struct GLFWwindow *window, Config& config);
    void Cleanup();
    void Draw(Entity camera, std::set<Entity>& entities);
    void RequestResize(Entity camera);
    void AllocateMesh(Mesh& mesh);
    void AllocateMaterialTextures(const MaterialTextureInfo& info, Material& material);
    void AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster);

    uint64_t GetFrameNumber() { return m_FrameNum; };

private:
    void InitVulkan(struct GLFWwindow *window);
    void InitMSAA(uint64_t requestedSamples);
    void InitSwapchain();
    void InitCommands();
    void InitFramebuffers();
    void InitSyncStructures();
    void InitGlobalDescriptor();
    void InitMainRenderPass();
    void InitMainPipelines();
    void InitDepthRenderPass();
    void InitDepthPipeline();
    void InitResolveDescriptor();
    void InitResolveResources();
    void InitResolvePipeline();
    void InitLightCullingDescriptor();
    void InitLightCullingResources();
    void InitLightCullingPipeline();
    void InitSSAOResources();
    void InitShadowRenderPass();
    void InitShadowResources();
    void InitShadowPipeline();
    void Resize(Entity camera);
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module);

private:
    // For Drawing
    std::vector<Entity> m_DrawOrder;

    // Global Descriptor
    VkDescriptorSetLayout m_DescriptorLayout;

    // Flags
    Entity m_ResizeCamera;
    VkSampleCountFlagBits m_MSAASamples;

    // Counters
    uint64_t m_FrameNum;

    // Cleanup
    DeletionQueue m_MainDeletionQueue;
    DeletionQueue m_DynamicDeletionQueue;

    // Data
    VkPresentModeKHR m_PresentMode;
    VkExtent2D m_Extent;
    VkViewport m_Viewport;
    VkRect2D m_Scissor;
    VkPhysicalDeviceProperties m_Properties;
    struct VmaAllocator_T *m_Allocator;
    CommandSubmitter m_Submitter;
    TextureArrayHandler m_ArrayHandler;

    // SSAO 
    AllocatedImage m_SSAONoiseImage;
    VkImageView m_SSAONoiseView;
    VkSampler m_SSAONoiseSampler;

    // Function Pointers
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_GetSurfaceCapabilities;

    // Main Pipelines
    VkPipelineLayout m_PipelineLayout;
    VkPipeline m_Pipeline;
    VkPipeline m_TransparencyPipeline;
    VkRenderPass m_RenderPass;

    // Depth Pre-Pass
    VkPipelineLayout m_DepthPipelineLayout;
    VkPipeline m_DepthPipeline;
    VkRenderPass m_DepthRenderPass;

    // Shadow Map
    VkExtent2D m_ShadowExtent;
    VkViewport m_ShadowViewport;
    VkRect2D m_ShadowScissor;
    VkPipelineLayout m_ShadowPipelineLayout;
    VkPipeline m_ShadowPipeline;
    VkRenderPass m_ShadowRenderPass;

    // Light Culling Compute
    uint32_t m_TilesX;
    uint32_t m_TilesY;
    VkPipelineLayout m_LightCullingPipelineLayout;
    VkPipeline m_LightCullingPipeline;
    VkDescriptorSetLayout m_LightCullingDescriptorLayout;;
    VkSampler m_DepthSampler;

    // Resolve Compute
    VkPipelineLayout m_ResolvePipelineLayout;
    VkPipeline m_ResolvePipeline;
    VkDescriptorSetLayout m_ResolveDescriptorLayout;

    // Vulkan Primitives
    VkInstance m_Instance;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;
    VkSurfaceKHR m_Surface;
    VkDebugUtilsMessengerEXT m_DebugMessenger;

    // Swapchain
    VkSwapchainKHR m_Swapchain;
    VkFormat m_SwapchainFormat;
    std::vector<SwapchainImageData> m_Images;

    // Synchronization
    FrameData m_Frames[FRAMES_IN_FLIGHT];
    VkQueue m_GraphicsQueue;
    uint32_t m_GraphicsQueueFamily;
};
