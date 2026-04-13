#pragma once

#include <filesystem>

enum ImageKind {
    IMAGE_KIND_COLOR,
    IMAGE_KIND_NORMAL,
    IMAGE_KIND_MAX_ENUM
};

class ImageData {
public:
    bool LoadImage(const std::filesystem::path& filePath, bool checkTransparency, ImageKind kind);
    void Resize(int width, int height);

public:
    ImageKind kind;
    std::string path;
    bool hasTransparency;
    int width;
    int height;
    int channels;
    uint8_t *pixels;
};
