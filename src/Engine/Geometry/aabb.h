#pragma once

#include "vertex.h"

struct AABB {
    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);

    void Update(Vertex& vertex);
    glm::vec3 Centroid();
};

struct CentreExtents {
    glm::vec3 centre;
    glm::vec3 extents;

    CentreExtents() {};
    CentreExtents(AABB& aabb);
    CentreExtents WorldSpace(const glm::mat4x4& model);
};

