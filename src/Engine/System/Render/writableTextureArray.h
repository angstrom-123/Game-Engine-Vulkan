#pragma once

#include "Util/allocator.h"
#include "commandSubmitter.h"
#include <deque>

struct WritableTextureArrayCreateInfo {
    uint32_t resolution;
    uint32_t size;
    VkFormat format;
    bool useShadowSampler;
};

class WritableTextureArray {
public:
    WritableTextureArray() = default;
    void Init(VkDevice device, VkQueue graphicsQueue, VkRenderPass renderPass, CommandSubmitter& submitter, VmaAllocator allocator, const WritableTextureArrayCreateInfo& info);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate();
    void TransitionRemainingLayersToReadable(VkCommandBuffer commandBuffer);
    void TransitionFromReadableToAttachment(VkCommandBuffer commandBuffer);

    VkImage GetImage() { return m_Image.image; }
    VkSampler GetSampler() { return m_Sampler; }
    VkImageView GetView() { return m_View; }
    VkImageView GetLayerView(uint32_t layer) { return m_LayerViews[layer]; }
    VkFramebuffer GetLayerFramebuffer(uint32_t layer) { return m_LayerFramebuffers[layer]; }

private:
    AllocatedImage m_Image;
    std::vector<VkFramebuffer> m_LayerFramebuffers; 
    std::vector<VkImageView> m_LayerViews;
    VkImageView m_View;
    VkFormat m_Format;
    VkSampler m_Sampler;
    uint32_t m_LayerCount;
    uint32_t m_Resolution;
    std::deque<uint32_t> m_FreeLayers;
};

