#version 450

#define MATERIAL_FLAG_NONE 0
#define MATERIAL_FLAG_TRANSPARENT 1
#define MATERIAL_FLAG_DOUBLE_SIDED 2
#define MATERIAL_FLAG_CUTOUT 2

#define TEXTURE_ARRAY_COUNT 5

#define PI 3.14159265359
#define EPSILON 0.00001

#define ALBEDO 0
#define NORMAL 1
#define MATERIAL 2 
#define DEPTH 3

#define POINT_LIGHT 0
#define SPOT_LIGHT 1 
#define DIRECTIONAL_LIGHT 2

#define SHADOW_CASCADE_COUNT 3

struct Light {
    vec4 position;      // w = light type (0: Point, 1: Spot, 2: Directional)
    vec4 color;         // rgb + intensity
    vec4 direction;     // spot / directional
    float radius;       // spot / point
    float innerCone;    // spot
    float outerCone;    // spot
    uint shadowIndex;   // spot / directional
};

struct Cascade {
    mat4 vp;
    uint shadowMapIndex;
    float far;
    float near;
};

struct Shadowcaster {
    Cascade cascades[SHADOW_CASCADE_COUNT];
};

layout (push_constant) uniform Constants {
    layout (offset = 64) vec4 albedo; // w = alpha
    float roughness;
    float metallic;
    float ao;
    float emissive;
    uint diffuseIndex;
    uint diffuseLayer;
    uint normalIndex;
    uint normalLayer;
    uint flags;
} constants;

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    // Camera
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;
    float near;
    float far;
    // Settings
    float exposure;
    float gamma;
    float ambientIntensity;
    uint lightCount;
    ivec2 screenSize;
    ivec2 shadowSize;
    // Light culling
    uint tileSize;
    uint tilesX;
    uint tilesY;
    uint maxLightsPerTile;
} uniforms;

layout (set = 0, binding = 1) readonly buffer LightBuffer { 
    Light lights[]; 
} lightBuffer;
layout (set = 0, binding = 2) readonly buffer ShadowBuffer { 
    Shadowcaster shadowcasters[]; 
} shadowBuffer;

layout (set = 1, binding = 0) uniform sampler2DArray textures[TEXTURE_ARRAY_COUNT];
layout (set = 1, binding = 1) uniform sampler2DArrayShadow shadowTextures;
layout (set = 1, binding = 2) uniform sampler2DArray shadowTexturesRaw;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec4 outColor;

float Attenuate(float dist, float radius)
{
    float d = dist / radius;
    float att = max(0.0, 1.0 - d * d);
    return att * att;
}

float SpotFactor(vec3 lightDir, vec3 spotDir, float inner, float outer)
{
    float cosTheta = dot(lightDir, spotDir);
    float smoothed = clamp((cosTheta - outer) / (inner - outer), 0.0, 1.0);
    return smoothed;
}

float DistributionGGX(float nh, float roughness) 
{
    float a = roughness * roughness;
    float a2 = a * a;
    float den = (nh * nh * (a2 - 1.0) + 1.0);
    return a2 / (PI * den * den);
}

float GeometrySchlickGGX(float nv, float roughness) 
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nv / (nv * (1.0 - k) + k);
}

