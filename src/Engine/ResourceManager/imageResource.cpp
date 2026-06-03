#include "imageResource.h"
#include "Util/enumIndex.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "Util/myAssert.h"

bool ImageResource::Load(const std::filesystem::path& path, uint32_t imageLoadFlags)
{
    stbi_set_flip_vertically_on_load(1);
    pixels = stbi_load(path.generic_string().c_str(), &size.x, &size.y, &channels, STBI_rgb_alpha);
    if (!pixels || channels != STBI_rgb_alpha) {
        ERROR("Failed to load image");
        return false;
    }

    if (imageLoadFlags & EnumBase(ImageLoadFlag::NON_COLOR)) {
        flags |= EnumBase(ImageFlag::NON_COLOR);
    }

    if (imageLoadFlags & EnumBase(ImageLoadFlag::CHECK_TRANSPARENCY)) {
        for (int i = 0; i < size.x * size.y; i++) {
            int pixel = pixels[i * 4 + 3];
            if (pixel < 255) {
                flags |= EnumBase(ImageFlag::CUTOUT);
                break;
            }
        }
    }

    return true;
}
