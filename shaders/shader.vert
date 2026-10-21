#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_normal;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float pad0;
    vec3 directional_light_direction;
    float pad1;
    vec3 directional_light_color;
    float pad2;
    vec3 ambient_light;
    float pad3;
    uint point_light_count;
    uint spot_light_count;
    float pad4;
    float pad5;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniform {
    mat4 model;
    mat4 normal_matrix;
    vec3 albedo;
    float alpha;
    vec3 specular;
    float pad;
} model_uniforms;

void main() {
    vec4 world_pos = model_uniforms.model * vec4(in_position, 1.0);
    out_world_position = world_pos.xyz;

    out_normal = mat3(model_uniforms.normal_matrix) * in_normal;

    //gl_Position — это встроенная переменная. GPU берет её и делает перспективное деление, растеризацию и т.д.
    gl_Position = scene_uniforms.projection
                * scene_uniforms.view
                * world_pos;
}
