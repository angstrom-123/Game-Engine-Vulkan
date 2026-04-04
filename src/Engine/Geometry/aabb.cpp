#include "aabb.h"

void AABB::Update(Vertex& vertex)
{
    min = glm::min(min, vertex.position);
    max = glm::max(max, vertex.position);
}

glm::vec3 AABB::Centroid()
{
    return (min + max) * 0.5f;
}
