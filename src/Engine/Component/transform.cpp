#include "transform.h"

#include "ECS/ecs.h"

Transform::Transform()
{
    translation = glm::vec3(0.0);
    rotation = glm::quat(1.0, 0.0, 0.0, 0.0);
    scale = glm::vec3(1.0);
    inherit = INVALID_HANDLE;
}

void Transform::InheritFrom(Entity parent)
{
    inherit = parent;
}

Transform& Transform::Rotate(float radians, const glm::vec3& axis)
{
    glm::quat q = glm::angleAxis(radians, glm::normalize(axis));
    rotation = q * rotation;
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

glm::mat4x4 Transform::LocalModelMatrix()
{
    glm::mat4x4 model = glm::mat4x4(1.0);
    model = glm::translate(model, translation);
    model = model * glm::mat4x4(rotation);
    model = glm::scale(model, scale);
    return model;
}

glm::mat4x4 Transform::GlobalModelMatrix()
{
    glm::mat4x4 model = LocalModelMatrix();
    if (inherit != INVALID_HANDLE) {
        model = ECS::Get().GetComponent<Transform>(inherit).GlobalModelMatrix() * model;
    }

    return model;
}
