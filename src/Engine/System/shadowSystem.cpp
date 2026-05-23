#include "shadowSystem.h"
#include "Util/profiler.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/ext/matrix_transform.hpp"

void ShadowSystem::Update(ECS *ecs)
{
    PROFILER_PROFILE_SCOPE("ShadowSystem::Update");

    for (const Entity e : entities) {
        Transform& transform = ecs->GetComponent<Transform>(e);
        Shadowcaster& shadowcaster = ecs->GetComponent<Shadowcaster>(e);
        Light& light = ecs->GetComponent<Light>(e);

        ASSERT(shadowcaster.shadowIndex == light.shadowIndex && "Shadow index mismatch");

        shadowcaster.view = glm::lookAt(transform.translation, transform.translation - glm::vec3(light.direction), glm::vec3(0.0, 1.0, 0.0));
        
        // Tiny on-axis rotation to break up stepped artifacts in lit areas
        float angle = glm::radians(5.0);
        glm::vec3 dir = glm::vec3(light.direction);
        shadowcaster.view *= glm::rotate(glm::mat4x4(1.0), angle, dir);

        light.lightVP = shadowcaster.projection * shadowcaster.view;
    }
}
