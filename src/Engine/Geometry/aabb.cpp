#include "aabb.h"

CentreExtents::CentreExtents(const AABB& aabb)
{
    centre = (aabb.min + aabb.max) * 0.5f;
    extents = (aabb.max - aabb.min) * 0.5f;
}

CentreExtents CentreExtents::ToWorldSpace(const glm::mat4x4& model)
{
    CentreExtents res;

    // Translation
    glm::vec4 worldCentre = model * glm::vec4(centre, 1.0);
    res.centre = glm::vec3(worldCentre) / worldCentre.w;

    // Rotation + Scale
    glm::mat3 rotationScale = glm::mat3(model);
    res.extents.x = std::abs(rotationScale[0][0]) * extents.x
                  + std::abs(rotationScale[0][1]) * extents.y
                  + std::abs(rotationScale[0][2]) * extents.z;
    res.extents.y = std::abs(rotationScale[1][0]) * extents.x
                  + std::abs(rotationScale[1][1]) * extents.y
                  + std::abs(rotationScale[1][2]) * extents.z;
    res.extents.z = std::abs(rotationScale[2][0]) * extents.x
                  + std::abs(rotationScale[2][1]) * extents.y
                  + std::abs(rotationScale[2][2]) * extents.z;

    return res;
}

void AABB::Update(Vertex& vertex)
{
    min.x = std::min(min.x, vertex.position.x);
    min.y = std::min(min.y, vertex.position.y);
    min.z = std::min(min.z, vertex.position.z);

    max.x = std::max(max.x, vertex.position.x);
    max.y = std::max(max.y, vertex.position.y);
    max.z = std::max(max.z, vertex.position.z);
}

glm::vec3 AABB::Centroid()
{
    return (min + max) * 0.5f;
}
