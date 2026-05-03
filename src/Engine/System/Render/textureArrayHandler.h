#pragma once

#include "ResourceManager/imageResource.h"
#include "ResourceManager/resourceTypes.h"
#include "textureArray.h"
#include <unordered_map>

enum TextureArrayID {
    TEXTURE_ARRAY_COLOR_1024,
    TEXTURE_ARRAY_COLOR_2048,
    TEXTURE_ARRAY_DATA_1024,
    TEXTURE_ARRAY_DATA_2048,
    TEXTURE_ARRAY_MAX_ENUM
};

struct TextureArraySizes {
    uint32_t color1024;
    uint32_t color2048;
    uint32_t data1024;
    uint32_t data2048;
};

struct TextureAllocation {
    TextureArrayID arrayID;
    uint32_t textureID;
};

class TextureArrayHandler {
public:
    void Init(const TextureArraySizes& sizes, class VulkanBackend *backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    TextureAllocation AllocateTexture(ImageResource& imageData, class VulkanBackend *backend);
    TextureAllocation GetFallbackColorTexture();
    TextureAllocation GetFallbackNormalTexture();

private:
    TextureArray m_Arrays[TEXTURE_ARRAY_MAX_ENUM];
    Resource m_DefaultColorTexture;
    Resource m_DefaultNormalTexture;
};
