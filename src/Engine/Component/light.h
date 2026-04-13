#pragma once 

#include "glm/common.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"

enum Point       { POINT        = 0 };
enum Spot        { SPOT         = 1 };
enum Directional { DIRECTIONAL  = 2 };

struct Light {
    glm::vec4 position;     // w = type (0 = point, 1 = spot, 2 = directional)
    glm::vec4 color;        // rgb + intensity
    glm::vec4 direction;    // spot / directional
    float radius;           // point / spot
    float innerCone;        // spot
    float outerCone;        // spot
    uint32_t shadowIndex;

    // NOTE: All light positions are handled by the lighting system - extracted from transform component

    Light() = default;
    Light(Point, glm::vec3 pointColor, float pointIntensity, float pointRadius)
    {
        position = glm::vec4(glm::vec3(0.0), POINT);
        color = glm::vec4(glm::clamp(pointColor, glm::vec3(0.0), glm::vec3(1.0)), pointIntensity);
        radius = std::max(pointRadius, 0.0f);
        direction = glm::vec4(0.0);
        innerCone = 1.0;
        outerCone = 1.0;
    }

    Light(Spot, glm::vec3 spotColor, float spotIntensity, float spotRadius, glm::vec3 spotDirection, float innerRadians, float outerRadians) 
    { 
        position = glm::vec4(glm::vec3(0.0), SPOT);
        color = glm::vec4(glm::clamp(spotColor, glm::vec3(0.0), glm::vec3(1.0)), spotIntensity);
        radius = std::max(spotRadius, 0.0f);
        direction = glm::vec4(glm::normalize(spotDirection), 0.0);
        innerCone = cos(std::max(innerRadians, 0.0f));
        outerCone = cos(std::max(outerRadians, 0.0f));
    }

    Light(Directional, glm::vec3 directionalColor, float directionalIntensity, glm::vec3 directionalDirection) 
    { 
        position = glm::vec4(glm::vec3(0.0), DIRECTIONAL);
        color = glm::vec4(glm::clamp(directionalColor, glm::vec3(0.0), glm::vec3(1.0)), directionalIntensity);
        radius = 1.0;
        direction = glm::vec4(glm::normalize(directionalDirection), 0.0);
        innerCone = 1.0;
        outerCone = 1.0;
    }
};
