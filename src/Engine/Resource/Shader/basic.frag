#version 450 

layout (set = 0, binding = 0) uniform GlobalUniforms {
    mat4 vp;
    vec4 lightPos;
    vec4 viewPos;
} uniforms;

layout (set = 1, binding = 0) uniform sampler2DArray uColorTexArray;
layout (set = 1, binding = 1) uniform sampler2DArray uDataTexArray;

layout (push_constant) uniform Constants {
    mat4 model;
    float specularExponent;
    uint ambientIndex;
    uint diffuseIndex;
    uint normalIndex;
} constants;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec4 outFragColor;

void main()
{
    vec3 normal = texture(uDataTexArray, vec3(vUV, constants.normalIndex)).rgb;
    normal = normal * 2.0 - 1.0;
    normal = normalize(vTBN * normal);

    vec3 lightDir = normalize(uniforms.lightPos.xyz - vPosition);
    vec3 viewDir = normalize(uniforms.viewPos.xyz - vPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    vec4 ambientMap = texture(uColorTexArray, vec3(vUV, constants.ambientIndex));
    vec4 diffuseMap = texture(uColorTexArray, vec3(vUV, constants.diffuseIndex));

    vec3 ambient = vec3(0.2);
    vec3 diffuse = vec3(max(dot(normal, lightDir), 0.0));
    vec3 specular = vec3(pow(max(dot(normal, halfDir), 0.0), constants.specularExponent * 4.0));

    vec3 result = (ambient + specular) * ambientMap.rgb + diffuse * diffuseMap.rgb;

    outFragColor = vec4(result, ambientMap.a); // Alpha channel for A2C taken from ambient texture
}
