#pragma once
#include <cstdint>
#include <vector>

#include "vulkan_core.h"
#include "config.h"

#define FIF 3

struct FrameData {
    VkFence renderFence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct ImageData {
    VkFence flightFence;
    VkSemaphore renderSemaphore;
};

class VulkanEngine {
public:
    struct GLFWwindow* m_Window;
    bool m_IsInitialized;
    bool m_DidResize;
    bool m_IsFullscreen;
    VkPresentModeKHR m_PresentMode;
    uint32_t m_SelectedShader;
    int64_t m_FrameNum;

    VkInstance m_Instance;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;
    VkSurfaceKHR m_Surface;
    VkDebugUtilsMessengerEXT m_DebugMessenger;

    VkSwapchainKHR m_Swapchain;
    VkFormat m_SwapchainFormat;

    VkRenderPass m_RenderPass;
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainViews;
    std::vector<VkFramebuffer> m_Framebuffers;
    std::vector<ImageData> m_Images;
    // VkExtent2D m_Extent { 1920, 1080 };
    VkExtent2D m_Extent { 960, 540 };

    FrameData m_Frames[FIF];

    VkQueue m_GraphicsQueue;
    uint32_t m_GraphicsQueueFamily;

    std::vector<VkShaderModule> m_ShaderModules;
    VkPipelineLayout m_PipelineLayout;
    std::vector<VkPipeline> m_Pipelines;
    VkViewport m_Viewport;
    VkRect2D m_Scissor;

    void Init(Config *config);
    void Run();
    void Cleanup();
    void Draw();
    void Resize();
    void ToggleFullscreen();

private:
    void InitVulkan();
    void InitSwapchain();
    void InitCommands();
    void InitDefaultRenderPass();
    void InitFramebuffers();
    void InitSyncStructures();
    void InitPipelines();
    void LoadShaderModule(const char *path, VkShaderModule *result);
    void DestroySwapchain();
};
