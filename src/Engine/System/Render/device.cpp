#include "device.h"
#include "Util/allocator.h"
#include "Util/macros.h"
#include "Util/stackVector.h"
#include <optional>
#include <vulkan/vulkan_core.h>
#include <glm/common.hpp>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
{
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            _LOG_STRIPPED("VALIDATION", "", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            _LOG_STRIPPED("VALIDATION", "\033[93m", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT :
            _LOG_STRIPPED("VALIDATION", "\033[91m", pCallbackData->pMessage);
            break;
        default:
            UNREACHABLE("Bad message severity");
    };
    return VK_FALSE;
}

Device::Device(GLFWwindow *window, VkPresentModeKHR presentMode, VkBool32 validationEnabled)
{
    m_ValidationEnabled = validationEnabled;

    CreateInstance();

    if (glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface) != VK_SUCCESS) {
        FATAL("Failed to create window surface");
    }

    SelectPhysicalDevice(presentMode);
    CreateDevice();
    CreateCommandPool();
    CreateAllocator();
}

void Device::Cleanup()
{
    vmaDestroyAllocator(m_Allocator);
    if (m_Swapchain != VK_NULL_HANDLE) {
        CleanupSwapchain();
    }
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr); 
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_ValidationEnabled) {
        pfn_DestroyDebugMessenger(m_Instance, m_DebugMessenger, nullptr);
    }
    vkDestroyInstance(m_Instance, nullptr);
}

void Device::EnqueueCleanup(std::deque<std::function<void ()>>& deleter) 
{
    deleter.push_back([this] {
        Cleanup();
    });
}

void Device::CleanupSwapchain()
{
    WaitForIdle();

    for (const auto& view : m_SwapchainViews) {
        vkDestroyImageView(m_Device, view, nullptr);
    }
    m_SwapchainViews.Clear();

    for (const auto& semaphore : m_SwapchainSemaphores) {
        vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    m_SwapchainSemaphores.Clear();

    pfn_DestroySwapchain(m_Device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}

VkExtent2D Device::CreateSwapchain(uint32_t requestedImages)
{
    VkSurfaceCapabilitiesKHR capabilties;
    pfn_GetSurfaceCapabilities(m_PhysicalDevice, m_Surface, &capabilties);

    VkExtent2D extent = {
        glm::clamp(capabilties.currentExtent.width, capabilties.minImageExtent.width, capabilties.maxImageExtent.width),
        glm::clamp(capabilties.currentExtent.height, capabilties.minImageExtent.height, capabilties.maxImageExtent.height)
    };

    // 0 means unlimited
    uint32_t maxImages = capabilties.maxImageCount == 0 ? UINT32_MAX : capabilties.maxImageCount;
    uint32_t imageCount = glm::clamp(requestedImages, capabilties.minImageCount, maxImages);

    VkSwapchainCreateInfoKHR swapchainInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_Surface,
        .minImageCount = imageCount,
        .imageFormat = m_Format,
        .imageColorSpace = m_ColorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilties.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = m_PresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    if (pfn_CreateSwapchain(m_Device, &swapchainInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
        FATAL("Failed to create swapchain");
    }
    
    pfn_GetSwapchainImages(m_Device, m_Swapchain, &imageCount, nullptr);
    m_SwapchainImages.Resize(imageCount);
    pfn_GetSwapchainImages(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.Data());

    m_SwapchainViews.Resize(imageCount);
    m_SwapchainFences.Resize(imageCount);
    m_SwapchainSemaphores.Resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_SwapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_Format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainViews[i]) != VK_SUCCESS) {
            FATAL("Failed to create swapchain image view");
        }

        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_SwapchainSemaphores[i]) != VK_SUCCESS) {
            FATAL("Failed to create swapchain semaphore");
        }

        m_SwapchainFences[i] = VK_NULL_HANDLE;
    }
    return extent;
}

VkExtent2D Device::RecreateSwapchain()
{
    vkDeviceWaitIdle(m_Device);

    uint32_t framesInFlight = m_SwapchainImages.Size();
    CleanupSwapchain();
    return CreateSwapchain(framesInFlight);
}

