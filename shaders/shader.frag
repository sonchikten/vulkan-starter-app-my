#version 450 core

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;

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
    float inner_angle;   // cos(внутреннего угла)
    float outer_angle;   // cos(внешнего угла)
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
    float pad;
} model_uniforms;

vec3 blinn_phong(vec3 N, vec3 L, vec3 V, vec3 light_color) {
    float diff = max(dot(N, L), 0.0); // если угол меньше 0, то значит свет падает с обратной стороны, поэтому отрезаем
    vec3 diffuse = model_uniforms.albedo * diff * light_color;

    vec3 H = normalize(L + V); // половинчатый вектор
    float spec = pow(max(dot(N, H), 0.0), model_uniforms.alpha);
    vec3 specular = model_uniforms.specular * spec * light_color;

    return diffuse + specular;
}

void main() {
    vec3 N = normalize(in_normal);
    vec3 V = normalize(scene_uniforms.camera_position - in_world_position);

    vec3 color = model_uniforms.albedo * scene_uniforms.ambient_light;

    vec3 L_dir = normalize(-scene_uniforms.directional_light_direction); //вектор от поверхности к солнцу
    color += blinn_phong(N, L_dir, V, scene_uniforms.directional_light_color);

    for (uint i = 0u; i < scene_uniforms.point_light_count; ++i) {
        vec3 to_light = point_lights[i].position - in_world_position; //вектор от пикселя к лампочке
        float dist = length(to_light);
        vec3 L = to_light / dist;

        float attenuation = 1.0 / (dist * dist);

        color += blinn_phong(N, L, V, point_lights[i].color * point_lights[i].intensity) * attenuation;
    }

    for (uint i = 0u; i < scene_uniforms.spot_light_count; ++i) {
        vec3 to_light = spot_lights[i].position - in_world_position;
        float dist = length(to_light);
        vec3 L = to_light / dist;

        vec3 spot_dir = normalize(spot_lights[i].direction); //куда смотрит прожектор
        float theta = dot(-L, spot_dir);   // -L = от источника к пикселю
        //косинус угла между осью прожектора и направлением на пиксель

        float epsilon = spot_lights[i].inner_angle - spot_lights[i].outer_angle;
        float intensity = clamp((theta - spot_lights[i].outer_angle) / epsilon, 0.0, 1.0); //нормализует положение пикселя между outer и inner в диапазон [0, 1]

        float attenuation = 1.0 / (dist * dist);

        color += blinn_phong(N, L, V, spot_lights[i].color * spot_lights[i].intensity) * attenuation * intensity;
    }

    out_color = vec4(color, 1.0);
}
