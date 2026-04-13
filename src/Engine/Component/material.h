#pragma once

#include "Util/imageLoader.h"
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

struct MaterialTextureInfo {
    ImageData *ambientTextureData;
    ImageData *diffuseTextureData;
    ImageData *normalTextureData;
};

struct Material {
    bool hasTransparency;
    float specularExponent;
    uint32_t ambientTextureIndex;
    uint32_t diffuseTextureIndex;
    uint32_t normalTextureIndex;
};
