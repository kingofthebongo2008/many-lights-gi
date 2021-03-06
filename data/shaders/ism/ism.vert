#version 420

layout(location = 0) in vec3 a_vertex;
layout(location = 1) in vec3 a_normal;

out vec3 v_normal;

void main()
{
    vec4 vertex = vec4(a_vertex, 1.0);

    v_normal = a_normal;

    gl_Position = vertex;
}
