#include "transform.h"

#include "ECS/ecs.h"

Transform& Transform::InheritFrom(Entity parent)
{
    inherit = parent;
    return *this;
}

Transform& Transform::Rotate(float radians, const glm::vec3& axis)
{
    glm::quat q = glm::angleAxis(radians, glm::normalize(axis));
    rotation = q * glm::quat(1.0, 0.0, 0.0, 0.0);
    return *this;
}

Transform& Transform::Scale(float scaleFactor)
{
    scale = glm::vec3(scaleFactor);
    return *this;
}

Transform& Transform::Translate(const glm::vec3& position)
{
    translation = position;
    return *this;
}

Transform& Transform::Translate(float x, float y, float z)
{
    translation = glm::vec3(x, y, z);
    return *this;
}

glm::vec3 Transform::GlobalTranslation(ECS *ecs) const 
{
    glm::vec3 t = translation;
    if (inherit != INVALID_HANDLE) {
        t += ecs->GetComponent<Transform>(inherit).GlobalTranslation(ecs);
    }
    return t;
}

glm::quat Transform::GlobalRotation(ECS *ecs) const
{
    glm::quat r = rotation;
    if (inherit != INVALID_HANDLE) {
        r += ecs->GetComponent<Transform>(inherit).GlobalRotation(ecs);
    }
    return r;
}

glm::vec3 Transform::GlobalScale(ECS *ecs) const
{
    glm::vec3 s = scale;
    if (inherit != INVALID_HANDLE) {
        s += ecs->GetComponent<Transform>(inherit).GlobalScale(ecs);
    }
    return s;
}

glm::mat4x4 Transform::LocalModelMatrix()
{
    glm::mat4x4 model = glm::mat4x4(1.0);
    model = glm::translate(model, translation);
    model = model * glm::mat4x4(rotation);
    model = glm::scale(model, scale);
    return model;
}

glm::mat4x4 Transform::GlobalModelMatrix(ECS *ecs)
{
    glm::mat4x4 model = LocalModelMatrix();
    if (inherit != INVALID_HANDLE) {
        model = ecs->GetComponent<Transform>(inherit).GlobalModelMatrix(ecs) * model;
    }

    return model;
}
