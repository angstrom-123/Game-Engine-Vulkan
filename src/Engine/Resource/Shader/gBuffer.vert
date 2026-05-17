#version 450

layout (push_constant) uniform Constants {
    mat4 model;
} constants;

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    mat4 view;
    mat4 proj;
} uniforms;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

layout (location = 0) out vec3 vPosition;
layout (location = 1) out vec2 vUV;
layout (location = 2) out mat3 vTBN;

mat3 CalculateTBN()
{
    mat3 modelMatrix = mat3(constants.model);
    vec3 T = normalize(modelMatrix * aTangent.xyz);
    vec3 N = normalize(modelMatrix * aNormal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * aTangent.w;
    return mat3(T, B, N);
}

void main()
{
    gl_Position = uniforms.proj * uniforms.view * constants.model * vec4(aPosition, 1.0);

    vPosition = vec3(constants.model * vec4(aPosition, 1.0));
    vUV = aUV;
    vTBN = CalculateTBN();
}
