#pragma once

#include "System/Render/textureArrayHandler.h"
#include "Util/imageLoader.h"
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

enum MaterialFlags {
    MATERIAL_FLAG_NONE          = 0x00000000,
    MATERIAL_FLAG_TRANSPARENT   = 0x00000001,
    MATERIAL_FLAG_MAX_ENUM      = 0x00000002,
    _MATERIAL_FLAG_FORCE_32_BIT = 0x10000000,
};

struct MaterialTextureInfo {
    ImageData *ambientTextureData;
    ImageData *diffuseTextureData;
    ImageData *normalTextureData;
};

struct Material {
    glm::vec3 baseColor;
    float specularExponent;
    TextureAllocation ambientTexture;
    TextureAllocation diffuseTexture;
    TextureAllocation normalTexture;
    uint32_t flags;
};
