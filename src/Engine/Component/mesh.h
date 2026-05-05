#pragma once

#include "Geometry/aabb.h"
#include "Util/allocator.h"

struct Mesh {
    int32_t vertexCount;
    AllocatedBuffer vertexBuffer;
    AABB bounds;
    bool allocated;
};
