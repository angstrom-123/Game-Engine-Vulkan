#pragma once

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Util/logger.h"
#include "ECS/ecsTypes.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#define X_AXIS glm::vec3(1.0, 0.0, 0.0)
#define Y_AXIS glm::vec3(0.0, 1.0, 0.0)
#define Z_AXIS glm::vec3(0.0, 0.0, -1.0)

class ECS;

struct Transform {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
    Entity inherit;

    Transform();
    void InheritFrom(Entity e);
    Transform& Rotate(float radians, const glm::vec3& axis);
    Transform& Scale(float factor);
    Transform& Translate(const glm::vec3& translation);
    Transform& Translate(float x, float y, float z);
    glm::mat4x4 LocalModelMatrix();
    glm::mat4x4 GlobalModelMatrix(ECS *ecs);
};
