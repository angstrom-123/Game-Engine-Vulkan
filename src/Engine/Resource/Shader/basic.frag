#version 450 

layout (binding = 1) uniform FragmentUniforms {
    vec4 placeholder;
} uniforms;

layout (binding = 2) uniform sampler2D tex;

layout (location = 0) in vec2 vUV;

layout (location = 0) out vec4 outFragColor;

void main()
{
    vec3 color = texture(tex, vUV).xyz;
    outFragColor = vec4(color, 1.0);
}
