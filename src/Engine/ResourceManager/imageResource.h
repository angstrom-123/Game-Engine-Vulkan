#pragma once

#include "glm/ext/vector_int2.hpp"
#include <filesystem>
#include <utility>
#include <stb/stb_image.h>

enum class ImageFlag : uint32_t {
    NONE         = 0x0,
    NON_COLOR    = 0x1,
    TRANSPARENT  = 0x2,
    CUTOUT       = 0x4,
    FONT_ATLAS   = 0x8
};
using ImageFlags = uint32_t;

enum class ImageLoadFlag : uint32_t {
    NONE                = 0x0,
    NON_COLOR           = 0x1,
    CHECK_TRANSPARENCY  = 0x2,
};
using ImageLoadFlags = uint32_t;

class ImageResource {
public:
    ImageResource(ImageFlags flags, glm::ivec2 size, uint8_t *pixels, int32_t channels)
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

public:
    ImageFlags flags = 0;
    glm::ivec2 size = {0, 0};
    uint8_t *pixels = nullptr;
    int32_t channels = 0;
};
