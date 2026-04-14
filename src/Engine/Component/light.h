#pragma once 

#include "glm/common.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_float3.hpp"

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

    // Positions and rotations of lights are pulled in from transform in the light system
    Light() = default;
    Light(Point, glm::vec3 pointColor, float pointIntensity, float pointRadius)
    {
        position = glm::vec4(glm::vec3(0.0), POINT);
        direction = glm::vec4(0.0);

        color = glm::vec4(glm::clamp(pointColor, glm::vec3(0.0), glm::vec3(1.0)), pointIntensity);
        radius = std::max(pointRadius, 0.0f);
        innerCone = 1.0;
        outerCone = 1.0;
    }

    Light(Spot, glm::vec3 spotColor, float spotIntensity, float spotRadius, float innerRadians, float outerRadians) 
    { 
        position = glm::vec4(glm::vec3(0.0), SPOT);
        direction = glm::vec4(0.0);

        color = glm::vec4(glm::clamp(spotColor, glm::vec3(0.0), glm::vec3(1.0)), spotIntensity);
        radius = std::max(spotRadius, 0.0f);
        innerCone = cos(std::max(innerRadians, 0.0f));
        outerCone = cos(std::max(outerRadians, 0.0f));
    }

    Light(Directional, glm::vec3 directionalColor, float directionalIntensity) 
    { 
        position = glm::vec4(glm::vec3(0.0), DIRECTIONAL);
        direction = glm::vec4(0.0);

        color = glm::vec4(glm::clamp(directionalColor, glm::vec3(0.0), glm::vec3(1.0)), directionalIntensity);
        radius = 1.0;
        innerCone = 1.0;
        outerCone = 1.0;
    }
};
