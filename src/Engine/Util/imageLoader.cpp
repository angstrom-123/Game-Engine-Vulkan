#include "imageLoader.h"

#include "logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

bool ImageData::LoadImage(const std::filesystem::path& filePath, bool checkTransparency, ImageKind imageKind)
{
    path = filePath;

    stbi_set_flip_vertically_on_load(1);
    pixels = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return false;
    }

    hasTransparency = false;
    kind = imageKind;

    if (channels < 4) return true;
    if (checkTransparency) {
        for (int i = 0; i < width * height; i++) {
            if (pixels[i * 4 + 3] < 255) {
                hasTransparency = true;
                return true;
            }
        }
    }
    return true;
}

void ImageData::Resize(int newWidth, int newHeight)
{
    uint8_t *newPixels = static_cast<uint8_t *>(malloc(newWidth * newHeight * channels));
    if (!stbir_resize_uint8_linear(pixels, width, height, 0, newPixels, newWidth, newHeight, 0, STBIR_RGBA)) {
        stbi_image_free(newPixels);
        ERROR("Failed to resize an image");
        abort();
    }
    stbi_image_free(pixels);
    pixels = newPixels;
    width = newWidth;
    height = newHeight;
}
