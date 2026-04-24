#include "commandSubmitter.h"

#include "initialiser.h"

void CommandSubmitter::Init(VkDevice device, uint32_t graphicsQueueFamily) 
{
    VkFenceCreateInfo fenceInfo = vkinit::FenceCreateInfo(0);
    VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &m_UploadFence));

    VkCommandPoolCreateInfo createInfo = vkinit::CommandPoolCreateInfo(graphicsQueueFamily);

    VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &m_CommandPool));
    VkCommandBufferAllocateInfo alloc_info = vkinit::CommandBufferAllocateInfo(m_CommandPool);
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &m_CommandBuffer));
}

void CommandSubmitter::Cleanup(VkDevice device)
{
    vkDestroyFence(device, m_UploadFence, nullptr);
    vkDestroyCommandPool(device, m_CommandPool, nullptr); 
}

void CommandSubmitter::ImmediateSubmit(VkDevice device, VkQueue graphicsQueue, std::function<void (VkCommandBuffer commandBuffer)>&& function)
{
    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo));

    function(m_CommandBuffer);

    VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));

    VkSubmitInfo submitInfo = vkinit::SubmitInfo(&m_CommandBuffer);
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, m_UploadFence));

    VK_CHECK(vkWaitForFences(device, 1, &m_UploadFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device, 1, &m_UploadFence));

    VK_CHECK(vkResetCommandPool(device, m_CommandPool, 0));
}
