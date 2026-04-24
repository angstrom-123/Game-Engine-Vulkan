#pragma once

#include <filesystem>
#include <set>

#include <vulkan/vulkan_core.h>

#include "ECS/ecs.h"
#include "System/Render/shadowArrayHandler.h"
#include "config.h"
#include "commandSubmitter.h"
#include "renderTypes.h"
#include "ECS/ecsTypes.h"
#include "Util/allocator.h"

class VulkanBackend {
public:
    ~VulkanBackend();
    void Init(struct GLFWwindow *window, Config &config);
    void Draw(ECS *ecs, Entity camera, ShadowArrayHandler *shadowHandler, std::set<Entity>& entities);
    void RequestResize(Entity camera);
    void TMP(ECS *ecs);
    static void WaitForIdle(VkDevice device);

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
    void Resize(ECS *ecs);
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module);

public:
    struct VmaAllocator_T *allocator = nullptr;
    CommandSubmitter submitter;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    VkDescriptorSetLayout arrayDescriptorLayout = VK_NULL_HANDLE;
    FrameData frames[FRAMES_IN_FLIGHT];
    VkRenderPass depthRenderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;

private:
    // For Drawing
    std::vector<Entity> m_DrawOrder;

    // Global Descriptor
    VkDescriptorSetLayout m_DescriptorLayout = VK_NULL_HANDLE;

    // Flags
    Entity m_ResizeCamera = INVALID_HANDLE;
    VkSampleCountFlagBits m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // Counters
    uint64_t m_FrameNum = 0;

    // Cleanup
    DeletionQueue m_DynamicDeletionQueue;
    DeletionQueue m_MainDeletionQueue;

    // Data
    VkPresentModeKHR m_PresentMode;
    VkExtent2D m_Extent = {100, 100};
    VkViewport m_Viewport = { .width = 100, .height = 100 };
    VkRect2D m_Scissor = { .extent = {100, 100} };
    VkPhysicalDeviceProperties m_Properties = {0};

    // SSAO 
    AllocatedImage m_SSAONoiseImage = {0};
    VkImageView m_SSAONoiseView = VK_NULL_HANDLE;
    VkSampler m_SSAONoiseSampler = VK_NULL_HANDLE;

    // Function Pointers
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_GetSurfaceCapabilities = nullptr;

    // Main Pipelines
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipeline m_TransparencyPipeline = VK_NULL_HANDLE;

    // Depth Pre-Pass
    VkPipelineLayout m_DepthPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_DepthPipeline = VK_NULL_HANDLE;

    // Shadow Map
    VkExtent2D m_ShadowExtent = {SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION};
    VkViewport m_ShadowViewport = {0};
    VkRect2D m_ShadowScissor = {0};
    VkPipelineLayout m_ShadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ShadowPipeline = VK_NULL_HANDLE;
    WritableTextureArray m_DefaultShadowArray;

    // Light Culling Compute
    uint32_t m_TilesX = 0;
    uint32_t m_TilesY = 0;
    VkPipelineLayout m_LightCullingPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_LightCullingPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_LightCullingDescriptorLayout = VK_NULL_HANDLE;
    VkSampler m_DepthSampler = VK_NULL_HANDLE;

    // Resolve Compute
    VkPipelineLayout m_ResolvePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ResolvePipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ResolveDescriptorLayout = VK_NULL_HANDLE;

    // Vulkan Primitives
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_SwapchainFormat = VK_FORMAT_UNDEFINED;
    std::vector<SwapchainImageData> m_Images;
};
