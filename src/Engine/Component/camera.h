#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

enum Perspective { CAMERA_PERSPECTIVE };
enum Orthographic { CAMERA_ORTHOGRAPHIC };

struct Camera {
    glm::mat4x4 view = glm::mat4x4(0.0);
    glm::mat4x4 projection = glm::mat4x4(0.0);
    bool perspective = true;
    float fov = 60.0;
    float scale = 0.01;
    float near = 0.01;
    float far = 10.0;
    float pitch = 0.0;
    float yaw = 0.0;

    Camera() = default;
    Camera(Perspective, glm::vec3 position, glm::vec2 dimensions, float fovRadians = glm::radians(60.0), float near = 0.01, float far = 1000.0);
    Camera(Orthographic, glm::vec3 position, glm::vec2 dimensions, float scale = 1.0, float near = 0.1, float far = 100.0);
    static glm::mat4x4 VulkanPerspective(float fovRadians, float aspect, float near, float far);
    static glm::mat4x4 VulkanOrthographic(float minX, float maxX, float minY, float maxY, float near, float far);
};
