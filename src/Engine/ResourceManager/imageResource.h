#pragma once

#include "glm/ext/vector_int2.hpp"
#include <filesystem>
#include <utility>
#include <stb/stb_image.h>

enum ImageFlag {
    IMAGE_FLAG_NONE         = 0x0,
    IMAGE_FLAG_NON_COLOR    = 0x1,
    IMAGE_FLAG_TRANSPARENT  = 0x2,
    IMAGE_FLAG_FONT_ATLAS   = 0x4
};

enum ImageLoadFlag {
    IMAGE_LOAD_FLAG_NONE                = 0x0,
    IMAGE_LOAD_FLAG_NON_COLOR           = 0x1,
    IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY  = 0x2,
};

class ImageResource {
public:
    ImageResource(uint32_t flags, glm::ivec2 size, uint8_t *pixels, int32_t channels)
        : flags(flags), size(size), pixels(pixels), channels(channels) {}
    ImageResource() = default;
    ~ImageResource() { if (pixels != nullptr) stbi_image_free(pixels); }
    ImageResource(const ImageResource&) = delete;
    ImageResource& operator=(const ImageResource&) = delete;
    ImageResource(ImageResource&& other) 
        : flags(other.flags), size(other.size), pixels(std::exchange(other.pixels, nullptr)), channels(other.channels) {}
    ImageResource& operator=(ImageResource&& other) noexcept
    {
        if (this != &other) {
            if (pixels != nullptr) stbi_image_free(pixels);
            pixels = std::exchange(other.pixels, nullptr);
            size = other.size;
            flags = other.flags;
            channels = other.channels;
        }
        return *this;
    };

    bool Load(const std::filesystem::path& path, uint32_t imageLoadFlags);
    bool Resize(glm::ivec2 newSize);

public:
    uint32_t flags = 0;
    glm::ivec2 size = {0, 0};
    uint8_t *pixels = nullptr;
    int32_t channels = 0;
};
