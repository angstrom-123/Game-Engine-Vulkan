#pragma once

#include <functional>
#include <vulkan/vulkan_core.h>

class CommandSubmitter {
public:
    void Init(VkDevice device, uint32_t graphicsQueueFamily);
    void Cleanup(VkDevice device);
    void ImmediateSubmit(VkDevice device, VkQueue graphicsQueue, std::function<void (VkCommandBuffer)>&& function);

private:
    VkFence m_UploadFence{VK_NULL_HANDLE};
    VkCommandPool m_CommandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_CommandBuffer{VK_NULL_HANDLE};
};
