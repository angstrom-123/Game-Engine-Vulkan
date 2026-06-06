#include "texture.h"
#include "Util/allocator.h"
#include "Util/enumIndex.h"
#include "Util/macros.h"
#include "vulkanBackend.h"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>

Texture::Texture(VkDevice device, VmaAllocator allocator, VkFormat format, VkImageUsageFlags usage, glm::uvec2 size, 
        VkImageAspectFlags aspect, uint32_t layers, TextureFlags flags)
{
    m_Flags = flags;
    m_Device = device;
    m_Initialized = true;
    m_Format = format;
    m_Aspect = aspect;
    m_Size = size;
    m_Layers = layers;
    m_Levels = 1;

    if (m_Flags & EnumBase(TextureFlag::MIP_MAPS)) {
        m_Levels = static_cast<uint32_t>(glm::floor(std::log2(glm::max(m_Size.x, m_Size.y)))) + 1;
    }

    for (uint32_t i = 0; i < m_Layers; i++) {
        m_FreeLayers.Push(i);
        std::fill(&m_Layouts[i][0], &m_Layouts[i][m_Levels], VK_IMAGE_LAYOUT_UNDEFINED);
    }

    VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_Format,
        .extent = { m_Size.x, m_Size.y, 1 },
        .mipLevels = m_Levels,
        .arrayLayers = m_Layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };
    VmaAllocationCreateInfo imageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_Image.image, &m_Image.allocation, nullptr);

    if (!(m_Flags & EnumBase(TextureFlag::NO_VIEW))) {
        VkImageViewCreateInfo viewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_Image.image,
            .viewType = (m_Layers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
            .format = m_Format,
            .subresourceRange = {
                .aspectMask = m_Aspect,
                .baseMipLevel = 0,
                .levelCount = m_Levels,
                .baseArrayLayer = 0,
                .layerCount = m_Layers,
            }
        };
        VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_Views[0]));

        if (m_Flags & EnumBase(TextureFlag::LAYER_VIEWS)) {
            viewCreateInfo.subresourceRange.layerCount = 1;
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            for (uint32_t i = 0; i < m_Layers; i++) {
                viewCreateInfo.subresourceRange.baseArrayLayer = i;
                VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_Views[i + 1]));
            }
        }
    }
}

Texture::Texture(VkDevice device, VkImage image, VkImageView view, VkFormat format,
        glm::uvec2 size, VkImageAspectFlags aspect)
{
    m_Device = device;
    m_Initialized = true;
    m_Image.image = image;
    m_Views[0] = view;
    m_Format = format;
    m_Aspect = aspect;
    m_Size = size;
    m_Layers = 1;
    m_Levels = 1;

    for (uint32_t i = 0; i < m_Levels; i++) {
        std::fill(&m_Layouts[i][0], &m_Layouts[i][m_Levels], VK_IMAGE_LAYOUT_UNDEFINED);
    }
}

void Texture::Transition(VkCommandBuffer commandBuffer,
        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
        VkImageLayout newLayout)
{
    TransitionExplicit(commandBuffer, srcAccess, dstAccess, srcStage, dstStage, 0, m_Layers, 0, m_Levels, newLayout);
}

void Texture::TransitionLayer(VkCommandBuffer commandBuffer, 
        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
        VkImageLayout newLayout, uint32_t layer)
{
    TransitionExplicit(commandBuffer, srcAccess, dstAccess, srcStage, dstStage, layer, 1, 0, m_Levels, newLayout);
}

void Texture::TransitionExplicit(VkCommandBuffer commandBuffer, 
        VkAccessFlags srcAccess, VkAccessFlags dstAccess, 
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, 
        uint32_t firstLayer, uint32_t layerCount, 
        uint32_t firstMip, uint32_t mipCount,
        VkImageLayout newLayout)
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(firstLayer >= 0 && firstLayer + layerCount <= m_Layers && "Layer out of bounds");

    // Assume that all layers being transitioned have the same layout as the first one
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = m_Layouts[firstLayer][firstMip],
        .newLayout = newLayout,
        .image = m_Image.image,
        .subresourceRange = {
            .aspectMask = m_Aspect,
            .baseMipLevel = firstMip,
            .levelCount = mipCount,
            .baseArrayLayer = firstLayer,
            .layerCount = layerCount
        }
    };
    vkCmdPipelineBarrier(commandBuffer, 
            srcStage, dstStage, 
            0, 0, nullptr, 0, nullptr, 1, &barrier);

    for (uint32_t i = firstLayer; i < firstLayer + layerCount; i++) {
        std::fill(&m_Layouts[i][firstMip], &m_Layouts[i][firstMip + mipCount], VK_IMAGE_LAYOUT_UNDEFINED);
    }
}

