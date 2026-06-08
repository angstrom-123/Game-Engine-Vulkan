#include "commandSubmitter.h"

#include "System/Render/backendDefs.h"
#include "System/Render/device.h"

void CommandSubmitter::Init(const Device& device) 
{
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device.GetDevice(), &fenceInfo, nullptr, &m_UploadFence));

    VkCommandPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.GetGraphicsQueueFamily()
    };

    VK_CHECK(vkCreateCommandPool(device.GetDevice(), &createInfo, nullptr, &m_CommandPool));
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(device.GetDevice(), &alloc_info, &m_CommandBuffer));
}

void CommandSubmitter::Cleanup(const Device& device)
{
    vkDestroyFence(device.GetDevice(), m_UploadFence, nullptr);
    vkDestroyCommandPool(device.GetDevice(), m_CommandPool, nullptr); 
}

void CommandSubmitter::EnqueueCleanup(const Device& device, std::deque<std::function<void ()>>& deleter) 
{
    deleter.push_back([device, this] {
        Cleanup(device);
    });
}

void CommandSubmitter::ImmediateSubmit(const Device& device, std::function<void (VkCommandBuffer commandBuffer)>&& function) const
{
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo));

    function(m_CommandBuffer);

    VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &m_CommandBuffer,
    };
    VK_CHECK(vkQueueSubmit(device.GetGraphicsQueue(), 1, &submitInfo, m_UploadFence));

    VK_CHECK(vkWaitForFences(device.GetDevice(), 1, &m_UploadFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device.GetDevice(), 1, &m_UploadFence));

    VK_CHECK(vkResetCommandPool(device.GetDevice(), m_CommandPool, 0));
}
