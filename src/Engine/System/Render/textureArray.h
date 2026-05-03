#pragma once

#include "ResourceManager/imageResource.h"
#include "Util/allocator.h"
#include <queue>

class TextureArray {
public:
    TextureArray() = default;
    void Init(uint32_t resolution, uint32_t size, VkFormat format, class VulkanBackend *backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate(ImageResource& imageData, class VulkanBackend *backend);

    VkSampler GetSampler() { return m_Sampler; };
    VkImageView GetView() { return m_View; };

private:
    AllocatedImage m_Image;
    VkImageView m_View;
    VkFormat m_Format;
    VkSampler m_Sampler;
    uint32_t m_LayerCount;
    uint32_t m_Resolution;
    uint32_t m_MipLevels;
    std::queue<uint32_t> m_FreeLayers;
};
