#version 450

layout (set = 0, binding = 0) uniform GlobalUniforms {
    mat4 vp;
    vec4 lightPos;
    vec4 viewPos;
} uniforms;

layout (push_constant) uniform Constants {
    mat4 model;
    float specularExponent;
    uint ambientIndex;
    uint diffuseIndex;
    uint normalIndex;
} constants;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

layout (location = 0) out vec3 vPosition;
layout (location = 1) out vec2 vUV;
layout (location = 2) out mat3 vTBN;

void main()
{
    gl_Position = uniforms.vp * constants.model * vec4(aPosition, 1.0);

    vPosition = vec3(constants.model * vec4(aPosition, 1.0));
    vUV = aUV;

    mat3 normalMatrix = inverse(transpose(mat3(constants.model)));
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    vec3 N = normalize(normalMatrix * aNormal);
    
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    vTBN = mat3(T, B, N);
}
