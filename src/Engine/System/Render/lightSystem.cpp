#include "lightSystem.h"
#include "Util/profiler.h"

void LightSystem::Update()
{
    PROFILER_PROFILE_SCOPE("LightSystem::Update");

    ECS& ecs = ECS::Get();

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

        glm::quat r = transform.rotation;
        light.direction.x = 2.0 * (r.x * r.z + r.w * r.y);
        light.direction.y = 2.0 * (r.y * r.z - r.w * r.x);
        light.direction.z = 1.0 - 2.0 * (r.x * r.x + r.y * r.y);
    }
}
