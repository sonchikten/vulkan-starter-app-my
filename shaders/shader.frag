#version 450 core

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

struct PointLight {
    vec3 position;
    float intensity;
    vec3 color;
    float pad1;
};

struct SpotLight {
    vec3 position;
    float intensity;
    vec3 color;
    float pad1;
    vec3 direction;
    float inner_angle;
    float outer_angle;
    float pad[3];
};

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

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer Spotlights {
    SpotLight spot_lights[];
};

layout(std140, set = 1, binding = 0) uniform ModelUniform {
    mat4 model;
    mat4 normal_matrix;
    vec3 albedo;
    float alpha;
    vec3 specular;
    float pad0;
    vec4 uv_transform;
} model_uniforms;

layout(set = 1, binding = 1) uniform sampler2D albedo_texture;
layout(set = 1, binding = 2) uniform sampler2D specular_texture;
layout(set = 1, binding = 3) uniform sampler2D emissive_texture;

layout(push_constant) uniform PipelineConsts {
    bool fancy_sampling;
    float time;
} pc;

vec3 blinn_phong(vec3 N, vec3 L, vec3 V, vec3 light_color, vec3 base_color, vec3 spec_mask) {
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = base_color * diff * light_color;

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), model_uniforms.alpha);
    vec3 specular = spec_mask * spec * light_color;

    return diffuse + specular;
}

void main() {
    vec3 N = normalize(in_normal);
    vec3 V = normalize(scene_uniforms.camera_position - in_world_position);

    vec3 base_color;
    if (pc.fancy_sampling) {
        vec2 uv1 = in_uv + vec2(pc.time * 0.1, 0.0); //сдвиг вправо по U, пропорциональный времени
        vec2 uv2 = in_uv * 2.0 - vec2(0.0, pc.time * 0.2); // *2 масштаб и сдвиг вверх по V
        vec2 uv3 = vec2(fract(in_uv.x + sin(pc.time) * 0.1), in_uv.y); //in_uv.x + sin(time) * 0.1 — сдвиг по U колеблется (sin(time)), fract(...) — дробная часть (чтобы UV оставался в [0, 1] — тайлинг)

        vec3 c1 = texture(albedo_texture, uv1).rgb;
        vec3 c2 = texture(albedo_texture, uv2).rgb;
        vec3 c3 = texture(albedo_texture, uv3).rgb;
        base_color = (c1 + c2 + c3) / 3.0;
    } else {
        base_color = texture(albedo_texture, in_uv).rgb;
    }

    vec3 spec_mask = texture(specular_texture, in_uv).rgb;

    vec3 color = base_color * scene_uniforms.ambient_light; //альбедо на цвет фонового света

    vec3 L_dir = normalize(-scene_uniforms.directional_light_direction);
    color += blinn_phong(N, L_dir, V, scene_uniforms.directional_light_color, base_color, spec_mask);

    for (uint i = 0u; i < scene_uniforms.point_light_count; ++i) {
        vec3 to_light = point_lights[i].position - in_world_position;
        float dist = length(to_light);
        vec3 L = to_light / dist;
        float attenuation = 1.0 / (dist * dist);
        color += blinn_phong(N, L, V,
                             point_lights[i].color * point_lights[i].intensity,
                             base_color, spec_mask) * attenuation;
    }

    for (uint i = 0u; i < scene_uniforms.spot_light_count; ++i) {
        vec3 to_light = spot_lights[i].position - in_world_position;
        float dist = length(to_light);
        vec3 L = to_light / dist;
        vec3 spot_dir = normalize(spot_lights[i].direction);
        float theta = dot(-L, spot_dir);
        float epsilon = spot_lights[i].inner_angle - spot_lights[i].outer_angle;
        float intensity = clamp((theta - spot_lights[i].outer_angle) / epsilon, 0.0, 1.0);
        float attenuation = 1.0 / (dist * dist);
        color += blinn_phong(N, L, V,
                             spot_lights[i].color * spot_lights[i].intensity,
                             base_color, spec_mask) * attenuation * intensity;
    }

    color += texture(emissive_texture, in_uv).rgb;

    out_color = vec4(color, 1.0);
}
