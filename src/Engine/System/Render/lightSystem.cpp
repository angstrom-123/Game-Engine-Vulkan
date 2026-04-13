#include "lightSystem.h"

void LightSystem::Update(ECS& ecs)
{
    for (const Entity e : entities) {
        Transform& transform = ecs.GetComponent<Transform>(e);
        Light& light = ecs.GetComponent<Light>(e);

        glm::vec3 position = transform.translation;
        if (transform.inherit != INVALID_HANDLE) {
            position += ecs.GetComponent<Transform>(transform.inherit).translation;
        }

        light.position.x = position.x;
        light.position.y = position.y;
        light.position.z = position.z;
    }
}
