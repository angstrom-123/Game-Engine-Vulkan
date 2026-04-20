#pragma once 

#include "glm/ext/matrix_float4x4.hpp"
#include "camera.h"

enum Perspective { PERSPECTIVE };
enum Orthographic { ORTHOGRAPHIC };

struct Shadowcaster {
    glm::mat4 view;
    glm::mat4 projection;
    uint32_t shadowIndex;

    Shadowcaster() = default;

    Shadowcaster(Perspective, float fovRadians, float near, float far)
    {
        this->projection = Camera::VulkanPerspective(fovRadians, 1.0, near, far);
        shadowIndex = UINT32_MAX;
    }

    Shadowcaster(Orthographic, float minX, float maxX, float minY, float maxY, float near, float far)
    {
        this->projection = Camera::VulkanOrthographic(minX, maxX, minY, maxY, near, far);
        shadowIndex = UINT32_MAX;
    }
};
