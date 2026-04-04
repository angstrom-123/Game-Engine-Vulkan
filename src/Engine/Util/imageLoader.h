#pragma once

#include <filesystem>

class ImageData {
public:
    ImageData(const std::filesystem::path& filePath);

public:
    bool corrupted;
    bool hasTransparency;
    int width;
    int height;
    int channels;
    uint8_t *pixels;
};
