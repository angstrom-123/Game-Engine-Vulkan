#pragma once

#include "ResourceManager/imageResource.h"
#include "Util/allocator.h"
#include "vulkan_core.h"
#include <queue>

enum class TextureKind : uint32_t {
    NONE,
    COLOR,
    NORMAL,
};

class TextureArray {
public:
    TextureArray(uint32_t resolution, VkFormat format, TextureKind kind);
    void Init(uint32_t layerCount, class VulkanBackend& backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate(ImageResource& image, class VulkanBackend& backend);

public:
    uint32_t resolution{0};
    uint32_t layerCount{0};
    uint32_t levelCount{0};
    AllocatedImage image;
    VkImageView view{VK_NULL_HANDLE};

private:
    void GenerateMipsColor();
    void GenerateMipsNormal();

private:
    TextureKind m_TextureKind{TextureKind::NONE};
    VkFormat m_Format{VK_FORMAT_UNDEFINED};
    std::queue<uint32_t> m_FreeLayers;
    bool m_Initialized{false};
};
