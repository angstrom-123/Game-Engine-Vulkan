#version 450 

#define SSAO_SAMPLES 8

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

layout (set = 0, binding = 0) uniform GlobalUniforms { 
    mat4 vp; 
    vec4 ssaoKernel[SSAO_SAMPLES];
} uniforms;
layout (set = 1, binding = 0) uniform sampler2DArray uTextures[4];
layout (set = 1, binding = 1) uniform sampler2DArrayShadow uShadowmaps;
layout (set = 1, binding = 2) uniform sampler2D uNoiseTexture;
layout (set = 2, binding = 0) uniform Camera {
    mat4 view;
    mat4 projection;
    mat4 inverseProjection;
    vec3 position;
} camera;
layout (set = 2, binding = 1) readonly buffer LightBuffer { Light lights[]; } lightBuffer;
layout (set = 2, binding = 2) readonly buffer TileIndices { uint indices[]; } tileIndices;
layout (set = 2, binding = 3) readonly buffer TileCounts { uint counts[]; } tileCounts;
layout (set = 2, binding = 4) uniform sampler2D uDepthTexture;

layout (push_constant) uniform Constants {
    mat4 model;
    vec3 baseColor;
    float specularExponent;
    uint ambientIndex;
    uint diffuseIndex;
    uint normalIndex;
    uint tileSize;
    uint tilesX;
    uint tilesY;
    uint maxTileLights;
    uint screenWidth;
    uint screenHeight;
    uint materialFlags;
    float shadowTexelSize;
} constants;

#define MATERIAL_FLAG_NONE 0 
#define MATERIAL_FLAG_TRANSPARENT 1

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec4 outFragColor;

vec2 screenResolution = vec2(constants.screenWidth, constants.screenHeight);
vec2 noiseScale = screenResolution / 8.0;
vec2 uv = gl_FragCoord.xy / screenResolution;

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
    // In case of non-shadowcasting lights
    if (light.shadowIndex == 0xFFFFFFFF) return 0.0;

    // Depth reconstruction and sampling
    float bias = light.direction.w;
    bias = max(bias * 10.0 * (1.0 - dot(normal, lightDir)), bias);
    vec4 positionLightspace = light.lightVP * vec4(vPosition, 1.0);
    vec3 projectionCoords = positionLightspace.xyz / positionLightspace.w;
    float currentDepth = projectionCoords.z;
    projectionCoords = projectionCoords * 0.5 + 0.5;

    // PCF
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec4 shadowCoord = vec4(projectionCoords.xy + constants.shadowTexelSize * vec2(x, y), light.shadowIndex, currentDepth - bias);
            shadow += texture(uShadowmaps, shadowCoord);
        }
    }
    shadow /= 9.0;

    return shadow;
}

vec3 ReconstructViewPosition(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.inverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

float SSAO(vec3 viewPos, vec3 normal)
{
    // No occlusion for transparent materials
    if ((constants.materialFlags & MATERIAL_FLAG_TRANSPARENT) == MATERIAL_FLAG_TRANSPARENT) {
        return 1.0;
    }

    const float bias = 0.025;
    const float radius = 1.0;

    vec3 randomVec = texture(uNoiseTexture, uv * noiseScale).xyz;
    vec3 viewNormal = mat3(camera.view) * normal;

    vec3 tangent = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
    vec3 bitangent = cross(viewNormal, tangent);
    mat3 TBN = mat3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    for (uint i = 0; i < SSAO_SAMPLES; i++) {
        vec3 samplePos = TBN * uniforms.ssaoKernel[i].xyz;
        samplePos = viewPos + samplePos * radius;

        vec4 offset = camera.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepth = texture(uDepthTexture, offset.xy).r;
        vec3 sampleViewPos = ReconstructViewPosition(offset.xy, sampleDepth);
        float sampleZ = sampleViewPos.z;

        float rangeCheck = abs(viewPos.z - sampleZ) < radius ? 1.0 : 0.0;

        occlusion += (sampleZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(SSAO_SAMPLES));
    return occlusion;
}

void main()
{
    // Indices packed. 16 high bits = array index, 16 low bits = texture index
    vec4 ambientMap = texture(uTextures[constants.ambientIndex >> 16], vec3(vUV, constants.ambientIndex & 0xFFFF));
    vec4 diffuseMap = texture(uTextures[constants.diffuseIndex >> 16], vec3(vUV, constants.diffuseIndex & 0xFFFF));
    vec4 normalMap = texture(uTextures[constants.normalIndex >> 16], vec3(vUV, constants.normalIndex & 0xFFFF));

    vec3 normal = normalMap.xyz * 2.0 - 1.0;
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

        vec3 lightDir = light.direction.xyz;
        float attenuation = 1.0;
        if (type != 2) {
            vec3 delta = light.position.xyz - vPosition;
            float dist = length(delta);
            lightDir = delta / dist;
            attenuation = Attenuate(dist, light.radius);
        }

        float spot = 1.0;
        if (type == 1) {
            spot = SpotFactor(lightDir, light.direction.xyz, light.innerCone, light.outerCone);
        }

        // Skip this light before the more expensive calculations if it won't affect the pixel much 
        if (attenuation * spot < 0.001) {
            continue;
        }

        float shadow = 0.0;
        if (type != 0 && light.shadowIndex != 0xFFFFFFFF) {
            shadow = CalculateShadow(light, normal, lightDir);
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

    float depth = texture(uDepthTexture, uv).r;
    float occlusion = SSAO(ReconstructViewPosition(uv, depth), normal);

    vec3 ambient = vec3(0.3) * ambientMap.rgb * occlusion;
    vec3 diffuse = diffuseTotal * diffuseMap.rgb;
    vec3 specular = specularTotal * ambientMap.rgb;
    vec3 result = (ambient + diffuse + specular) * constants.baseColor;

    outFragColor = vec4(result, ambientMap.a);
}
