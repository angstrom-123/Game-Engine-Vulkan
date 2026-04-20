#include "light.h"

Light::Light(Point, glm::vec3 color, float intensity, float radius)
{
    position = glm::vec4(glm::vec3(0.0), POINT);
    direction = glm::vec4(0.0);

    this->color = glm::vec4(glm::clamp(color, glm::vec3(0.0), glm::vec3(1.0)), intensity);
    this->radius = std::max(radius, 0.0f);
    innerCone = 1.0;
    outerCone = 1.0;
    shadowIndex = UINT32_MAX;
}

Light::Light(Spot, glm::vec3 color, float intensity, float radius, float innerCone, float outerCone) 
{ 
    position = glm::vec4(glm::vec3(0.0), SPOT);
    direction = glm::vec4(0.0);

    this->color = glm::vec4(glm::clamp(color, glm::vec3(0.0), glm::vec3(1.0)), intensity);
    this->radius = std::max(radius, 0.0f);
    this->innerCone = cos(std::max(innerCone, 0.0f));
    this->outerCone = cos(std::max(outerCone, 0.0f));
    shadowIndex = UINT32_MAX;
}

Light::Light(Directional, glm::vec3 color, float intensity) 
{ 
    position = glm::vec4(glm::vec3(0.0), DIRECTIONAL);
    direction = glm::vec4(0.0);

    this->color = glm::vec4(glm::clamp(color, glm::vec3(0.0), glm::vec3(1.0)), intensity);
    radius = 1.0;
    innerCone = 1.0;
    outerCone = 1.0;
    shadowIndex = UINT32_MAX;
}
