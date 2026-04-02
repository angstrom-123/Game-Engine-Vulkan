#version 450 

layout (std140, binding = 1) uniform FragmentUniforms {
    vec4 lightPos;
    vec4 viewPos;
} uniforms;

layout (binding = 2) uniform sampler2D tex;

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUV;

layout (location = 0) out vec4 outFragColor;

void main()
{
    vec3 lightDir = normalize(uniforms.lightPos.xyz - vPosition);
    vec3 viewDir = normalize(uniforms.viewPos.xyz - vPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    float ambient = 0.3;
    float diffuse = max(dot(vNormal, lightDir), 0.0);
    float specular = pow(max(dot(vNormal, halfDir), 0.0), 16.0);

    vec3 color = texture(tex, vUV).xyz * min(diffuse + specular + ambient, 1.0);

    outFragColor = vec4(color, 1.0);
}
