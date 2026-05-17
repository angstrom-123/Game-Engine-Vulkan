#include "textureArrayV2.h"
#include "vulkanBackendV2.h"
#include "Util/myAssert.h"
#include <cstring>

TextureArrayV2::TextureArrayV2(uint32_t resolution, VkFormat format)
{
    this->resolution = resolution;
    this->format = format;
    levelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(resolution, resolution)))) + 1;
}

void TextureArrayV2::Init(uint32_t layerCount, class VulkanBackendV2& backend)
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

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = levelCount,
        .arrayLayers = layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = levelCount,
            .baseArrayLayer = 0,
            .layerCount = layerCount,
        }
    };
    VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &view));

    // Transition whole array to shader read
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = levelCount,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        };
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image.image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    initialized = true;
}

void TextureArrayV2::Cleanup(VkDevice device, VmaAllocator allocator)
{
    ASSERT(initialized && "Not initialized");
    vkDestroyImageView(device, view, nullptr);
    vmaDestroyImage(allocator, image.image, image.allocation);
}

uint32_t TextureArrayV2::Allocate(ImageResource& imageData, class VulkanBackendV2& backend)
{
    ASSERT(initialized && "Not initialized");
    ASSERT(freeLayers.size() > 0 && "Allocating too many texture array layers");
    uint32_t layerIndex = freeLayers.front();
    freeLayers.pop();

    ASSERT(imageData.pixels != nullptr && "Image is not loaded");
    if (static_cast<uint32_t>(imageData.size.x) < resolution || static_cast<uint32_t>(imageData.size.y) < resolution) {
        bool res = imageData.Resize(glm::ivec2(resolution));
        ASSERT(res && "Failed to resize image");
    }

    VkDeviceSize imageSizeBytes = imageData.size.x * imageData.size.y * imageData.channels;

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSizeBytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(backend.allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    vmaMapMemory(backend.allocator, stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, imageData.pixels, imageSizeBytes);
    vmaUnmapMemory(backend.allocator, stagingBuffer.allocation);

    // Copy buffer to texture array gpu memory
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = layerIndex,
            .layerCount = 1
        };

        // Transition image to optimal layout for a data transfer
        VkImageMemoryBarrier barrierToTransfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image.image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

        // Copy staging buffer to image
        VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            },
            .imageExtent = { resolution, resolution, 1 }
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Check for linear blit support (for mipmap creation)
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(backend.physicalDevice, format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            ERROR("Texture image format does not support linear blitting");
            WARN("TODO: Implement alternative image resizing for mipmap generation");
            abort();
        }

        // Generate mipmaps
        VkImageMemoryBarrier mipmapBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            },
        };

        // Transfer mip level 0 to transfer src
        mipmapBarrier.subresourceRange.baseMipLevel = 0;
        mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);

        // Transfer all mip levels except level 0 to transfer destination
        for (uint32_t i = 1; i < levelCount; i++) {
            mipmapBarrier.subresourceRange.baseMipLevel = i;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
        }

        int32_t mipWidth = resolution;
        int32_t mipHeight = resolution;
        for (uint32_t i = 1; i < levelCount; i++) {
            // Blit next mip level
            VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i - 1,
                    .baseArrayLayer = layerIndex,
                    .layerCount = 1
                },
                .srcOffsets = {
                    { 0, 0, 0 },
                    { mipWidth, mipHeight, 1 }
                },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i,
                    .baseArrayLayer = layerIndex,
                    .layerCount = 1
                },
                .dstOffsets = {
                    { 0, 0, 0 },
                    { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
                },
            };
            vkCmdBlitImage(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // Wait for blit to finish and transfer to shader-readable
            mipmapBarrier.subresourceRange.baseMipLevel = i - 1;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);

            // If not last mip level, transition current level to src, ready to blit to next
            if (i < levelCount - 1) {
                mipmapBarrier.subresourceRange.baseMipLevel = i;
                mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
            }

            // Update dimensions for next level
            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // Transition final mip level to shader-readable
        mipmapBarrier.subresourceRange.baseMipLevel = levelCount - 1;
        mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
    });

    vmaDestroyBuffer(backend.allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    return layerIndex;
}
