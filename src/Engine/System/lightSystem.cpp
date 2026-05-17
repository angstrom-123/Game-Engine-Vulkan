#include "lightSystem.h"
#include "System/Render/backendTypes.h"
#include "Util/profiler.h"

void LightSystem::Update(ECS *ecs)
{
    PROFILER_PROFILE_SCOPE("LightSystem::Update");

    ASSERT(entities.size() <= MAX_LIGHTS && "Too many lights");

    for (const Entity e : entities) {
        Transform& transform = ecs->GetComponent<Transform>(e);
        Light& light = ecs->GetComponent<Light>(e);

        glm::vec3 position = transform.GlobalTranslation(ecs);

        light.position.x = position.x;
        light.position.y = position.y;
        light.position.z = position.z;

        glm::quat r = transform.rotation;
        light.direction.x = 2.0 * (r.x * r.z + r.w * r.y);
        light.direction.y = 2.0 * (r.y * r.z - r.w * r.x);
        light.direction.z = 1.0 - 2.0 * (r.x * r.x + r.y * r.y);
    }
}
