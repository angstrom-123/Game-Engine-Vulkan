#pragma once

#include "glm/ext/vector_int2.hpp"
#include <filesystem>

enum ImageFlag {
    IMAGE_FLAG_NONE         = 0x0,
    IMAGE_FLAG_NON_COLOR    = 0x1,
    IMAGE_FLAG_TRANSPARENT  = 0x2,
};

enum ImageLoadFlag {
    IMAGE_LOAD_FLAG_NONE                = 0x0,
    IMAGE_LOAD_FLAG_NON_COLOR           = 0x1,
    IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY  = 0x2,
};

struct ImageResource {
    uint32_t flags = 0;
    glm::ivec2 size = {};
    uint8_t *pixels = nullptr;

    bool Load(const std::filesystem::path& path, uint32_t imageLoadFlags);
    void Cleanup();
    bool Resize(glm::ivec2 newSize);
    size_t SizeBytes();
};
