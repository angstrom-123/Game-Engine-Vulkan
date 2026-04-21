#include "camera.h"

Camera::Camera(glm::vec3 position, glm::vec2 dimensions, float fovRadians, float nearClip, float farClip)
{
    view = glm::lookAt(position, position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, 1.0, 0.0));
    projection = VulkanPerspective(fovRadians, dimensions.x / dimensions.y, nearClip, farClip);
    near = nearClip;
    far = farClip;
    aspect = dimensions.x / dimensions.y;
    fov = fovRadians;
    pitch = 0.0;
    yaw = 0.0;
}

glm::mat4x4 Camera::VulkanPerspective(float fovRadians, float aspect, float near, float far)
{
    glm::mat4x4 p = glm::perspective(fovRadians, aspect, near, far);
    p[1][1] *= -1.0;
    return p;
}

glm::mat4x4 Camera::VulkanOrthographic(float left, float right, float bottom, float top, float near, float far)
{
    glm::mat4x4 o = glm::ortho(left, right, bottom, top, near, far);
    o[1][1] *= -1.0;
    return o;
}
