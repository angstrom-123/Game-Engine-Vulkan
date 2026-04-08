#pragma once

#include "Util/imageLoader.h"
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

struct MaterialTextureInfo {
    ImageData *ambientTextureData;
    ImageData *diffuseTextureData;
    ImageData *normalTextureData;
};

struct PushConstants {
    glm::mat4x4 model;
    float specularExponent;
    uint32_t ambientIndex;
    uint32_t diffuseIndex;
    uint32_t normalIndex;
};

struct GlobalUniforms {
    glm::mat4x4 vp;
    glm::vec4 lightPos;
    glm::vec4 viewPos;
};

struct Material {
    bool hasTransparency;
    float specularExponent;
    uint32_t ambientTextureIndex;
    uint32_t diffuseTextureIndex;
    uint32_t normalTextureIndex;
};