uint32_t Texture::AllocateLayer()
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(m_Layers > 1 && "Not an array texture");

    uint32_t layerIndex = m_FreeLayers.Pop();
    return layerIndex;
}

void Texture::CopyToLayer(const ImageResource& image, VulkanBackend& backend, uint32_t layer)
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(layer >= 0 && layer < m_Layers && "Layer out of bounds");

    if (m_Flags & EnumBase(TextureFlag::NON_COLOR)) {
        GenerateNormalMips(image, backend, layer);
    } else {
        GenerateColorMips(image, backend, layer);
    }
}

void Texture::Cleanup(VkDevice device, VmaAllocator allocator)
{
    ASSERT(m_Initialized && "Not initialized");
    if (!(m_Flags & EnumBase(TextureFlag::NO_VIEW))) {
        vkDestroyImageView(device, m_Views[0], nullptr);
        if (m_Flags & EnumBase(TextureFlag::LAYER_VIEWS)) {
            for (uint32_t i = 0; i < m_Layers; i++) {
                vkDestroyImageView(device, m_Views[i + 1], nullptr);
            }
        }
    }
    vmaDestroyImage(allocator, m_Image.image, m_Image.allocation);
}

void Texture::EnqueueCleanup(VkDevice device, VmaAllocator allocator, std::deque<std::function<void ()>>& deletionQueue) 
{
    ASSERT(m_Initialized && "Not initialized");
    deletionQueue.push_back([=, this] {
        Cleanup(device, allocator);
    });
}

VkImageView Texture::GetView() const
{
    ASSERT(!(m_Flags & EnumBase(TextureFlag::NO_VIEW)) && "No view for this texture");
    return m_Views[0];
}

VkImageView Texture::GetLayerView(uint32_t layer) const
{
    ASSERT(!(m_Flags & EnumBase(TextureFlag::NO_VIEW)) && "No view for this texture");
    return m_Views[layer + 1];
}

