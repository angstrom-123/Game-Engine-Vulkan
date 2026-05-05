#include "frustum.h"

Frustum::Frustum(const glm::mat4x4& vp)
{
    // Calculate
    for (int i = 0; i < 4; i++) planes[0][i] = vp[i][3] + vp[i][0]; // left 
    for (int i = 0; i < 4; i++) planes[1][i] = vp[i][3] - vp[i][0]; // right
    for (int i = 0; i < 4; i++) planes[2][i] = vp[i][3] + vp[i][1]; // bottom
    for (int i = 0; i < 4; i++) planes[3][i] = vp[i][3] - vp[i][1]; // top
    for (int i = 0; i < 4; i++) planes[4][i] = vp[i][3] + vp[i][2]; // near
    for (int i = 0; i < 4; i++) planes[5][i] = vp[i][3] - vp[i][2]; // far

    // Normalize
    for (Plane& plane : planes) {
        float magnitude = glm::length(glm::vec3(plane));
        if (magnitude > 0.0001) plane /= magnitude;
    }
}

bool Frustum::Intersects(const CentreExtents& aabb) const
{
    for (const Plane& plane : planes) {
        float radius = aabb.extents.x * std::abs(plane.x) 
                     + aabb.extents.y * std::abs(plane.y) 
                     + aabb.extents.z * std::abs(plane.z);

        // Distance from centre to plane
        float distance = glm::dot(plane, glm::vec4(aabb.centre, 1.0));

        // Centre outside radius, therefore outside frustum
        if (distance + radius < 0) return false;
    }

    // Passed all checks: intersection!
    return true;
}
