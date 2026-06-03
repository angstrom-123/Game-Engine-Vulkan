#pragma once

#include "System/Render/backendTypes.h"
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

enum class MaterialFlag : uint32_t {
    NONE          = 0x00000000,
    TRANSPARENT   = 0x00000001,
    DOUBLE_SIDED  = 0x00000002,
    CUTOUT        = 0x00000004,
};
using MaterialFlags = uint32_t;

struct Material {
    glm::vec4 albedo{1.0};        // rgb + alpha
    float roughness{1.0};
    float metallic{0.0};
    float ao{1.0};
    float emissive{0.0};
    AllocatedTexture diffuseTexture;
    AllocatedTexture normalTexture;
    MaterialFlags flags{0};

    FragmentPushConstants::Material Pack();
};
