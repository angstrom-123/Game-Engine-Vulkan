#include "shadowcaster.h"
#include "System/Render/backendTypes.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/ext/matrix_transform.hpp"

void Shadowcaster::CalculateFrustumCorners(const Camera& camera, FrustumCorners (&cascades)[SHADOW_CASCADE_COUNT])
{
    // Cascade distances
    const float range = camera.shadowDistance - camera.near;
    const float n = range / ((1 << SHADOW_CASCADE_COUNT) - 1); // (far - near) / (2^c - 1)
    
    for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
        float nearFactor = (1 << i) - 1;
        float farFactor = (1 << (i + 1)) - 1;
        cascades[i].near = camera.near + n * nearFactor;
        cascades[i].far = camera.near + n * farFactor;
    }
    cascades[0].near = camera.near;
    cascades[SHADOW_CASCADE_COUNT - 1].far = camera.shadowDistance;

    // Calculate view space frustum for each and transform to world space
    const glm::mat4x4 invVP = glm::inverse(camera.projection * camera.view);
    for (uint32_t i = 0; i < SHADOW_CASCADE_COUNT; i++) {
        FrustumCorners& cascade = cascades[i];

        // View space frustum
        float ndcNear = (camera.far * (cascade.near - camera.near)) / (cascade.near * (camera.far - camera.near));
        float ndcFar = (camera.far * (cascade.far - camera.near)) / (cascade.far * (camera.far - camera.near));
        glm::vec4 ndcCorners[8] = {
            {-1.0, -1.0, ndcNear, 1.0 },
            { 1.0, -1.0, ndcNear, 1.0 },
            {-1.0,  1.0, ndcNear, 1.0 },
            { 1.0,  1.0, ndcNear, 1.0 },
            {-1.0, -1.0, ndcFar, 1.0 },
            { 1.0, -1.0, ndcFar, 1.0 },
            {-1.0,  1.0, ndcFar, 1.0 },
            { 1.0,  1.0, ndcFar, 1.0 }
        };

        // World space transformation (also calculate world-space centre for later)
        for (uint32_t j = 0; j < 8; j++) {
            glm::vec4 worldCorner = invVP * ndcCorners[j];
            worldCorner /= worldCorner.w;
            cascade.corners[j] = worldCorner;

            cascade.centre += glm::vec3(worldCorner);
        }
        cascade.centre /= 8.0;
    }
}

glm::mat4x4 Shadowcaster::CalculateDirectionalVP(const FrustumCorners& cascade, const Light& light, const VulkanBackendSettings& settings)
{
    glm::vec3 dir = glm::normalize(light.direction);
    glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);

    // Fallback up vector if light direction is almost vertical
    if (glm::abs(glm::dot(dir, up)) > 0.999) {
        up = glm::vec3(1.0, 0.0, 0.0);
    }
    glm::mat4x4 rawView = glm::lookAt(cascade.centre, cascade.centre - dir, up);

    // Bounding sphere
    float radius = 0.0;
    for (const auto& v : cascade.corners) {
        radius = glm::max(radius, glm::length(glm::vec3(v) - cascade.centre));
    }
    radius = glm::ceil(radius);

    // Scale on z axis to handle external occluders
    glm::vec3 bounds(radius);
    bounds.z *= 5.0;
    glm::mat4x4 rawProj = Camera::VulkanOrthographic(-bounds.x, bounds.x, -bounds.y, bounds.y, -bounds.z, bounds.z);

    // Texel snapping
    glm::mat4x4 rawVP = rawProj * rawView;
    glm::vec4 shadowOrigin = rawVP * glm::vec4(0.0, 0.0, 0.0, 1.0);
    shadowOrigin *= (static_cast<float>(settings.shadowResolution) / 2.0);

    glm::vec2 roundedOrigin = glm::round(glm::vec2(shadowOrigin));
    glm::vec2 texelOffset = roundedOrigin - glm::vec2(shadowOrigin);
    texelOffset *= (2.0 / static_cast<float>(settings.shadowResolution));

    // Apply texel offset to matrix translation components
    rawProj[3][0] += texelOffset.x;
    rawProj[3][1] += texelOffset.y;

    return rawProj * rawView;
}

glm::mat4x4 Shadowcaster::CalculateSpotVP(const Light& light)
{
    glm::mat4x4 view = glm::lookAt(glm::vec3(light.position) + glm::vec3(light.direction), glm::vec3(light.position), glm::vec3(0.0, 1.0, 0.0));
    glm::mat4x4 proj = Camera::VulkanPerspective(light.outerCone * 2.0, 1.0, 0.01, light.radius);
    return proj * view;
}
