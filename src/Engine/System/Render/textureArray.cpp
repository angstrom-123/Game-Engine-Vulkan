#include "textureArray.h"

#include <cmath>
#include <cstring>

#include "ResourceManager/imageResource.h"
#include "Util/allocator.h"
#include "vulkanBackend.h"
#include "initialiser.h"
#include "Util/logger.h"
#include "Util/myAssert.h"

#include <vulkan/vulkan_core.h>

void TextureArray::Init(uint32_t resolution, uint32_t size, VkFormat format, VulkanBackend *backend)
{
    m_Resolution = resolution;
    m_LayerCount = size;
    m_Format = format;

    m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_Resolution, m_Resolution)))) + 1;

    for (uint32_t i = 0; i < m_LayerCount; i++) {
        m_FreeLayers.push(i);
    }

    VkExtent3D extent = {
        .width = m_Resolution,
        .height = m_Resolution,
        .depth = 1
    };

    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(m_Format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, extent);
    imageInfo.arrayLayers = m_LayerCount;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.mipLevels = m_MipLevels;

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    vmaCreateImage(backend->allocator, &imageInfo, &allocInfo, &m_Image.image, &m_Image.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend->allocator, m_Image.allocation, "Texture_Array_Image");

    VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(m_Format, m_Image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.layerCount = m_LayerCount;
    viewInfo.subresourceRange.levelCount = m_MipLevels;
    VK_CHECK(vkCreateImageView(backend->device, &viewInfo, nullptr, &m_View));

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0,
        .minLod = 0.0,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VK_CHECK(vkCreateSampler(backend->device, &samplerInfo, nullptr, &m_Sampler));

    // Transition whole array to shader read
    backend->submitter.ImmediateSubmit(backend->device, backend->graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = m_MipLevels,
            .baseArrayLayer = 0,
            .layerCount = m_LayerCount
        };
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = m_Image.image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
}

void TextureArray::Cleanup(VkDevice device, VmaAllocator allocator)
{
    vkDestroySampler(device, m_Sampler, nullptr);
    vkDestroyImageView(device, m_View, nullptr);
    vmaDestroyImage(allocator, m_Image.image, m_Image.allocation);
}

uint32_t TextureArray::Allocate(ImageResource& imageData, VulkanBackend *backend)
{
    ASSERT(m_FreeLayers.size() > 0 && "Allocating too many texture array layers");
    uint32_t layerIndex = m_FreeLayers.front();
    m_FreeLayers.pop();

    ASSERT(imageData.pixels != nullptr && "Image is not loaded");
    if (static_cast<uint32_t>(imageData.size.x) < m_Resolution || static_cast<uint32_t>(imageData.size.y) < m_Resolution) {
        bool res = imageData.Resize(glm::ivec2(m_Resolution));
        ASSERT(res && "Failed to resize image");
    }

    ASSERT(imageData.size.x == static_cast<int>(m_Resolution) && "Image width doesn't match array texture");
    ASSERT(imageData.size.y == static_cast<int>(m_Resolution) && "Image height doesn't match array texture");

    VkDeviceSize imageSize = imageData.SizeBytes();

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(backend->allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend->allocator, stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    vmaMapMemory(backend->allocator, stagingBuffer.allocation, &stagingBuffer.data);
    memcpy(stagingBuffer.data, imageData.pixels, imageSize);
    vmaUnmapMemory(backend->allocator, stagingBuffer.allocation);

    // Copy buffer to texture array gpu memory
    backend->submitter.ImmediateSubmit(backend->device, backend->graphicsQueue, [&](VkCommandBuffer commandBuffer) {
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
            .image = m_Image.image,
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
            .imageExtent = { m_Resolution, m_Resolution, 1 }
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Check for linear blit support (for mipmap creation)
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(backend->physicalDevice, m_Format, &properties);
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
            .image = m_Image.image,
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
        for (uint32_t i = 1; i < m_MipLevels; i++) {
            mipmapBarrier.subresourceRange.baseMipLevel = i;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
        }

        int32_t mipWidth = m_Resolution;
        int32_t mipHeight = m_Resolution;
        for (uint32_t i = 1; i < m_MipLevels; i++) {
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
            vkCmdBlitImage(commandBuffer, m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // Wait for blit to finish and transfer to shader-readable
            mipmapBarrier.subresourceRange.baseMipLevel = i - 1;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);

            // If not last mip level, transition current level to src, ready to blit to next
            if (i < m_MipLevels - 1) {
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
        mipmapBarrier.subresourceRange.baseMipLevel = m_MipLevels - 1;
        mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
    });

    vmaDestroyBuffer(backend->allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    return layerIndex;
}