std::optional<SwapchainImage> Device::AcquireSwapchainImage(VkSemaphore acquireSemaphore)
{
    uint32_t index;
    VkResult acquireErr = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &index);
    if (acquireErr == VK_SUCCESS) {
        return std::make_optional<SwapchainImage>(
            index,
            m_Format,
            m_SwapchainImages[index],
            m_SwapchainViews[index],
            m_SwapchainFences[index],
            m_SwapchainSemaphores[index]
        );
    }
    if (acquireErr == VK_ERROR_OUT_OF_DATE_KHR || acquireErr == VK_SUBOPTIMAL_KHR) {
        return {};
    }
    FATAL("Failed to acquire swapchain semaphore");
}

void Device::SetSwapchainFence(SwapchainImage& swapchainImage, VkFence fence)
{
    swapchainImage.fence = fence;
    m_SwapchainFences[swapchainImage.index] = fence;
}

VkCommandBuffer Device::AllocateCommandBuffer()
{
    ASSERT(m_CommandPool != VK_NULL_HANDLE && "Command pool not created");
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_Device, &alloc_info, &commandBuffer);
    return commandBuffer;
}

AllocatedBuffer Device::AllocateMappedMemory(DeletionQueue& deleter, uint32_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = memoryUsage
    };

    AllocatedBuffer buffer;
    vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, nullptr);
    vmaMapMemory(m_Allocator, buffer.allocation, &buffer.data);

    deleter.push_back([buffer, this]{
        vmaUnmapMemory(m_Allocator, buffer.allocation);
        vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
    });

    return buffer;
}

uint32_t Device::ScorePhysicalDevice(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    uint32_t deviceScore;
    switch (properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            deviceScore = 1000;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            deviceScore = 100;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            deviceScore = 10;
            break;
        default:
            deviceScore = 1;
    };
    return deviceScore;
}

bool Device::CheckFeatureSupport(VkPhysicalDevice device)
{
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    if (!deviceFeatures.samplerAnisotropy) return false;
    return true;
}

bool Device::CheckExtensionSupport(VkPhysicalDevice device, const char **extensions, uint32_t count)
{
    uint32_t availableCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &availableCount, nullptr);
    StackVector<VkExtensionProperties, 256> availableExtensions(availableCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &availableCount, availableExtensions.Data());
    for (uint32_t i = 0; i < count; i++) {
        bool found = false;
        for (const auto& extension : availableExtensions) {
            if (std::strcmp(extensions[i], extension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool Device::SelectQueue(VkPhysicalDevice device, uint32_t *outQueueFamily)
{
    // Check support for graphics and presentation
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0) return false;
    StackVector<VkQueueFamilyProperties, 8> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.Data());
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
        if (presentSupport && queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *outQueueFamily = i;
            return true;
        }
    }
    return false;
}

bool Device::SelectFormat(VkPhysicalDevice device, VkFormat *outFormat, VkColorSpaceKHR *outColorSpace)
{
    // Check support for formats and present modes
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
    if (formatCount == 0) return false;
    StackVector<VkSurfaceFormatKHR, 8> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, formats.Data());
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *outFormat = format.format;
            *outColorSpace = format.colorSpace;
            return true;
        }
    }
    return false;
}

bool Device::SelectPresentMode(VkPhysicalDevice device, VkPresentModeKHR desiredPresentMode, VkPresentModeKHR *outPresentMode)
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
    if (presentModeCount == 0) return false;
    StackVector<VkPresentModeKHR, 8> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, presentModes.Data());
    for (const auto& presentMode : presentModes) {
        if (presentMode == desiredPresentMode) {
            *outPresentMode = presentMode;
            return true;
        }
    }
    return false;
}

