#include "writableTextureArray.h"
#include "renderTypes.h"
#include "initialiser.h"
#include "vulkanBackend.h"
#include "Util/myAssert.h"
#include "vulkan_core.h"

void WritableTextureArray::Init(class VulkanBackend *backend, VkRenderPass renderPass, uint32_t resolution, uint32_t size, VkFormat format, bool useShadowSampler)
{
    m_Resolution = resolution;
    m_LayerCount = size;
    m_Format = format;
    m_LayerViews.reserve(m_LayerCount);
    m_LayerFramebuffers.reserve(m_LayerCount);

    for (uint32_t i = 0; i < m_LayerCount; i++) {
        m_FreeLayers.push_back(i);
    }

    VkExtent3D extent = {
        .width = m_Resolution,
        .height = m_Resolution,
        .depth = 1
    };

    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(m_Format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, extent);
    imageInfo.arrayLayers = m_LayerCount;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };
    vmaCreateImage(backend->allocator, &imageInfo, &allocInfo, &m_Image.image, &m_Image.allocation, nullptr);
    VMA_NAME_ALLOCATION(backend->allocator, m_Image.allocation, "Writable_Texture_Array_Image");

    VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(m_Format, m_Image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkFramebufferCreateInfo framebufferInfo = vkinit::FramebufferCreateInfo(renderPass, { m_Resolution, m_Resolution });
    for (uint32_t i = 0; i < m_LayerCount; i++) {
        viewInfo.subresourceRange.baseArrayLayer = i;
        VK_CHECK(vkCreateImageView(backend->device, &viewInfo, nullptr, &m_LayerViews[i]));

        framebufferInfo.pAttachments = &m_LayerViews[i];
        VK_CHECK(vkCreateFramebuffer(backend->device, &framebufferInfo, nullptr, &m_LayerFramebuffers[i]));
    }

    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.layerCount = m_LayerCount;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    VK_CHECK(vkCreateImageView(backend->device, &viewInfo, nullptr, &m_View));

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .compareEnable = useShadowSampler ? VK_TRUE : VK_FALSE,
        .compareOp = useShadowSampler ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_ALWAYS,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };
    VK_CHECK(vkCreateSampler(backend->device, &samplerInfo, nullptr, &m_Sampler));

    // Transition whole array to shader readable
    backend->submitter.ImmediateSubmit(backend->device, backend->graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrierToShader = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = m_Image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .levelCount = 1,
                .layerCount = MAX_SHADOWCASTERS
            }
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);
    });
}

void WritableTextureArray::Cleanup(VkDevice device, VmaAllocator allocator)
{
    vkDestroySampler(device, m_Sampler, nullptr);
    vkDestroyImageView(device, m_View, nullptr);
    for (uint32_t i = 0; i < m_LayerCount; i++) {
        vkDestroyImageView(device, m_LayerViews[i], nullptr);
        vkDestroyFramebuffer(device, m_LayerFramebuffers[i], nullptr);
    }
    vmaDestroyImage(allocator, m_Image.image, m_Image.allocation);
}

void WritableTextureArray::Reset(VkDevice device, VkRenderPass renderPass)
{
    m_FreeLayers.clear();
    for (uint32_t i = 0; i < m_LayerCount; i++) {
        m_FreeLayers.push_back(i);
        // Free up old resources
        vkDestroyImageView(device, m_LayerViews[i], nullptr);
        vkDestroyFramebuffer(device, m_LayerFramebuffers[i], nullptr);

        // Recreate views and framebuffers
        VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(m_Format, m_Image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        VkFramebufferCreateInfo framebufferInfo = vkinit::FramebufferCreateInfo(renderPass, { m_Resolution, m_Resolution });
        viewInfo.subresourceRange.baseArrayLayer = i;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &m_LayerViews[i]));

        framebufferInfo.pAttachments = &m_LayerViews[i];
        VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_LayerFramebuffers[i]));
    }
}

uint32_t WritableTextureArray::Allocate()
{
    ASSERT(m_FreeLayers.size() > 0 && "Allocating too many texture array layers");
    uint32_t layerIndex = m_FreeLayers.front();
    m_FreeLayers.pop_front();
    return layerIndex;
}

void WritableTextureArray::TransitionRemainingLayersToReadable(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barriers[MAX_SHADOWCASTERS] = {};
    uint32_t barrierCount = 0;
    for (uint32_t layerIndex : m_FreeLayers) {
        barriers[barrierCount++] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = m_Image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .levelCount = 1,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            }
        };
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, barrierCount, barriers);
}

void WritableTextureArray::TransitionFromReadableToAttachment(VkCommandBuffer commandBuffer)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .image = m_Image.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .levelCount = 1,
            .layerCount = MAX_SHADOWCASTERS
        }
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
