#include "imageResource.h"
#include "Util/flags.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

#include "Util/myAssert.h"

constexpr int32_t CHANNEL_COUNT = 4;

bool ImageResource::Load(const std::filesystem::path& path, uint32_t imageLoadFlags)
{
    stbi_set_flip_vertically_on_load(1);
    int channels;
    pixels = stbi_load(path.c_str(), &size.x, &size.y, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return false;
    }

    if (channels != CHANNEL_COUNT) {
        ERROR("Loading image with missing channels");
        return false;
    }

    if (FLAGS_HAVE_BIT(imageLoadFlags, IMAGE_LOAD_FLAG_NON_COLOR)) {
        flags |= IMAGE_FLAG_NON_COLOR;
    }

    if (FLAGS_HAVE_BIT(imageLoadFlags, IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY)) {
        for (int i = 0; i < size.x * size.y; i++) {
            if (pixels[i * 4 + 3] < 255) {
                flags |= IMAGE_FLAG_TRANSPARENT;
                break;
            }
        }
    }

    return true;
}

void ImageResource::Cleanup()
{
    ASSERT(pixels != nullptr && "Cleaning up unloaded image");
    stbi_image_free(pixels);
}

bool ImageResource::Resize(glm::ivec2 newSize)
{
    uint8_t *newPixels = stbir_resize_uint8_linear(pixels, size.x, size.y, 0, 0, newSize.x, newSize.y, 0, STBIR_RGBA);
    if (!newPixels) {
        ERROR("Failed to resize image");
        return false;
    }
    stbi_image_free(pixels);
    pixels = newPixels;
    size = newSize;
    return true;
}

size_t ImageResource::SizeBytes()
{
    return size.x * size.y * CHANNEL_COUNT;
}
