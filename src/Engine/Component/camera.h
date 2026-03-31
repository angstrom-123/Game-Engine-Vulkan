#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::mat4x4 view;
    glm::mat4x4 projection;
    float near;
    float far;
    float pitch;
    float yaw;
    float aspect;
    float fov;

    Camera() = default;
    Camera(glm::vec3 position, glm::vec2 dimensions, float fovRadians, float nearClip, float farClip)
    {
        view = glm::lookAt(position, position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, 1.0, 0.0));
        projection = VulkanPerspective(fovRadians, dimensions.x / dimensions.y, nearClip, farClip);
        near = nearClip;
        far = farClip;
        aspect = dimensions.x / dimensions.y;
        fov = fovRadians;
    }

    static glm::mat4x4 VulkanPerspective(float fovRadians, float aspect, float near, float far)
    {
        glm::mat4x4 p = glm::perspective(fovRadians, aspect, near, far);
        p[1][1] *= -1.0;
        return p;
    }
};
