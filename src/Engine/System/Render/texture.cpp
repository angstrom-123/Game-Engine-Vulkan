#include "texture.h"
#include "Util/allocator.h"
#include "vulkan_core.h"

Texture::Texture(VkDevice device, VmaAllocator allocator, VkFormat format, VkImageUsageFlags usage, glm::uvec2 size, VkImageAspectFlags aspect)
{
    m_Device = device;
    m_Allocated = true;
    m_Format = format;
    m_Aspect = aspect;
    m_Size = size;

    VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_Format,
        .extent = { m_Size.x, m_Size.y, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };
    VmaAllocationCreateInfo imageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_Image.image, &m_Image.allocation, nullptr);

    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_Image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_Format,
        .subresourceRange = {
            .aspectMask = m_Aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };
    vkCreateImageView(device, &viewCreateInfo, nullptr, &m_View);
}

Texture::Texture(VkDevice device, VkImage image, VkImageView view, VkFormat format, glm::uvec2 size, VkImageAspectFlags aspect)
{
    m_Device = device;
    m_Allocated = false;
    m_Image.image = image;
    m_View = view;
    m_Format = format;
    m_Aspect = aspect;
    m_Size = size;
}

void Texture::Transition(VkCommandBuffer commandBuffer, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = m_Layout,
        .newLayout = newLayout,
        .image = m_Image.image,
        .subresourceRange = {
            .aspectMask = m_Aspect,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCmdPipelineBarrier(commandBuffer, 
            srcStage, dstStage, 
            0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_Layout = newLayout;
}

void Texture::EnqueueCleanup(VkDevice device, VmaAllocator allocator, std::deque<std::function<void ()>>& deletionQueue) 
{
    deletionQueue.push_back([=, this] {
        vkDestroyImageView(device, m_View, nullptr);
        vmaDestroyImage(allocator, m_Image.image, m_Image.allocation);
    });
}
