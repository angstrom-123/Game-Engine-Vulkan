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
    TextureArray() = default;
    TextureArray(uint32_t resolution, VkFormat format, TextureKind kind);
    void Init(uint32_t layerCount, class VulkanBackend& backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    uint32_t Allocate(const ImageResource& image, class VulkanBackend& backend);

public:
    uint32_t resolution{0};
    uint32_t layerCount{0};
    uint32_t levelCount{0};
    AllocatedImage image;
    VkImageView view{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_UNDEFINED};
    TextureKind textureKind{TextureKind::NONE};

private:
    void GenerateMipsColor(VkCommandBuffer commandBuffer, AllocatedImage stagingImage, const ImageResource& imageData, uint32_t layerIndex);
    void GenerateMipsNormal(VkCommandBuffer commandBuffer, AllocatedImage stagingImage, const ImageResource& imageData, uint32_t layerIndex);

private:
    std::queue<uint32_t> m_FreeLayers;
    bool m_Initialized{false};
};
