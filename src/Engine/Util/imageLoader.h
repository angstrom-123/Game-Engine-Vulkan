#pragma once

#include <filesystem>

class ImageData {
public:
    bool LoadImage(const std::filesystem::path& filePath, bool checkTransparency);
    void Resize(int width, int height);

public:
    bool hasTransparency;
    int width;
    int height;
    int channels;
    uint8_t *pixels;
};
