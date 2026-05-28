#pragma once

#include "Geometry/aabb.h"
#include "glm/ext/matrix_float4x4.hpp"

typedef glm::vec4 Plane;

struct Frustum {
    Plane planes[6];

    Frustum(const glm::mat4x4& vpMatrix);
    bool Intersects(const CentreExtents& aabb) const;

    const Plane *begin() const { return planes; }
    const Plane *end() const { return planes + 6; }
};
