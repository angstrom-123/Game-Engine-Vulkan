#pragma once 

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_float3.hpp"

enum Point       { POINT        = 0 };
enum Spot        { SPOT         = 1 };
enum Directional { DIRECTIONAL  = 2 };

struct LightCreateInfo {
    glm::vec3 position;             // All
    glm::vec3 color;                // All
    glm::vec3 direction;            // Spot / Directional
    float intensity;                // All
    float radius;                   // Spot / Point
    float innerConeRadians;         // Spot
    float outerConeRadians;         // Spot
    float distance;                 // Directional
    bool shadowcaster;              // Shadow - Spot / Directional
    float shadowBias;               // Shadow - Spot / Directional
    float projectionLeft;           // Shadow - Directional
    float projectionRight;          // Shadow - Directional
    float projectionBottom;         // Shadow - Directional
    float projectionTop;            // Shadow - Directional
};

// NOTE: Light struct contains a lot of redundant data (position, direction, shadowindex) because 
//       this structure is passed directly to the gpu in a storage buffer.
//       There are systems such as LightSystem and ShadowSystem that update these redundant values 
//       automatically (e.g., calculating position and direction from transform component)
//       This lets the following light structure be passed directly to the gpu without any tampering.
//
//       The extra weight of these redundant fields is negligible because the light is small and 
//       there is a hard limit on how many can be present at once.
struct Light {
    glm::vec4 position;     // w = type (0 = point, 1 = spot, 2 = directional)
    glm::vec4 color;        // rgb + intensity
    glm::vec4 direction;    // w = shadowBias   (spot / directional)
    float radius;           //                  (point / spot)
    float innerCone;        //                  (spot)
    float outerCone;        //                  (spot)
    uint32_t shadowIndex;   //                  (spot / directional);
    glm::mat4x4 lightVP;    //                  (spot / directional)

    Light() = default;
    Light(Point, glm::vec3 color, float intensity, float radius);
    Light(Spot, glm::vec3 color, float intensity, float radius, float innerCone, float outerCone);
    Light(Directional, glm::vec3 color, float intensity);
};
