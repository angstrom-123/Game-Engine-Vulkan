#version 450

layout (set = 0, binding = 0) uniform GlobalUniforms {
    mat4 vp;
    vec4 ssaoKernel[64];
} uniforms;

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
    uint screenWidth;
    uint screenHeight;
} constants;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

layout (location = 0) out vec3 vPosition;
layout (location = 1) out vec2 vUV;
layout (location = 2) out mat3 vTBN;

mat3 CalculateTBN()
{
    mat3 normalMatrix = inverse(transpose(mat3(constants.model)));
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(normalMatrix * aNormal);
    // Reorthogonalize
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(T, N) * aTangent.w;
    return mat3(T, B, N);
}

void main()
{
    vPosition = vec3(constants.model * vec4(aPosition, 1.0));
    vUV = aUV;
    vTBN = CalculateTBN();

    // Don't reuse vPosition in this calculation because floating point inprecision 
    // causes z-fighting with the precomputed depth map from a prior pass.
    gl_Position = uniforms.vp * constants.model * vec4(aPosition, 1.0);
}
