#pragma once

#include <functional>
#include <vulkan/vulkan_core.h>

class CommandSubmitter {
public:
    void Init(VkDevice device, uint32_t graphicsQueueFamily);
    void Cleanup(VkDevice device);
    void ImmediateSubmit(VkDevice device, VkQueue graphicsQueue, std::function<void (VkCommandBuffer commandBuffer)>&& function);

private:
    VkFence m_UploadFence;
    VkCommandPool m_CommandPool;
    VkCommandBuffer m_CommandBuffer;
};