void Texture::GenerateColorMips(const ImageResource& image, VulkanBackend& backend, uint32_t layer)
{
    VkDeviceSize imageSizeBytes = image.size.x * image.size.y * image.channels;

    Texture stagingImage(backend.device, backend.allocator, m_Format, 
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 
            glm::uvec2(image.size), VK_IMAGE_ASPECT_COLOR_BIT, 1, EnumBase(TextureFlag::NO_VIEW));

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSizeBytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo cpuAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(backend.allocator, &bufferInfo, &cpuAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    // Copy into staging
    vmaMapMemory(backend.allocator, stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, image.pixels, imageSizeBytes);
    vmaUnmapMemory(backend.allocator, stagingBuffer.allocation);

    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        // Transition image to optimal layout for a data transfer
        TransitionExplicit(commandBuffer, 
                0, VK_ACCESS_TRANSFER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                layer, 1,
                0, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Transition staging image to optimal too
        stagingImage.Transition(commandBuffer, 
                0, VK_ACCESS_TRANSFER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy buffer to staging image 
        VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
            .imageExtent = { static_cast<uint32_t>(image.size.x), static_cast<uint32_t>(image.size.y), 1 }
        };
        vkCmdCopyBufferToImage(commandBuffer, 
                stagingBuffer.buffer, stagingImage.GetImage(), 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &copy);

        // Transition staging image to transfer source
        stagingImage.Transition(commandBuffer, 
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Blit staging image to first mip level resizing if required
        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { image.size.x, image.size.y, 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = layer,
                .layerCount = 1
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { static_cast<int32_t>(m_Size.x), static_cast<int32_t>(m_Size.y), 1 }
            },
        };
        vkCmdBlitImage(commandBuffer, 
                stagingImage.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &blit, VK_FILTER_LINEAR);

        // Transfer mip level 0 to transfer src
        TransitionExplicit(commandBuffer, 
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                layer, 1,
                0, 1,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // Transfer all mip levels except level 0 to transfer destination
        for (uint32_t i = 1; i < m_Levels; i++) {
            TransitionExplicit(commandBuffer, 
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    layer, 1,
                    i, 1,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // Generate mips
        int32_t mipWidth = m_Size.x;
        int32_t mipHeight = m_Size.y;
        for (uint32_t i = 1; i < m_Levels; i++) {
            // Blit next mip level
            VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i - 1,
                    .baseArrayLayer = layer,
                    .layerCount = 1
                },
                .srcOffsets = {
                    { 0, 0, 0 },
                    { mipWidth, mipHeight, 1 }
                },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i,
                    .baseArrayLayer = layer,
                    .layerCount = 1
                },
                .dstOffsets = {
                    { 0, 0, 0 },
                    { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
                },
            };
            vkCmdBlitImage(commandBuffer, 
                    m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                    m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    1, &blit, VK_FILTER_LINEAR);

            // Wait for blit to finish and transfer to shader-readable
            TransitionExplicit(commandBuffer, 
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    layer, 1, 
                    i - 1, 1, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            // If not last mip level, transition current level to src, ready to blit to next
            if (i < m_Levels - 1) {
                TransitionExplicit(commandBuffer, 
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        layer, 1, 
                        i, 1, 
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            }

            // Update dimensions for next level
            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // Transition final mip level to shader-readable
        TransitionExplicit(commandBuffer, 
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                layer, 1, 
                m_Levels - 1, 1, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    vmaDestroyBuffer(backend.allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    stagingImage.Cleanup(backend.device, backend.allocator);
}

void Texture::GenerateNormalMips(const ImageResource& image, VulkanBackend& backend, uint32_t layer)
{
    VkDeviceSize imageSizeBytes = image.size.x * image.size.y * image.channels;

    // Staging image
    Texture stagingImage(backend.device, backend.allocator, m_Format, 
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(image.size), VK_IMAGE_ASPECT_COLOR_BIT, 1, EnumBase(TextureFlag::NO_VIEW));

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSizeBytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo cpuAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(backend.allocator, &bufferInfo, &cpuAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend.allocator, stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    // Copy into staging
    vmaMapMemory(backend.allocator, stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, image.pixels, imageSizeBytes);
    vmaUnmapMemory(backend.allocator, stagingBuffer.allocation);

    VkImageView levelViews[MAX_NORMAL_MIPS] = {};
    backend.submitter.ImmediateSubmit(backend.device, backend.graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        // Create views for each level (view 0 = staging image view)
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = stagingImage.GetImage(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_Format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &levelViews[0]));

        viewInfo.image = m_Image.image;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        for (uint32_t i = 0; i < m_Levels; i++) {
            viewInfo.subresourceRange.baseMipLevel = i;
            VK_CHECK(vkCreateImageView(backend.device, &viewInfo, nullptr, &levelViews[i + 1]));
        }

        // Transition image to general
        TransitionExplicit(commandBuffer, 
                0, VK_ACCESS_SHADER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                layer, 1,
                0, m_Levels,
                VK_IMAGE_LAYOUT_GENERAL);
        // Transition staging image to transfer destination
        stagingImage.Transition(commandBuffer, 
                0, VK_ACCESS_TRANSFER_WRITE_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy buffer to staging image 
        VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
            .imageExtent = { static_cast<uint32_t>(image.size.x), static_cast<uint32_t>(image.size.y), 1 }
        };
        vkCmdCopyBufferToImage(commandBuffer, 
                stagingBuffer.buffer, stagingImage.GetImage(), 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &copy);

        // Transition staging image to general
        stagingImage.Transition(commandBuffer, 
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_GENERAL);

        // Update descriptors
        VkDescriptorImageInfo imageInfos[MAX_NORMAL_MIPS] = {};
        for (uint32_t i = 0; i < m_Levels + 1; i++) {
            imageInfos[i] = {
                .sampler = VK_NULL_HANDLE,
                .imageView = levelViews[i],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
        }
        // Point all remaining descriptors to the last mip to avoid validation issues
        for (uint32_t i = m_Levels + 1; i < MAX_NORMAL_MIPS; i++) {
            imageInfos[i] = {
                .sampler = VK_NULL_HANDLE,
                .imageView = levelViews[m_Levels],
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL
            };
        }

        VkWriteDescriptorSet writes[2] = {
            { // Source
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = backend.GetNormalMipmapDescriptorSet(),
                .dstBinding = 0,
                .descriptorCount = MAX_NORMAL_MIPS,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = imageInfos
            },
            { // Destination
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = backend.GetNormalMipmapDescriptorSet(),
                .dstBinding = 1,
                .descriptorCount = MAX_NORMAL_MIPS,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = imageInfos
            }
        };
        vkUpdateDescriptorSets(backend.device, 2, writes, 0, nullptr);

        VkDescriptorSet mipmapDescriptor = backend.GetNormalMipmapDescriptorSet();
        backend.GetNormalMipmapPipeline().BeginCompute(commandBuffer, { &mipmapDescriptor, 1 });

        // Scale up staging image to top mip level
        NormalMipmapPushConstants constants = {
            .srcIndex = 0,
            .srcWidth = static_cast<uint32_t>(image.size.x),
            .srcHeight = static_cast<uint32_t>(image.size.y),
            .dstIndex = 1,
            .dstWidth = m_Size.x,
            .dstHeight = m_Size.y
        };
        vkCmdPushConstants(commandBuffer, 
                backend.GetNormalMipmapPipeline().GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                0, sizeof(NormalMipmapPushConstants), 
                &constants);

        const uint32_t TILE_SIZE = 16;
        uint32_t groupsX = (m_Size.x + TILE_SIZE - 1) / TILE_SIZE;
        uint32_t groupsY = (m_Size.y + TILE_SIZE - 1) / TILE_SIZE;
        vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

        for (uint32_t i = 0; i < m_Levels - 1; i++) {
            uint32_t srcLevel = i;
            uint32_t dstLevel = i + 1;

            uint32_t srcWidth = glm::max(m_Size.x >> srcLevel, 1u);
            uint32_t srcHeight = glm::max(m_Size.y >> srcLevel, 1u);
            uint32_t dstWidth = glm::max(m_Size.x >> dstLevel, 1u);
            uint32_t dstHeight = glm::max(m_Size.y >> dstLevel, 1u);

            // NOTE: Index 0 of the bound view is the staging image so the mips start at index 1
            NormalMipmapPushConstants constants = {
                .srcIndex = srcLevel + 1,
                .srcWidth = srcWidth,
                .srcHeight = srcHeight,
                .dstIndex = dstLevel + 1,
                .dstWidth = dstWidth,
                .dstHeight = dstHeight
            };

            vkCmdPushConstants(commandBuffer, 
                    backend.GetNormalMipmapPipeline().GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                    0, sizeof(NormalMipmapPushConstants), 
                    &constants);

            uint32_t groupsX = (dstWidth + TILE_SIZE - 1) / TILE_SIZE;
            uint32_t groupsY = (dstHeight + TILE_SIZE - 1) / TILE_SIZE;
            vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

            // Wait for current mip to finish rendering
            TransitionExplicit(commandBuffer, 
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    layer, 1, 
                    srcLevel, 1, 
                    VK_IMAGE_LAYOUT_GENERAL);
        }

        backend.GetNormalMipmapPipeline().EndCompute();

        // Transition all levels to shader readable
        TransitionExplicit(commandBuffer, 
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                layer, 1, 
                0, m_Levels, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    for (uint32_t i = 0; i < m_Levels + 1; i++) {
        vkDestroyImageView(backend.device, levelViews[i], nullptr);
    }

    vmaDestroyBuffer(backend.allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    stagingImage.Cleanup(backend.device, backend.allocator);
}
