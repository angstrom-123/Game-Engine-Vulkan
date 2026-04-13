#pragma once

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Util/logger.h"
#include "ECS/ecsTypes.h"

#define X_AXIS glm::vec3(1.0, 0.0, 0.0)
#define Y_AXIS glm::vec3(0.0, 1.0, 0.0)
#define Z_AXIS glm::vec3(0.0, 0.0, -1.0)

struct Transform {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
    Entity inherit;

    Transform()
    {
        translation = glm::vec3(0.0);
        rotation = glm::quat(1.0, 0.0, 0.0, 0.0);
        scale = glm::vec3(1.0);
        inherit = INVALID_HANDLE;
    }

    void InheritFrom(Entity parent)
    {
        inherit = parent;
    }

    Transform& Rotate(float thetaRadians, const glm::vec3& axis)
    {
        glm::quat q = glm::angleAxis(thetaRadians, glm::normalize(axis));
        rotation = q * rotation;
        return *this;
    }

    Transform& Scale(float scaleFactor)
    {
        scale = glm::vec3(scaleFactor);
        return *this;
    }

    Transform& Translate(glm::vec3 position)
    {
        translation = position;
        return *this;
    }

    Transform& Translate(float x, float y, float z)
    {
        translation = glm::vec3(x, y, z);
        return *this;
    }

    glm::mat4x4 ModelMatrix()
    {
        glm::mat4x4 model = glm::mat4x4(1.0);
        model = glm::translate(model, translation);
        model = model * glm::mat4x4(rotation);
        model = glm::scale(model, scale);
        return model;
    }
};
