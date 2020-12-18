#version 450

layout(location=0) out vec2 v_Texcoord;

void main()
{
    v_Texcoord  = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    gl_Position = vec4(v_Texcoord * 2.0 - 1.0, 0.0, 1.0);
}
