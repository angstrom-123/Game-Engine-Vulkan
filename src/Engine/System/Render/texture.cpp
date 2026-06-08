#include "texture.h"
#include "backendDefs.h"
#include "Util/allocator.h"
#include "Util/enumIndex.h"
#include "Util/macros.h"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>

Texture::Texture(const Device& device, VkFormat format, VkImageUsageFlags usage, glm::uvec2 size, VkImageAspectFlags aspect, uint32_t layers, TextureFlags flags)
{
    m_Flags = flags;
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
        std::fill(&m_Layouts[i][0], &m_Layouts[i][m_Levels], TextureLayout::UNDEFINED);
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
    vmaCreateImage(device.GetAllocator(), &imageCreateInfo, &imageAllocInfo, &m_Image.image, &m_Image.allocation, nullptr);

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
        VK_CHECK(vkCreateImageView(device.GetDevice(), &viewCreateInfo, nullptr, &m_Views[0]));

        if (m_Flags & EnumBase(TextureFlag::LAYER_VIEWS)) {
            viewCreateInfo.subresourceRange.layerCount = 1;
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            for (uint32_t i = 0; i < m_Layers; i++) {
                viewCreateInfo.subresourceRange.baseArrayLayer = i;
                VK_CHECK(vkCreateImageView(device.GetDevice(), &viewCreateInfo, nullptr, &m_Views[i + 1]));
            }
        }
    }
}

Texture::Texture(const Device& device, VkImage image, VkImageView view, VkFormat format, glm::uvec2 size, VkImageAspectFlags aspect)
{
    m_Initialized = true;
    m_Image.image = image;
    m_Views[0] = view;
    m_Format = format;
    m_Aspect = aspect;
    m_Size = size;
    m_Layers = 1;
    m_Levels = 1;

    for (uint32_t i = 0; i < m_Levels; i++) {
        std::fill(&m_Layouts[i][0], &m_Layouts[i][m_Levels], TextureLayout::UNDEFINED);
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
        .oldLayout = ToVulkanLayout(m_Layouts[firstLayer][firstMip]),
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
        std::fill(&m_Layouts[i][firstMip], &m_Layouts[i][firstMip + mipCount], FromVulkanLayout(newLayout));
    }
}

uint32_t Texture::AllocateLayer()
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(m_Layers > 1 && "Not an array texture");

    uint32_t layerIndex = m_FreeLayers.Pop();
    return layerIndex;
}

void Texture::CopyToLayer(const Device& device, const CommandSubmitter& submitter, std::optional<MipGenerator>&& mipGenerator, const ImageResource& image, uint32_t layer)
{
    ASSERT(m_Initialized && "Not initialized");
    ASSERT(layer >= 0 && layer < m_Layers && "Layer out of bounds");

    if (mipGenerator.has_value()) {
        GenerateCustomMips(device, submitter, mipGenerator.value(), image, layer);
    } else {
        GenerateDefaultMips(device, submitter, image, layer);
    }
}

void Texture::Cleanup(const Device& device)
{
    ASSERT(m_Initialized && "Not initialized");
    if (!(m_Flags & EnumBase(TextureFlag::NO_VIEW))) {
        vkDestroyImageView(device.GetDevice(), m_Views[0], nullptr);
        if (m_Flags & EnumBase(TextureFlag::LAYER_VIEWS)) {
            for (uint32_t i = 0; i < m_Layers; i++) {
                vkDestroyImageView(device.GetDevice(), m_Views[i + 1], nullptr);
            }
        }
    }
    vmaDestroyImage(device.GetAllocator(), m_Image.image, m_Image.allocation);
}

void Texture::EnqueueCleanup(const Device& device, std::deque<std::function<void ()>>& deletionQueue) 
{
    ASSERT(m_Initialized && "Not initialized");
    deletionQueue.push_back([=, this] {
        Cleanup(device);
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

void Texture::GenerateCustomMips(const Device& device, const CommandSubmitter& submitter, MipGenerator& mipGenerator, const ImageResource& image, uint32_t layer)
{
    VkDeviceSize imageSizeBytes = image.size.x * image.size.y * image.channels;

    // Staging image
    Texture stagingImage(device, m_Format, 
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
    vmaCreateBuffer(device.GetAllocator(), &bufferInfo, &cpuAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(device.GetAllocator(), stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    // Copy into staging
    vmaMapMemory(device.GetAllocator(), stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, image.pixels, imageSizeBytes);
    vmaUnmapMemory(device.GetAllocator(), stagingBuffer.allocation);

    const uint32_t MAX_NORMAL_MIPS = 16;
    VkImageView levelViews[MAX_NORMAL_MIPS] = {};
    submitter.ImmediateSubmit(device, [&](VkCommandBuffer commandBuffer) {
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
        VK_CHECK(vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &levelViews[0]));

        viewInfo.image = m_Image.image;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        for (uint32_t i = 0; i < m_Levels; i++) {
            viewInfo.subresourceRange.baseMipLevel = i;
            VK_CHECK(vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &levelViews[i + 1]));
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

        mipGenerator.WriteDescriptors(device, imageInfos);

        mipGenerator.Generate(commandBuffer, 0, image.size, 1, m_Size);
        for (uint32_t i = 0; i < m_Levels - 1; i++) {
            glm::uvec2 srcSize(glm::max(m_Size.x >> i, 1u), glm::max(m_Size.y >> i, 1u));
            glm::uvec2 dstSize(glm::max(m_Size.x >> (i + 1), 1u), glm::max(m_Size.y >> (i + 1), 1u));
            mipGenerator.Generate(commandBuffer, i + 1, srcSize, i + 2, dstSize);
            // Wait for current mip to finish rendering
            TransitionExplicit(commandBuffer, 
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    layer, 1, 
                    i, 1, 
                    VK_IMAGE_LAYOUT_GENERAL);
        }

        // Transition all levels to shader readable
        TransitionExplicit(commandBuffer, 
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                layer, 1, 
                0, m_Levels, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    for (uint32_t i = 0; i < m_Levels + 1; i++) {
        vkDestroyImageView(device.GetDevice(), levelViews[i], nullptr);
    }

    vmaDestroyBuffer(device.GetAllocator(), stagingBuffer.buffer, stagingBuffer.allocation);
    stagingImage.Cleanup(device);
}

void Texture::GenerateDefaultMips(const Device& device, const CommandSubmitter& submitter, const ImageResource& image, uint32_t layer)
{
    VkDeviceSize imageSizeBytes = image.size.x * image.size.y * image.channels;

    Texture stagingImage(device, m_Format, 
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
    vmaCreateBuffer(device.GetAllocator(), &bufferInfo, &cpuAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(device.GetAllocator(), stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    // Copy into staging
    vmaMapMemory(device.GetAllocator(), stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, image.pixels, imageSizeBytes);
    vmaUnmapMemory(device.GetAllocator(), stagingBuffer.allocation);

    submitter.ImmediateSubmit(device, [&](VkCommandBuffer commandBuffer) {
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

    vmaDestroyBuffer(device.GetAllocator(), stagingBuffer.buffer, stagingBuffer.allocation);
    stagingImage.Cleanup(device);
}
