#include "shadowSystem.h"
#include "Component/light.h"
#include "System/Render/backendTypes.h"
#include "Util/profiler.h"

void ShadowSystem::Update(ECS *ecs, Entity cameraEntity)
{
    PROFILER_PROFILE_SCOPE("ShadowSystem::Update");

    Camera& camera = ecs->GetComponent<Camera>(cameraEntity);
    for (const Entity e : entities) {
        Transform& transform = ecs->GetComponent<Transform>(e);
        Shadowcaster& shadowcaster = ecs->GetComponent<Shadowcaster>(e);
        Light& light = ecs->GetComponent<Light>(e);

        ASSERT(light.shadowIndex != UINT32_MAX && "Unallocated shadowcaster");
        if (static_cast<uint32_t>(light.position.w) == DIRECTIONAL) {
            FrustumCorners corners[SHADOW_CASCADE_COUNT];
            Shadowcaster::CalculateFrustumCorners(camera, corners);
            for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
                ASSERT(shadowcaster.cascades[i].shadowMapIndex != UINT32_MAX && "Unallocated shadow map");
                shadowcaster.cascades[i].vp = Shadowcaster::CalculateDirectionalVP(corners[i], light);
                shadowcaster.cascades[i].far = corners[i].far;
                shadowcaster.cascades[i].near = corners[i].near;
            }

            // For cascade blending overlap near / far planes
            const float CASCADE_BLENDING = 0.15;
            for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT - 1; i++) {
                float overlap = shadowcaster.cascades[i].far * CASCADE_BLENDING;
                shadowcaster.cascades[i].far += overlap;
                shadowcaster.cascades[i + 1].near -= overlap;
            }
        } else { // SPOT
            ASSERT(shadowcaster.cascades[0].shadowMapIndex != UINT32_MAX && "Unallocated shadow map");
            shadowcaster.cascades[0].vp = Shadowcaster::CalculateSpotVP(light);
            shadowcaster.cascades[0].far = light.radius;
        }
    }
}
