#include "commandSubmitter.h"

#include "System/Render/vulkanBackend.h"

void CommandSubmitter::Init(VkDevice device, uint32_t graphicsQueueFamily) 
{
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_UploadFence));

    VkCommandPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily
    };

    VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &m_CommandPool));
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &m_CommandBuffer));
}

void CommandSubmitter::Cleanup(VkDevice device)
{
    vkDestroyFence(device, m_UploadFence, nullptr);
    vkDestroyCommandPool(device, m_CommandPool, nullptr); 
}

void CommandSubmitter::ImmediateSubmit(VkDevice device, VkQueue graphicsQueue, std::function<void (VkCommandBuffer commandBuffer)>&& function)
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
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, m_UploadFence));

    VK_CHECK(vkWaitForFences(device, 1, &m_UploadFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device, 1, &m_UploadFence));

    VK_CHECK(vkResetCommandPool(device, m_CommandPool, 0));
}
