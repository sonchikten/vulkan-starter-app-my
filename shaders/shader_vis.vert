#version 450 core

layout(location = 0) in vec3 in_position;

layout(std140, set = 0, binding = 0) uniform VisUniforms {
    mat4 view_projection;
} vis_uniforms;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) out vec4 out_color;

void main() {
    gl_Position = vis_uniforms.view_projection * pc.model * vec4(in_position, 1.0);
    out_color = pc.color;
}
