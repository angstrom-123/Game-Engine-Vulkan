#version 450

layout (location = 0) in vec3 vPos;
layout (location = 0) out vec4 frag_col;

void main()
{
    // frag_col = vec4(1.0, 0.0, 0.0, 1.0);
    frag_col = vec4(vPos, 1.0);
}
