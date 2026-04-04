#include "imageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

ImageData::ImageData(const std::filesystem::path& filePath)
{
    stbi_set_flip_vertically_on_load(1);
    pixels = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) corrupted = true;

    hasTransparency = false;
    if (channels < 4) return;

    for (int i = 0; i < width * height; i++) {
        if (pixels[i * 4 + 3] < 255) {
            hasTransparency = true;
            return;
        }
    }
}
