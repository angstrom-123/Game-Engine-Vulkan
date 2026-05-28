#include "textureArray.h"
#include "Util/allocator.h"
#include "vulkanBackend.h"
#include "Util/myAssert.h"
#include "vulkan_core.h"
#include <cstring>

TextureArray::TextureArray(uint32_t resolution, VkFormat format, TextureKind kind)
{
    this->resolution = resolution;
    m_Format = format;
    m_TextureKind = kind;
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
        .format = m_Format,
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
        .format = m_Format,
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

uint32_t TextureArray::Allocate(ImageResource& imageData, class VulkanBackend& backend)
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(m_FreeLayers.size() > 0 && "Allocating too many texture array layers");

    uint32_t layerIndex = m_FreeLayers.front();
    m_FreeLayers.pop();

    ASSERT(imageData.pixels != nullptr && "Image is not loaded");

    VkDeviceSize imageSizeBytes = imageData.size.x * imageData.size.y * imageData.channels;

    // Staging image
    VkExtent3D extent = {
        .width = static_cast<uint32_t>(imageData.size.x),
        .height = static_cast<uint32_t>(imageData.size.y),
        .depth = 1
    };
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_Format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR, // IMPORTANT FOR THE DIRECT CPU COPY
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };
    AllocatedImage stagingImage;
    vmaCreateImage(backend.allocator, &imageInfo, &allocInfo, &stagingImage.image, &stagingImage.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, stagingImage.allocation, "Texture_Array_Staging_Image");

    vmaMapMemory(backend.allocator, stagingImage.allocation, &stagingImage.data);
    std::memcpy(stagingImage.data, imageData.pixels, imageSizeBytes);
    vmaUnmapMemory(backend.allocator, stagingImage.allocation);

    // Copy buffer to texture array gpu memory
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        // Transition image to optimal layout for a data transfer
        VkImageMemoryBarrier barrierToTransfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .baseArrayLayer = layerIndex,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(commandBuffer, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

        barrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrierToTransfer.image = stagingImage.image;
        barrierToTransfer.subresourceRange.baseArrayLayer = 0;
        vkCmdPipelineBarrier(commandBuffer, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

        // Blit staging buffer to image (resizing if required)
        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { imageData.size.x, imageData.size.y, 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = layerIndex,
                .layerCount = 1
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { static_cast<int32_t>(resolution), static_cast<int32_t>(resolution), 1 }
            },
        };
        vkCmdBlitImage(commandBuffer, 
                stagingImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &blit, VK_FILTER_LINEAR);

        // Transfer mip level 0 to transfer src
        VkImageMemoryBarrier mipmapBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            },
        };
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

        // Generate mips
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

    // vmaDestroyBuffer(backend.allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    vmaDestroyImage(backend.allocator, stagingImage.image, stagingImage.allocation);

    return layerIndex;
}

// TODO
void TextureArray::GenerateMipsColor()
{

}

void TextureArray::GenerateMipsNormal()
{

}
