#pragma once

#include "Geometry/vertex.h"
#include "Geometry/aabb.h"
#include "Util/allocator.h"

struct Mesh {
    Vertex *vertices;
    int32_t vertexCount;
    AllocatedBuffer vertexBuffer;
    AABB bounds;
    bool allocated;
};
