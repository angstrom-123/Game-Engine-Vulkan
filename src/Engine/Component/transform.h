#pragma once

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Util/logger.h"

struct Transform {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

    Transform()
    {
        translation = glm::vec3(0.0);
        rotation = glm::quat(1.0, 0.0, 0.0, 0.0);
        scale = glm::vec3(1.0);
    }

    void Rotate(float thetaRadians, const glm::vec3& axis)
    {
        glm::quat q = glm::angleAxis(thetaRadians, glm::normalize(axis));
        rotation = q * rotation;
    }

    void Log()
    {
        INFO("Transform: " << std::endl <<
             "    translation: " << translation.x << ", " << translation.y << ", " << translation.z << std::endl <<
             "    rotation   : " << rotation.x << ", " << rotation.y << ", " << rotation.z << ", " << rotation.w << std::endl <<
             "    scale      : " << scale.x << ", " << scale.y << ", " << scale.z);
    }

    static glm::mat4x4 ToModelMatrix(Transform& t)
    {
        glm::mat4x4 model = glm::mat4x4(1.0);
        model = glm::translate(model, t.translation);
        model = model * glm::mat4x4(t.rotation);
        model = glm::scale(model, t.scale);
        return model;
    }
};
