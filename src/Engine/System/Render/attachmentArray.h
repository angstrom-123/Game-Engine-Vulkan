#pragma once

#include "Util/allocator.h"
#include "vulkan_core.h"
#include <queue>

class AttachmentArray {
public:
    AttachmentArray(uint32_t resolution, VkFormat format);
    void Init(uint32_t layerCount, class VulkanBackendV2& backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate();

private:
    bool IsDepthFormat(VkFormat format);

public:
    uint32_t resolution = 0;
    uint32_t layerCount = 0;
    AllocatedImage image = {};
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> arrayViews;
    VkImageView view;
    std::queue<uint32_t> freeLayers;
    bool initialized = false;
};
