#include "defaultCameraSystem.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_common.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>

void DefaultCameraSystem::Update(ECS& ecs, bool *keysDown, glm::vec2 mouseDelta, double dt)
{
    constexpr float MOVE_SPEED = 4.0;
    constexpr float MOUSE_SENSITIVITY = 0.04;
    constexpr float MAX_PITCH = 89.9;

    for (Entity e : entities) {
        Camera& camera = ecs.GetComponent<Camera>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);

        // Keyboard input
        glm::vec3 forward = transform.rotation * glm::vec3(0.0, 0.0, -1.0);
        glm::vec3 right = transform.rotation * glm::vec3(1.0, 0.0, 0.0);
        glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);

        glm::vec3 move = glm::vec3(0.0);
        if (keysDown[GLFW_KEY_W]) move += forward;
        if (keysDown[GLFW_KEY_S]) move -= forward;
        if (keysDown[GLFW_KEY_D]) move += right;
        if (keysDown[GLFW_KEY_A]) move -= right;
        if (keysDown[GLFW_KEY_SPACE]) move += up;
        if (keysDown[GLFW_KEY_LEFT_CONTROL]) move -= up;

        // Early return if there is nothing to update
        if (move == glm::vec3(0.0) && mouseDelta == glm::vec2(0.0)) 
            return;

        transform.translation += move * MOVE_SPEED * static_cast<float>(dt);

        // Mouse input
        camera.pitch += -mouseDelta.y * MOUSE_SENSITIVITY;
        camera.yaw += -mouseDelta.x * MOUSE_SENSITIVITY;

        camera.pitch = std::clamp(camera.pitch, -MAX_PITCH, MAX_PITCH);

        glm::quat yawQuaternion = glm::angleAxis(glm::radians(camera.yaw), up);

        // Recompute right axis with the new yaw applied
        glm::vec3 localRight = yawQuaternion * glm::vec3(1.0, 0.0, 0.0);
        glm::quat pitchQuaternion = glm::angleAxis(glm::radians(camera.pitch), localRight);

        // Yaw applied first, then pitch
        transform.rotation = glm::normalize(pitchQuaternion * yawQuaternion);

        // Apply rotation and translation
        glm::mat4 rotationMatrix = glm::mat4_cast(glm::conjugate(transform.rotation));
        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0), -transform.translation);
        camera.view = rotationMatrix * translationMatrix;
    };
}
