#include "shadowSystem.h"
#include "System/Render/renderTypes.h"
#include "Util/profiler.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/ext/matrix_transform.hpp"

void ShadowSystem::Update(ECS *ecs)
{
    PROFILER_PROFILE_SCOPE("ShadowSystem::Update");

    ASSERT(entities.size() <= MAX_SHADOWCASTERS && "Too many shadowcasters");

    for (const Entity e : entities) {
        Transform& transform = ecs->GetComponent<Transform>(e);
        Shadowcaster& shadowcaster = ecs->GetComponent<Shadowcaster>(e);
        Light& light = ecs->GetComponent<Light>(e);

        ASSERT(shadowcaster.shadowIndex == light.shadowIndex && "Shadow index mismatch");

        glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
        shadowcaster.view = glm::lookAt(transform.translation, transform.translation - glm::vec3(light.direction), up);

        light.lightVP = shadowcaster.projection * shadowcaster.view;
    }
}
