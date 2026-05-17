#pragma once

#include "ResourceManager/imageResource.h"
#include "Util/allocator.h"
#include "vulkan_core.h"
#include <queue>

struct TextureArrayV2 {
    TextureArrayV2(uint32_t resolution, VkFormat format);
    void Init(uint32_t layerCount, class VulkanBackendV2& backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate(ImageResource& image, class VulkanBackendV2& backend);

    uint32_t resolution = 0;
    uint32_t layerCount = 0;
    uint32_t levelCount = 0;
    AllocatedImage image = {};
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::queue<uint32_t> freeLayers;
    bool initialized = false;
};
