#include "textureArray.h"
#include "Util/allocator.h"
#include "vulkanBackend.h"
#include "Util/myAssert.h"
#include "vulkan_core.h"
#include <cstring>

TextureArray::TextureArray(uint32_t resolution, VkFormat format, TextureKind kind)
{
    this->resolution = resolution;
    this->format = format;
    textureKind = kind;
    levelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(resolution, resolution)))) + 1;
}

void TextureArray::Init(uint32_t layerCount, class VulkanBackend& backend)
{
    this->layerCount = layerCount;
    for (uint32_t i = 0; i < layerCount; i++) {
        m_FreeLayers.push(i);
    }

    VkExtent3D extent = {
        .width = resolution,
        .height = resolution,
        .depth = 1
    };

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = levelCount,
        .arrayLayers = layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (textureKind == TextureKind::NORMAL) {
        imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    vmaCreateImage(backend.allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, image.allocation, "Texture_Array_Image");

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = levelCount,
            .layerCount = layerCount,
        }
    };
    VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &view));

    // Transition whole array to shader read
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = levelCount,
                .layerCount = layerCount
            }
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    m_Initialized = true;
}

void TextureArray::Cleanup(VkDevice device, VmaAllocator allocator)
{
    ASSERT(m_Initialized && "Not initialized");
    vkDestroyImageView(device, view, nullptr);
    vmaDestroyImage(allocator, image.image, image.allocation);
}

uint32_t TextureArray::Allocate(const ImageResource& imageData, class VulkanBackend& backend)
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(m_FreeLayers.size() > 0 && "Allocating too many texture array layers");

    uint32_t layerIndex = m_FreeLayers.front();
    m_FreeLayers.pop();
    return layerIndex;
}
