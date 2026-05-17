#pragma once

#include "System/Render/backendTypes.h"
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

enum MaterialFlags {
    MATERIAL_FLAG_NONE          = 0x00000000,
    MATERIAL_FLAG_TRANSPARENT   = 0x00000001,
    MATERIAL_FLAG_DOUBLE_SIDED  = 0x00000002,
    MATERIAL_FLAG_CUTOUT        = 0x00000004,
    _MATERIAL_FLAG_FORCE_32_BIT = 0x10000000,
};

struct Material {
    glm::vec4 albedo{1.0};        // rgb + alpha
    float roughness{1.0};
    float metallic{0.0};
    float ao{1.0};
    float emissive{0.0};
    AllocatedTexture diffuseTexture;
    AllocatedTexture normalTexture;
    uint32_t flags{0};

    FragmentPushConstants::Material Pack();
    void UseTrueTransparency() { flags |= MATERIAL_FLAG_TRANSPARENT; }
};
