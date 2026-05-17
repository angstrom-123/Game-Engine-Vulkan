#include "attachmentArray.h"
#include "vulkanBackendV2.h"
#include "vulkan_core.h"

AttachmentArray::AttachmentArray(uint32_t resolution, VkFormat format)
{
    this->resolution = resolution;
    this->format = format;
}

void AttachmentArray::Init(uint32_t layerCount, VulkanBackendV2& backend)
{
    this->layerCount = layerCount;
    for (uint32_t i = 0; i < layerCount; i++) {
        freeLayers.push(i);
    }

    VkExtent3D extent = {
        .width = resolution,
        .height = resolution,
        .depth = 1
    };

    VkImageUsageFlags usage = IsDepthFormat(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
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
            .aspectMask = IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = layerCount,
        }
    };
    VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &view));

    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    arrayViews.resize(layerCount);
    for (uint32_t i = 0; i < layerCount; i++) {
        viewInfo.subresourceRange.baseArrayLayer = i;
        VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &arrayViews[i]));
    }

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
                .aspectMask = IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = layerCount
            },
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    initialized = true;
}

void AttachmentArray::Cleanup(VkDevice device, VmaAllocator allocator)
{
    vkDestroyImageView(device, view, nullptr);
    for (VkImageView& arrayView : arrayViews) {
        vkDestroyImageView(device, arrayView, nullptr);
    }
    vmaDestroyImage(allocator, image.image, image.allocation);
}

uint32_t AttachmentArray::Allocate()
{
    ASSERT(initialized && "Not initialized");
    ASSERT(freeLayers.size() > 0 && "Allocating too many texture array layers");
    uint32_t layerIndex = freeLayers.front();
    freeLayers.pop();
    return layerIndex;
}

bool AttachmentArray::IsDepthFormat(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
           format == VK_FORMAT_D16_UNORM_S8_UINT ||
           format == VK_FORMAT_D16_UNORM;
}
