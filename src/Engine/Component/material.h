#pragma once

#include "System/Render/textureArrayHandler.h"
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
    TextureAllocation ambientTexture;
    TextureAllocation diffuseTexture;
    TextureAllocation normalTexture;
};
