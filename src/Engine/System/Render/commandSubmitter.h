#pragma once

#include <deque>
#include <functional>
#include <vulkan/vulkan_core.h>

class CommandSubmitter {
public:
    ~CommandSubmitter() = default;
    CommandSubmitter() = default;
    void Init(const class Device& device);
    void Cleanup(const class Device& device);
    void EnqueueCleanup(const class Device& device, std::deque<std::function<void ()>>& deleter);
    void ImmediateSubmit(const class Device& device, std::function<void (VkCommandBuffer)>&& function) const;

private:
    VkFence m_UploadFence{VK_NULL_HANDLE};
    VkCommandPool m_CommandPool{VK_NULL_HANDLE};
    VkCommandBuffer m_CommandBuffer{VK_NULL_HANDLE};
};
