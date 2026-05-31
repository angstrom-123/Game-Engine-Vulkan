#pragma once 

#include "Component/light.h"
#include "System/Render/backendTypes.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "camera.h"
#include <cmath>
#include <cstring>

struct FrustumCorners {
    glm::vec4 corners[8];
    glm::vec3 centre{0.0};
    float near{0.0};
    float far{0.0};
};

struct Shadowcaster {
    struct Cascade {
        alignas(16) glm::mat4x4 vp{0.0};
        alignas(4) uint32_t shadowMapIndex{0};
        alignas(4) float far{-1.0};
        alignas(4) float near{-1.0};
    } cascades[SHADOW_CASCADE_COUNT];

    static void CalculateFrustumCorners(const Camera& camera, FrustumCorners (&cascades)[SHADOW_CASCADE_COUNT]);
    static glm::mat4x4 CalculateDirectionalVP(const FrustumCorners& corners, const Light& light, const VulkanBackendSettings& settings);
    static glm::mat4x4 CalculateSpotVP(const Light& light);
};