float GeometrySmith(float nv, float nl, float roughness) 
{
    return GeometrySchlickGGX(nv, roughness) * GeometrySchlickGGX(nl, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) 
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float ComputeShadow(Light light, vec3 worldPos, vec3 normal, vec3 lightDir)
{
    // In case of non-shadowcasting lights
    if (light.shadowIndex == 0xFFFFFFFF) return 0.0;

    Shadowcaster shadowcaster = shadowBuffer.shadowcasters[light.shadowIndex];

    // Select cascade
    Cascade cascade0 = shadowcaster.cascades[0];
    Cascade cascade1 = cascade0;
    float bias0 = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    float bias1 = bias0;
    float cascadeBlend = 0.0;
    if (uint(light.position.w) == DIRECTIONAL_LIGHT) {
        vec4 positionViewspace = uniforms.view * vec4(worldPos, 1.0);
        float depth = -positionViewspace.z;
        int layerIndex = -1;
        while (++layerIndex < SHADOW_CASCADE_COUNT) {
            Cascade currCascade = shadowcaster.cascades[layerIndex];
            if (depth < currCascade.far) {
                cascade0 = currCascade;
                bias0 *= 1.0 / (currCascade.far * 0.5);

                if (layerIndex + 1 < SHADOW_CASCADE_COUNT) {
                    Cascade nextCascade = shadowcaster.cascades[layerIndex + 1];
                    if (depth > nextCascade.near) {
                        cascade1 = nextCascade;
                        bias1 *= 1.0 / (nextCascade.far * 0.5);
                        cascadeBlend = clamp((depth - nextCascade.near) / (currCascade.far - nextCascade.near), 0.0, 1.0);
                    }
                }
                break;
            }
        }

        // Early exit if out of range
        if (layerIndex == SHADOW_CASCADE_COUNT) {
            return 0.0;
        }
    }

    // Depth reconstruction and sampling
    vec2 texelSize = 1.0 / vec2(uniforms.shadowSize);
    float shadow = 0.0;

    if (cascadeBlend > 0.0) {
        vec4 positionLightspace0 = cascade0.vp * vec4(worldPos, 1.0);
        vec3 projectionCoords0 = positionLightspace0.xyz / positionLightspace0.w;
        projectionCoords0.xy = projectionCoords0.xy * 0.5 + 0.5;

        vec4 positionLightspace1 = cascade1.vp * vec4(worldPos, 1.0);
        vec3 projectionCoords1 = positionLightspace1.xyz / positionLightspace1.w;
        projectionCoords1.xy = projectionCoords1.xy * 0.5 + 0.5;

        for (int x = -2; x <= 2; x++) {
            for (int y = -2; y <= 2; y++) {
                vec4 shadowCoord0 = vec4(projectionCoords0.xy + texelSize * vec2(x, y), cascade0.shadowMapIndex, projectionCoords0.z - bias0);
                float shadow0 = texture(shadowTextures, shadowCoord0);

                vec4 shadowCoord1 = vec4(projectionCoords1.xy + texelSize * vec2(x, y), cascade1.shadowMapIndex, projectionCoords1.z - bias1);
                float shadow1 = texture(shadowTextures, shadowCoord1);

                shadow += mix(shadow0, shadow1, cascadeBlend);
            }
        }
        shadow /= 25.0;
    } else {
        vec4 positionLightspace0 = cascade0.vp * vec4(worldPos, 1.0);
        vec3 projectionCoords0 = positionLightspace0.xyz / positionLightspace0.w;
        projectionCoords0.xy = projectionCoords0.xy * 0.5 + 0.5;

        for (int x = -2; x <= 2; x++) {
            for (int y = -2; y <= 2; y++) {
                vec4 shadowCoord = vec4(projectionCoords0.xy + texelSize * vec2(x, y), cascade0.shadowMapIndex, projectionCoords0.z - bias0);
                shadow += texture(shadowTextures, shadowCoord);
            }
        }
        shadow /= 25.0;
    }

    return shadow;
}

vec3 ComputePBR(Light light, vec3 worldPos, vec3 viewDir, vec3 normal, vec3 albedo, float metallic, float roughness, vec3 F0, float nv)
{
    uint lightKind = uint(light.position.w);
    vec3 lightDir;
    vec3 radiance = light.color.rgb * light.color.a;
    float attenuation = 1.0;
    float spotFactor = 1.0;
    
    if (lightKind == DIRECTIONAL_LIGHT) {
        lightDir = normalize(light.direction.xyz);
        radiance *= (1.0 - ComputeShadow(light, worldPos, normal, lightDir));
    } else if (lightKind == POINT_LIGHT) {
        vec3 delta = light.position.xyz - worldPos;
        float dist = length(delta);
        lightDir = delta / dist;
        attenuation = Attenuate(dist, light.radius);
    } else if (lightKind == SPOT_LIGHT) {
        vec3 delta = light.position.xyz - worldPos;
        float dist = length(delta);
        lightDir = delta / dist;
        attenuation = Attenuate(dist, light.radius);
        spotFactor = SpotFactor(lightDir, light.direction.xyz, light.innerCone, light.outerCone);
        radiance *= (1.0 - ComputeShadow(light, worldPos, normal, lightDir));
    }
    
    if (attenuation <= EPSILON || spotFactor <= EPSILON) {
        return vec3(0.0);
    }
    
    float nl = max(dot(normal, lightDir), 0.0);
    if (nl <= 0.0) return vec3(0.0);
    
    vec3 halfway = normalize(viewDir + lightDir);
    float NDF = DistributionGGX(max(dot(normal, halfway), 0.0), roughness);
    float G = GeometrySmith(max(dot(normal, viewDir), 0.0), nl, roughness);
    vec3 F = FresnelSchlick(max(dot(halfway, viewDir), 0.0), F0);
    
    vec3 num = NDF * G * F;
    float den = 4.0 * nv * nl + EPSILON;
    vec3 specular = num / den;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    return (kD * albedo / PI + specular) * radiance * nl * attenuation * spotFactor;
}

vec4 ComputeAlbedo()
{
    vec4 albedo = texture(textures[constants.diffuseIndex], vec3(vUV, constants.diffuseLayer));
    albedo.xyz *= constants.albedo.xyz;
    return albedo;
}

vec3 ComputeNormal()
{
    // TODO: Can go back to this if I fixup normal mipmapping
    // vec3 normal = texture(textures[constants.normalIndex], vec3(vUV, constants.normalLayer)).xyz;

    // Goofy manual lod selection to fix some visible triangle seams
    float dist = length(vPosition - uniforms.position.xyz);
    float lod = log2(dist * 0.8); // higher scaling factor = higher lod
    lod = clamp(lod, 0.0, textureQueryLevels(textures[constants.normalIndex]) - 1);

    vec3 normal = textureLod(textures[constants.normalIndex], vec3(vUV, constants.normalLayer), lod).xyz;

    normal = normal * 2.0 - 1.0;
    normal = normalize(vTBN * normal);
    return normal * 0.5 + 0.5;
}

void main()
{
    // Special case for rendering world-space text
    if (constants.diffuseIndex == TEXTURE_ARRAY_COUNT - 1) {
        float alpha = texture(textures[constants.diffuseIndex], vec3(vUV, constants.diffuseLayer)).r;
        outColor = vec4(constants.albedo.rgb, alpha * constants.albedo.a);
        return;
    }

    vec4 albedo = ComputeAlbedo();

    vec3 normal = ComputeNormal();
    if ((constants.flags & MATERIAL_FLAG_DOUBLE_SIDED) == MATERIAL_FLAG_DOUBLE_SIDED && !gl_FrontFacing) {
        normal = -normal;
    }

    vec3 viewDir = normalize(uniforms.position.xyz - vPosition);

    vec3 F0 = mix(vec3(0.04), albedo.xyz, constants.metallic);
    float nv = max(dot(normal, viewDir), 0.0);
    vec3 accumulatedColor = vec3(0.0);
    for (uint i = 0; i < uniforms.lightCount; i++) {
        Light light = lightBuffer.lights[i];
        accumulatedColor += ComputePBR(light, vPosition, viewDir, normal, albedo.xyz, constants.metallic, constants.roughness, F0, nv);
    }

    // TODO: Add IBL (image based lighting)
    vec3 ambientColor = albedo.xyz * uniforms.ambientIntensity * constants.ao;

    // TODO: Add emissive to materials
    vec3 emissiveColor = albedo.xyz * constants.emissive;

    // Result 
    vec3 finalColor = accumulatedColor + ambientColor + emissiveColor;
    outColor = vec4(finalColor, albedo.a);
}
