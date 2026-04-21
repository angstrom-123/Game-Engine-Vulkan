#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::mat4x4 view;
    glm::mat4x4 projection;
    float near;
    float far;
    float aspect;
    float fov;
    float pitch;
    float yaw;

    Camera() = default;
    Camera(glm::vec3 position, glm::vec2 dimensions, float fovRadians, float nearClip, float farClip);
    static glm::mat4x4 VulkanPerspective(float fovRadians, float aspect, float near, float far);
    static glm::mat4x4 VulkanOrthographic(float minX, float maxX, float minY, float maxY, float near, float far);
};
