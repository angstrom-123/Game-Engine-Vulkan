#pragma once

#include "ResourceManager/imageResource.h"
#include "textureArray.h"
#include <cstdint>

enum TextureArrayID {
    TEXTURE_ARRAY_COLOR_1024,
    TEXTURE_ARRAY_COLOR_2048,
    TEXTURE_ARRAY_DATA_1024,
    TEXTURE_ARRAY_DATA_2048,
    TEXTURE_ARRAY_FONT,
    TEXTURE_ARRAY_MAX_ENUM
};

struct TextureArraySizes {
    uint32_t color1024;
    uint32_t color2048;
    uint32_t data1024;
    uint32_t data2048;
    uint32_t font;
};

struct TextureAllocation {
    TextureArrayID arrayID = TEXTURE_ARRAY_MAX_ENUM;
    uint32_t layerID = UINT32_MAX;
};

class TextureArrayHandler {
public:
    void Init(const TextureArraySizes& sizes, class VulkanBackend *backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    TextureAllocation AllocateTexture(ImageResource& imageData, class VulkanBackend *backend);
    TextureAllocation AllocateRaw(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, class VulkanBackend *backend);

private:
    TextureArrayID SelectArray(glm::ivec2 size, uint32_t flags);

private:
    TextureArray m_Arrays[TEXTURE_ARRAY_MAX_ENUM];
};
