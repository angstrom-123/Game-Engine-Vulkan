#pragma once

#include "renderTypes.h"
#include "textureArray.h"

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
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, CommandSubmitter& submitter, VmaAllocator allocator, FrameData *frames, TextureArraySizes& sizes);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    TextureAllocation AllocateTexture(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, CommandSubmitter& submitter, VmaAllocator allocator, ImageData *imageData);
    TextureAllocation GetFallbackTexture(ImageData *imageData, ImageKind kind);

public:
    VkDescriptorSetLayout descriptorLayout;

private:
    TextureAllocation m_DefaultColorAllocation;
    TextureAllocation m_DefaultNormalAllocation;
    TextureArray m_Arrays[TEXTURE_ARRAY_MAX_ENUM];
    std::unordered_map<std::string, TextureAllocation> m_AllocatedTextures;
};
