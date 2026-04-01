#pragma once

#include <filesystem>

class ImageData {
public:
    ImageData(const std::filesystem::path& filePath);

public:
    bool corrupted;
    int width;
    int height;
    int channels;
    uint8_t *pixels;
};