void Device::SelectPhysicalDevice(VkPresentModeKHR desiredPresentMode)
{
    // Find all possible devices
    uint32_t deviceCount;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    StackVector<VkPhysicalDevice, 4> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.Data());
    
    uint32_t bestScore = 0;
    for (uint32_t i = 0; i < deviceCount; i++) {
        const VkPhysicalDevice device = devices[i];
        uint32_t deviceScore = ScorePhysicalDevice(device) ;
        if (deviceScore < bestScore) continue;

        StackVector<const char *, 2> deviceExtensions({
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
        });
        if (!CheckExtensionSupport(device, deviceExtensions.Data(), deviceExtensions.Size())) continue;

        if (!CheckFeatureSupport(device)) continue;

        uint32_t graphicsQueue;
        if (!SelectQueue(device, &graphicsQueue)) continue;

        VkFormat format;
        VkColorSpaceKHR colorSpace;
        if (!SelectFormat(device, &format, &colorSpace)) continue;

        VkPresentModeKHR presentMode;
        if (!SelectPresentMode(device, desiredPresentMode, &presentMode)) continue;

        m_Format = format;
        m_ColorSpace = colorSpace;
        m_PhysicalDevice = device;
        m_GraphicsQueueFamily = graphicsQueue;
        bestScore = deviceScore;
    }

    if (bestScore == 0) {
        FATAL("Failed to select physical device");
    }
}

void Device::CreateDevice()
{
    float queuePriority = 1.0;
    VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_GraphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    VkPhysicalDeviceFeatures deviceFeatures = {
        .samplerAnisotropy = VK_TRUE
    };

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = VK_TRUE
    };

    StackVector<const char *, 2> deviceExtensions({
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    });

    // TODO: Validation layers?

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamicRenderingFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.Size()),
        .ppEnabledExtensionNames = deviceExtensions.Data(),
        .pEnabledFeatures = &deviceFeatures,
    };

    if (vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_Device) != VK_SUCCESS) {
        FATAL("Failed to create device");
    }

    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
}

void Device::CreateInstance()
{
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan Backend",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4
    };

    uint32_t glfwExtensionCount;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    StackVector<const char *, 32> instanceExtensions(glfwExtensionCount);
    std::copy(glfwExtensions, glfwExtensions + glfwExtensionCount, instanceExtensions.Data());

    VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo = {};
    StackVector<const char *, 1> validationLayers({
        "VK_LAYER_KHRONOS_validation"
    });
    if (m_ValidationEnabled) {
        instanceExtensions.PushBack(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        debugMessengerInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
                // |
                // VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
            .pfnUserCallback = DebugCallback,
        };
    }

    VkInstanceCreateInfo instanceInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.Size()),
        .ppEnabledExtensionNames = instanceExtensions.Data()
    };

    if (m_ValidationEnabled) {
        instanceInfo.pNext = &debugMessengerInfo;
        instanceInfo.enabledLayerCount = validationLayers.Size();
        instanceInfo.ppEnabledLayerNames = validationLayers.Data();
    }

    if (vkCreateInstance(&instanceInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        FATAL("Failed to create instance");
    }

    pfn_GetSurfaceCapabilities = GET_FUNCTION_POINTER(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    pfn_CreateSwapchain = GET_FUNCTION_POINTER(vkCreateSwapchainKHR);
    pfn_DestroySwapchain = GET_FUNCTION_POINTER(vkDestroySwapchainKHR);
    pfn_GetSwapchainImages = GET_FUNCTION_POINTER(vkGetSwapchainImagesKHR);
    pfn_CreateDebugMessenger = GET_FUNCTION_POINTER(vkCreateDebugUtilsMessengerEXT);
    pfn_DestroyDebugMessenger = GET_FUNCTION_POINTER(vkDestroyDebugUtilsMessengerEXT);

    if (m_ValidationEnabled) {
        if (pfn_CreateDebugMessenger(m_Instance, &debugMessengerInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS) {
            FATAL("Failed to create debug messenger");
        }
    }
}

void Device::CreateCommandPool()
{
    VkCommandPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_GraphicsQueueFamily
    };
    if (vkCreateCommandPool(m_Device, &createInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        FATAL("Failed to create command pool");
    }
}

void Device::CreateAllocator()
{
    VmaAllocatorCreateInfo allocInfo = {
        .physicalDevice = m_PhysicalDevice,
        .device = m_Device,
        .instance = m_Instance,
    };
    vmaCreateAllocator(&allocInfo, &m_Allocator);
}
