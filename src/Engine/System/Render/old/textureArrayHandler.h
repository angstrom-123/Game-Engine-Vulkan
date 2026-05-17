// #pragma once
//
// #include "ResourceManager/imageResource.h"
// #include "textureArray.h"
// #include <cstdint>
//
// enum TextureArrayID {
//     TEXTURE_ARRAY_COLOR_SMALL,
//     TEXTURE_ARRAY_COLOR_LARGE,
//     TEXTURE_ARRAY_DATA_SMALL,
//     TEXTURE_ARRAY_DATA_LARGE,
//     TEXTURE_ARRAY_FONT,
//     TEXTURE_ARRAY_MAX_ENUM
// };
//
// struct TextureArraySizes {
//     uint32_t colorSmall;
//     uint32_t colorLarge;
//     uint32_t dataSmall;
//     uint32_t dataLarge;
//     uint32_t font;
// };
//
// struct AllocatedTexture {
//     TextureArrayID arrayID = TEXTURE_ARRAY_MAX_ENUM;
//     uint32_t layerID = UINT32_MAX;
// };
//
// class TextureArrayHandler {
// public:
//     void Init(const TextureArraySizes& sizes, class VulkanBackend *backend);
//     void Cleanup(VkDevice device, VmaAllocator allocator);
//     AllocatedTexture AllocateTexture(ImageResource& imageData, class VulkanBackend *backend);
//     AllocatedTexture AllocateRaw(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, class VulkanBackend *backend);
//
// private:
//     TextureArrayID SelectArray(glm::ivec2 size, uint32_t flags);
//
// private:
//     TextureArray m_Arrays[TEXTURE_ARRAY_MAX_ENUM];
// };
