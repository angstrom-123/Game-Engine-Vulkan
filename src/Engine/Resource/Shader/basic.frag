#version 450 

struct Light {
    vec4 position;      // w = light type (0: Point, 1: Spot, 2: Directional)
    vec4 color;         // rgb + intensity
    vec4 direction;     // spot / directional
    float radius;       // point / spot
    float innerCone;    // spot
    float outerCone;    // spot
    uint shadowIndex;
};

layout (set = 0, binding = 0) uniform GlobalUniforms { mat4 vp; } uniforms;
layout (set = 1, binding = 0) uniform sampler2DArray uColorTexArray;
layout (set = 1, binding = 1) uniform sampler2DArray uDataTexArray;
layout (set = 2, binding = 0) uniform Camera {
    mat4 view;
    mat4 projection;
    vec3 position;
} camera;
layout (set = 2, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;
layout (set = 2, binding = 2) readonly buffer TileIndices { uint indices[]; } tileIndices;
layout (set = 2, binding = 3) readonly buffer TileCounts { uint counts[]; } tileCounts;

layout (push_constant) uniform Constants {
    mat4 model;
    float specularExponent;
    uint ambientIndex;
    uint diffuseIndex;
    uint normalIndex;
    uint tileSize;
    uint tilesX;
    uint tilesY;
    uint maxTileLights;
} constants;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec4 outFragColor;

// For point / spot lights
float Attenuate(float dist, float radius)
{
    float d = dist / radius;
    float att = max(0.0, 1.0 - d * d);
    return att * att;
}

// For spot lights
float SpotFactor(vec3 lightDir, vec3 spotDir, float inner, float outer)
{
    float cosTheta = dot(lightDir, -spotDir); // lightDir from fragment to light
    float smoothed = clamp((cosTheta - outer) / (inner - outer), 0.0, 1.0);
    return smoothed;
}

void main()
{
    vec3 normal = texture(uDataTexArray, vec3(vUV, constants.normalIndex)).rgb;
    normal = normal * 2.0 - 1.0;
    normal = normalize(vTBN * normal);

    vec4 ambientMap = texture(uColorTexArray, vec3(vUV, constants.ambientIndex));
    vec4 diffuseMap = texture(uColorTexArray, vec3(vUV, constants.diffuseIndex));

    // Retrieve tile data
    uint tileX = uint(gl_FragCoord.x) / constants.tileSize;
    uint tileY = uint(gl_FragCoord.y) / constants.tileSize;
    uint tileIndex = tileY * constants.tilesX + tileX;

    uint offset = constants.maxTileLights * tileIndex;
    uint count = tileCounts.counts[tileIndex];

    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    for (uint i = 0; i < count; i++) {
        uint lightIndex = tileIndices.indices[offset + i];
        Light light = lightBuffer.lights[lightIndex];
        uint type = uint(light.position.w);

        vec3 lightDir;
        float attenuation = 1.0;
        float spot = 1.0;

        if (type == 2) {
            lightDir = -light.direction.xyz;
        } else {
            vec3 delta = light.position.xyz - vPosition;
            float dist = length(delta);
            lightDir = delta / dist;
            attenuation = Attenuate(dist, light.radius);
            if (type == 1) {
                spot = SpotFactor(lightDir, light.direction.xyz, light.innerCone, light.outerCone);
            }
        }

        float contributionFactor = spot * attenuation * light.color.a;

        // Diffuse
        float ndl = max(dot(normal, lightDir), 0.0);
        vec3 diffuseContribution = ndl * light.color.rgb * contributionFactor;

        // Specular
        vec3 viewDir = normalize(camera.position - vPosition);
        vec3 halfDir = normalize(lightDir + viewDir);
        float ndh = max(dot(normal, halfDir), 0.0);
        float spec = pow(ndh, constants.specularExponent * 4.0);
        vec3 specularContribution = spec * light.color.rgb * contributionFactor;

        diffuse += diffuseContribution;
        specular += specularContribution;
    }

    vec3 ambient = vec3(0.2);
    vec3 result = (ambient * ambientMap.rgb) + (diffuse * diffuseMap.rgb) + (specular * ambientMap.rgb);

    outFragColor = vec4(result, ambientMap.a);
}
