#pragma once 

#include "backendTypes.h"
#include "Util/allocator.h"
#include "Util/stackVector.h"
#include <deque>
#include <functional>
#include <optional>
#include <vulkan/vulkan_core.h>

struct SwapchainImage {
    uint32_t index{UINT32_MAX};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore semaphore{VK_NULL_HANDLE};
};

class Device {
public:
    ~Device() = default;
    Device() = default;
    Device(struct GLFWwindow *window, VkPresentModeKHR presentMode, VkBool32 validationEnabled);
    void Cleanup();
    void EnqueueCleanup(std::deque<std::function<void ()>>& deleter);
    void CleanupSwapchain();
    VkExtent2D CreateSwapchain(uint32_t requestedImages);
    VkExtent2D RecreateSwapchain();
    std::optional<SwapchainImage> AcquireSwapchainImage(VkSemaphore acquireSemaphore);
    void SetSwapchainFence(SwapchainImage& swapchainImage, VkFence fence);
    VkCommandBuffer AllocateCommandBuffer();
    AllocatedBuffer AllocateMappedMemory(DeletionQueue& deleter, uint32_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    void WaitForIdle() { vkDeviceWaitIdle(m_Device); }
    void WaitForFence(VkFence *fence) { vkWaitForFences(m_Device, 1, fence, VK_TRUE, UINT64_MAX); }
    void ResetFence(VkFence *fence) { vkResetFences(m_Device, 1, fence); }

    VmaAllocator GetAllocator() const { return m_Allocator; }
    VkDevice GetDevice() const { return m_Device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
    uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
    VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
    VkFormat GetSwapchainFormat() const { return m_Format; }


#define GET_FUNCTION_POINTER(T) GetFunctionPointer<PFN_##T>(#T)
    template<typename T> T GetFunctionPointer(const char *name) 
    { 
        T result = reinterpret_cast<T>(vkGetInstanceProcAddr(m_Instance, name)); 
        ASSERT(result && "Failed to get function pointer");
        return result;
    }

private:
    bool CheckFeatureSupport(VkPhysicalDevice device);
    bool CheckExtensionSupport(VkPhysicalDevice device, const char **extensions, uint32_t count);
    uint32_t ScorePhysicalDevice(VkPhysicalDevice device);
    bool SelectQueue(VkPhysicalDevice device, uint32_t *outQueueFamily);
    bool SelectFormat(VkPhysicalDevice device, VkFormat *outFormat, VkColorSpaceKHR *outColorSpace);
    bool SelectPresentMode(VkPhysicalDevice device, VkPresentModeKHR desiredPresentMode, VkPresentModeKHR *outPresentMode);
    void SelectPhysicalDevice(VkPresentModeKHR desiredPresentMode);
    void CreateDevice();
    void CreateInstance();
    void CreateCommandPool();
    void CreateAllocator();

private:
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfn_GetSurfaceCapabilities{nullptr};
    PFN_vkCreateSwapchainKHR pfn_CreateSwapchain{nullptr};
    PFN_vkDestroySwapchainKHR pfn_DestroySwapchain{nullptr};
    PFN_vkGetSwapchainImagesKHR pfn_GetSwapchainImages{nullptr};
    PFN_vkCreateDebugUtilsMessengerEXT pfn_CreateDebugMessenger{nullptr};
    PFN_vkDestroyDebugUtilsMessengerEXT pfn_DestroyDebugMessenger{nullptr};

    VkBool32 m_ValidationEnabled{VK_FALSE};
    VkInstance m_Instance{VK_NULL_HANDLE};
    VmaAllocator m_Allocator{VK_NULL_HANDLE};
    VkSurfaceKHR m_Surface{VK_NULL_HANDLE};
    VkDevice m_Device{VK_NULL_HANDLE};
    VkPhysicalDevice m_PhysicalDevice{VK_NULL_HANDLE};
    VkCommandPool m_CommandPool{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_DebugMessenger{VK_NULL_HANDLE};

    VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
    StackVector<VkImage, 6> m_SwapchainImages;
    StackVector<VkImageView, 6> m_SwapchainViews;
    StackVector<VkFence, 6> m_SwapchainFences;
    StackVector<VkSemaphore, 6> m_SwapchainSemaphores;

    VkQueue m_GraphicsQueue{VK_NULL_HANDLE};
    uint32_t m_GraphicsQueueFamily{0};
    VkFormat m_Format{VK_FORMAT_UNDEFINED};
    VkColorSpaceKHR m_ColorSpace{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkPresentModeKHR m_PresentMode{VK_PRESENT_MODE_IMMEDIATE_KHR};
};
