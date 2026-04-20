#version 450 

struct Light {
    vec4 position;      // xyz + light type  - all                 (0: Point, 1: Spot, 2: Directional)
    vec4 color;         // rgb + intensity   - all
    vec4 direction;     // xyz + shadow bias - spot / directional
    float radius;       //                   - all
    float innerCone;    //                   - spot
    float outerCone;    //                   - spot
    uint shadowIndex;   //                   - spot / directional
    mat4 lightVP;       //                   - spot / directional
};

layout (set = 0, binding = 0) uniform GlobalUniforms { mat4 vp; } uniforms;
layout (set = 1, binding = 0) uniform sampler2DArray uTextures[4];
layout (set = 1, binding = 1) uniform sampler2DArray uShadowmaps;
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
    // float cosTheta = dot(lightDir, -spotDir); // lightDir from fragment to light
    float cosTheta = dot(lightDir, spotDir); // lightDir from fragment to light
    float smoothed = clamp((cosTheta - outer) / (inner - outer), 0.0, 1.0);
    return smoothed;
}

// For spot / directional lights
float CalculateShadow(Light light, vec3 normal, vec3 lightDir)
{
    if (light.shadowIndex == 0xFFFFFFFF) return 0.0;

    float bias = light.direction.w;
    bias = max(bias * 10.0 * (1.0 - dot(normal, lightDir)), bias);
    vec4 positionLightspace = light.lightVP * vec4(vPosition, 1.0);
    vec3 projectionCoords = positionLightspace.xyz / positionLightspace.w;
    float currentDepth = projectionCoords.z;
    projectionCoords = projectionCoords * 0.5 + 0.5;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowmaps, 0));
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 coordOffset = texelSize * vec2(x, y);
            float pcfDepth = texture(uShadowmaps, vec3(projectionCoords.xy + coordOffset, light.shadowIndex)).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow;

    // float sampledDepth = texture(uShadowmaps, vec3(projectionCoords.xy, light.shadowIndex)).r;
    // return (currentDepth - bias) > sampledDepth ? 1.0 : 0.0;
}

void main()
{
    // Indices packed. 16 high bits = array index, 16 low bits = texture index
    vec4 ambientMap = texture(uTextures[constants.ambientIndex >> 16], vec3(vUV, constants.ambientIndex & 0xFFFF));
    vec4 diffuseMap = texture(uTextures[constants.diffuseIndex >> 16], vec3(vUV, constants.diffuseIndex & 0xFFFF));
    vec4 normalMap = texture(uTextures[constants.normalIndex >> 16], vec3(vUV, constants.normalIndex & 0xFFFF));

    vec3 normal = normalMap.rgb * 2.0 - 1.0;
    normal = normalize(vTBN * normal);

    // Retrieve tile data
    uint tileX = uint(gl_FragCoord.x) / constants.tileSize;
    uint tileY = uint(gl_FragCoord.y) / constants.tileSize;
    uint tileIndex = tileY * constants.tilesX + tileX;

    uint offset = constants.maxTileLights * tileIndex;
    uint count = tileCounts.counts[tileIndex];

    vec3 diffuseTotal = vec3(0.0);
    vec3 specularTotal = vec3(0.0);
    for (uint i = 0; i < count; i++) {
        uint lightIndex = tileIndices.indices[offset + i];
        Light light = lightBuffer.lights[lightIndex];
        uint type = uint(light.position.w);

        vec3 lightDir;
        float attenuation = 1.0;
        float spot = 1.0;
        float shadow = 0.0;

        if (type == 2) {
            lightDir = light.direction.xyz;
            shadow = CalculateShadow(light, normal, lightDir);
        } else {
            vec3 delta = light.position.xyz - vPosition;
            float dist = length(delta);
            lightDir = delta / dist;
            attenuation = Attenuate(dist, light.radius);
            if (type == 1) {
                spot = SpotFactor(lightDir, light.direction.xyz, light.innerCone, light.outerCone);
                shadow = CalculateShadow(light, normal, lightDir);
            }
        }

        float contributionFactor = spot * attenuation * light.color.a * (1.0 - shadow);

        // Diffuse
        float ndl = max(dot(normal, lightDir), 0.0);
        vec3 diffuseContribution = ndl * light.color.rgb * contributionFactor;

        // Specular
        vec3 viewDir = normalize(camera.position - vPosition);
        vec3 halfDir = normalize(lightDir + viewDir);
        float ndh = max(dot(normal, halfDir), 0.0);
        float spec = pow(ndh, constants.specularExponent * 4.0);
        vec3 specularContribution = spec * light.color.rgb * contributionFactor;

        diffuseTotal += diffuseContribution;
        specularTotal += specularContribution;
    }

    vec3 ambient = vec3(0.2) * ambientMap.rgb;
    vec3 diffuse = diffuseTotal * diffuseMap.rgb;
    vec3 specular = specularTotal * ambientMap.rgb;
    vec3 result = ambient + diffuse + specular;

    outFragColor = vec4(result, ambientMap.a);
}
