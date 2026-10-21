#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "application.hpp"

#include <imgui.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

struct SimpleVertex {  
    glm::vec3 position;
};

struct PushData { //для передачи данных в визуализационный шейдер 
    glm::mat4 model;
    glm::vec4 color;
};

glm::vec3 computeNormal(glm::vec3 position) {
    return glm::normalize(position);
}

const Vertex octahedron_vertices[] = {
    {glm::vec3( 0.0f,  1.0f,  0.0f), computeNormal(glm::vec3( 0.0f,  1.0f,  0.0f))},
    {glm::vec3( 1.0f,  0.0f,  0.0f), computeNormal(glm::vec3( 1.0f,  0.0f,  0.0f))},
    {glm::vec3( 0.0f,  0.0f,  1.0f), computeNormal(glm::vec3( 0.0f,  0.0f,  1.0f))},
    {glm::vec3(-1.0f,  0.0f,  0.0f), computeNormal(glm::vec3(-1.0f,  0.0f,  0.0f))},
    {glm::vec3( 0.0f,  0.0f, -1.0f), computeNormal(glm::vec3( 0.0f,  0.0f, -1.0f))},
    {glm::vec3( 0.0f, -1.0f,  0.0f), computeNormal(glm::vec3( 0.0f, -1.0f,  0.0f))},
};

const uint32_t octahedron_indices[] = {
    0, 1, 2,
    0, 2, 3,
    0, 3, 4,
    0, 4, 1,
    5, 2, 1,
    5, 3, 2,
    5, 4, 3,
    5, 1, 4,
};

const SimpleVertex cube_vertices[] = {
    {glm::vec3(-0.5f, -0.5f, -0.5f)},  
    {glm::vec3( 0.5f, -0.5f, -0.5f)},  
    {glm::vec3( 0.5f,  0.5f, -0.5f)},  
    {glm::vec3(-0.5f,  0.5f, -0.5f)},  
    {glm::vec3(-0.5f, -0.5f,  0.5f)}, 
    {glm::vec3( 0.5f, -0.5f,  0.5f)}, 
    {glm::vec3( 0.5f,  0.5f,  0.5f)}, 
    {glm::vec3(-0.5f,  0.5f,  0.5f)},
};

const uint32_t cube_indices[] = {
    0, 2, 1,  0, 3, 2, //по 2 треугольника на каждую из 6ти граней
    4, 5, 6,  4, 6, 7,
    0, 7, 3,  0, 4, 7,
    1, 6, 5,  1, 2, 6,
    0, 1, 5,  0, 5, 4,
    3, 7, 6,  3, 6, 2,
};

std::vector<SimpleVertex> cone_vertices;
std::vector<uint32_t> cone_indices;

void generateCone(float radius, float height, int segments) { //segments - сколько точек в круге 
    cone_vertices.clear();
    cone_indices.clear();

    cone_vertices.push_back({glm::vec3(0, 0, 0)}); //острие в центре

    for (int i = 0; i <= segments; ++i) { // <= segments - надо замкнуть круг, чтобы первая точка совпадала с последней
        float angle = 2.0f * 3.14159f * i / segments;
        float x = cos(angle) * radius;  
        float z = sin(angle) * radius;  
        cone_vertices.push_back({glm::vec3(x, -height, z)});
    }

    for (int i = 0; i < segments; ++i) { //соединяем треугольники, острие, текущая точка на кругу, следующая точка на кругу
        cone_indices.push_back(0);
        cone_indices.push_back(1 + i);
        cone_indices.push_back(1 + i + 1);
    }
}


namespace application {

VkPipeline vk_pipeline = VK_NULL_HANDLE;
VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;

VkBuffer vk_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vk_vertex_buffer_allocation = VK_NULL_HANDLE;
VkBuffer vk_index_buffer = VK_NULL_HANDLE;
VmaAllocation vk_index_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_scene_uniform_buffer = VK_NULL_HANDLE;
VmaAllocation vk_scene_uniform_buffer_allocation = VK_NULL_HANDLE;
void* vk_scene_uniform_buffer_mapped = nullptr;

VkBuffer vk_model_uniform_buffer = VK_NULL_HANDLE;
VmaAllocation vk_model_uniform_buffer_allocation = VK_NULL_HANDLE;
void* vk_model_uniform_buffer_mapped = nullptr;


VkBuffer vk_point_light_buffer = VK_NULL_HANDLE;
VmaAllocation vk_point_light_buffer_allocation = VK_NULL_HANDLE;
void* vk_point_light_buffer_mapped = nullptr;

VkBuffer vk_spot_light_buffer = VK_NULL_HANDLE;
VmaAllocation vk_spot_light_buffer_allocation = VK_NULL_HANDLE;
void* vk_spot_light_buffer_mapped = nullptr;

VkDescriptorSetLayout vk_scene_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout vk_model_descriptor_set_layout = VK_NULL_HANDLE;

VkDescriptorPool vk_common_descriptor_pool = VK_NULL_HANDLE;

VkDescriptorSet vk_scene_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet vk_model_descriptor_set = VK_NULL_HANDLE;


VkPipeline vk_vis_pipeline = VK_NULL_HANDLE;
VkPipelineLayout vk_vis_pipeline_layout = VK_NULL_HANDLE;

VkBuffer vk_cube_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vk_cube_vertex_buffer_allocation = VK_NULL_HANDLE;
VkBuffer vk_cube_index_buffer = VK_NULL_HANDLE;
VmaAllocation vk_cube_index_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_cone_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vk_cone_vertex_buffer_allocation = VK_NULL_HANDLE;
VkBuffer vk_cone_index_buffer = VK_NULL_HANDLE;
VmaAllocation vk_cone_index_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_vis_uniform_buffer = VK_NULL_HANDLE;
VmaAllocation vk_vis_uniform_buffer_allocation = VK_NULL_HANDLE;
void* vk_vis_uniform_buffer_mapped = nullptr;

VkDescriptorSetLayout vk_vis_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorSet  vk_vis_descriptor_set = VK_NULL_HANDLE;

bool show_light_visualization = true;


struct SceneUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_position;
    float pad0;
    glm::vec3 directional_light_direction; //куда светит солнце 
    float pad1;
    glm::vec3 directional_light_color; //цвет солнца
    float pad2;
    glm::vec3 ambient_light; //цвет фонового света
    float pad3;
    uint32_t point_light_count; //сколько точечных ламп
    uint32_t spot_light_count; //сколько прожекторов
    float pad4[2];
};

struct ModelUniform {
    glm::mat4 model;
    glm::mat4 normal_matrix;
    glm::vec3 albedo; //базовый цвет модели
    float alpha; //коэффициент блеска
    glm::vec3 specular; //цвет блика
    float padding;
};

struct PointLight {
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float pad1;
};

struct SpotLight {
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float pad1;
    glm::vec3 direction; //вектор направления
    float inner_angle; //косинус внутреннего угла (полная яркость)
    float outer_angle; //косинус внешнего угла (край косинуса)
    float padding[3];
};

struct VisUniforms {
    glm::mat4 view_projection; // матрица view * projection
};

float object_rotation[3] = {0.0f, 0.0f, 0.0f};
float object_position[3] = {0.0f, 0.0f, 0.0f};
float object_scale[3] = {1.0f, 1.0f, 1.0f};

bool use_perspective = true;
float fov = 45.0f;
float ortho_size = 5.0f;

bool animation_playing = true;
float animation_speed = 1.0f;
float animation_radius = 2.0f;

float camera_position[3] = {0.0f, 0.0f, 3.0f};
float camera_target[3] = {0.0f, 0.0f, 0.0f};

float dir_light_direction[3] = {-1.0f, -1.0f, -1.0f};
float dir_light_color[3] = {1.0f, 1.0f, 1.0f};
float dir_light_intensity = 1.0f;

float ambient_color[3] = {0.1f, 0.1f, 0.1f};

float material_albedo[3] = {0.8f, 0.3f, 0.3f};
float material_specular[3] = {1.0f, 1.0f, 1.0f};
float material_alpha = 32.0f;

std::vector<PointLight> point_lights;
std::vector<SpotLight> spot_lights;

constexpr uint32_t MAX_POINT_LIGHTS = 16;
constexpr uint32_t MAX_SPOT_LIGHTS = 16;

void drawLightVisualization(VkCommandBuffer cmd);


VkShaderModule loadShaderModule(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); //// открыть в бинарном виде и сразу перейти в конец файла, чтобы опрееделить размер
    if (!file) {
        std::cerr << "Failed to open shader: " << path << "\n";
        return VK_NULL_HANDLE;
    }
    size_t size = file.tellg(); //текущая позиция файла (а мы в конце файла)
    std::vector<char> buffer(size);
    file.seekg(0); // в начало файла 
    file.read(buffer.data(), size);
    file.close();

    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = reinterpret_cast<const uint32_t*>(buffer.data()),
    };

    VkShaderModule module;
    if (vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

bool createVertexBuffer() {
    auto& ctx = graphics::internal::context;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(octahedron_vertices),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, //доступ только из одной очереди
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, //VMA сразу даст нам указатель на эту память, CPU будет писать в эту память последовательно
        .usage = VMA_MEMORY_USAGE_AUTO, //VMA сама выберет правильный тип памяти
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_vertex_buffer, &vk_vertex_buffer_allocation, &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create vertex buffer\n";
        return false;
    }
    std::memcpy(allocation_info.pMappedData, octahedron_vertices, sizeof(octahedron_vertices));
    return true;
}

bool createIndexBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(octahedron_indices),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_index_buffer, &vk_index_buffer_allocation, &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create index buffer\n";
        return false;
    }
    std::memcpy(allocation_info.pMappedData, octahedron_indices, sizeof(octahedron_indices));
    return true;
}

bool createSceneUniformBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SceneUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_scene_uniform_buffer, &vk_scene_uniform_buffer_allocation,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create scene uniform buffer\n";
        return false;
    }
    vk_scene_uniform_buffer_mapped = allocation_info.pMappedData;
    return true;
}

bool createModelUniformBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(ModelUniform),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_model_uniform_buffer, &vk_model_uniform_buffer_allocation,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create model uniform buffer\n";
        return false;
    }
    vk_model_uniform_buffer_mapped = allocation_info.pMappedData;
    return true;
}

bool createPointLightBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(PointLight) * MAX_POINT_LIGHTS,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_point_light_buffer, &vk_point_light_buffer_allocation,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create point light buffer\n";
        return false;
    }
    vk_point_light_buffer_mapped = allocation_info.pMappedData;
    return true;
}

bool createSpotLightBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(SpotLight) * MAX_SPOT_LIGHTS,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_spot_light_buffer, &vk_spot_light_buffer_allocation,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create spot light buffer\n";
        return false;
    }
    vk_spot_light_buffer_mapped = allocation_info.pMappedData;
    return true;
}

bool createCubeBuffers() {
    auto& ctx = graphics::internal::context;
    {
        VkBufferCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(cube_vertices),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo ai = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo out;
        if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                            &vk_cube_vertex_buffer, &vk_cube_vertex_buffer_allocation, &out) != VK_SUCCESS) return false;
        std::memcpy(out.pMappedData, cube_vertices, sizeof(cube_vertices));
    }
    {
        VkBufferCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(cube_indices),
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo ai = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo out;
        if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                            &vk_cube_index_buffer, &vk_cube_index_buffer_allocation, &out) != VK_SUCCESS) return false;
        std::memcpy(out.pMappedData, cube_indices, sizeof(cube_indices));
    }
    return true;
}

bool createConeBuffers() {
    auto& ctx = graphics::internal::context;
    {
        VkBufferCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(SimpleVertex) * cone_vertices.size(),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo ai = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo out;
        if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                            &vk_cone_vertex_buffer, &vk_cone_vertex_buffer_allocation, &out) != VK_SUCCESS) return false;
        std::memcpy(out.pMappedData, cone_vertices.data(), sizeof(SimpleVertex) * cone_vertices.size());
    }
    {
        VkBufferCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(uint32_t) * cone_indices.size(),
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo ai = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo out;
        if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                            &vk_cone_index_buffer, &vk_cone_index_buffer_allocation, &out) != VK_SUCCESS) return false;
        std::memcpy(out.pMappedData, cone_indices.data(), sizeof(uint32_t) * cone_indices.size());
    }
    return true;
}

bool createVisUniformBuffer() {
    auto& ctx = graphics::internal::context;
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VisUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo ai = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo out;
    if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                        &vk_vis_uniform_buffer, &vk_vis_uniform_buffer_allocation, &out) != VK_SUCCESS) return false;
    vk_vis_uniform_buffer_mapped = out.pMappedData;
    return true;
}

bool createSceneDescriptorSetLayout() { //описывает набор set = 0
    auto& ctx = graphics::internal::context;
    VkDescriptorSetLayoutBinding bindings[] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }, //нужен вершинному и фрагментному шейдерам
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings,
    };
    if (vkCreateDescriptorSetLayout(ctx.device, &layout_info, nullptr,
                                    &vk_scene_descriptor_set_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create scene descriptor set layout\n";
        return false;
    }
    return true;
}

bool createModelDescriptorSetLayout() {
    auto& ctx = graphics::internal::context;
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(ctx.device, &layout_info, nullptr,
                                    &vk_model_descriptor_set_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create model descriptor set layout\n";
        return false;
    }
    return true;
}

bool createVisDescriptorSetLayout() {
    auto& ctx = graphics::internal::context;
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    return vkCreateDescriptorSetLayout(ctx.device, &info, nullptr, &vk_vis_descriptor_set_layout) == VK_SUCCESS;
}

bool createDescriptorSets() {
    auto& ctx = graphics::internal::context;

    VkDescriptorPoolSize pool_sizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 3 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3, //максимум три набора, scene, model, vis
        .poolSizeCount = 2, //сколько типов ресурсов описано в pool_size
        .pPoolSizes = pool_sizes,
    };
    if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr,
                               &vk_common_descriptor_pool) != VK_SUCCESS) {
        std::cerr << "Failed to create common descriptor pool\n";
        return false;
    }

    VkDescriptorSetLayout layouts[] = {
        vk_scene_descriptor_set_layout, //set = 0
        vk_model_descriptor_set_layout, //set = 1
    };
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_common_descriptor_pool,
        .descriptorSetCount = 2,
        .pSetLayouts = layouts,
    };
    VkDescriptorSet sets[2];
    if (vkAllocateDescriptorSets(ctx.device, &alloc_info, sets) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor sets\n";
        return false;
    }
    vk_scene_descriptor_set = sets[0];
    vk_model_descriptor_set = sets[1];

    VkDescriptorBufferInfo scene_buffer_info = {
        .buffer = vk_scene_uniform_buffer,
        .offset = 0,
        .range = sizeof(SceneUniforms),
    };
    VkWriteDescriptorSet scene_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_scene_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &scene_buffer_info,
    };

    VkDescriptorBufferInfo point_buffer_info = {
        .buffer = vk_point_light_buffer,
        .offset = 0,
        .range = sizeof(PointLight) * MAX_POINT_LIGHTS,
    };
    VkWriteDescriptorSet point_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_scene_descriptor_set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &point_buffer_info,
    };

    VkDescriptorBufferInfo spot_buffer_info = {
        .buffer = vk_spot_light_buffer,
        .offset = 0,
        .range = sizeof(SpotLight) * MAX_SPOT_LIGHTS,
    };
    VkWriteDescriptorSet spot_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_scene_descriptor_set,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &spot_buffer_info,
    };

    VkDescriptorBufferInfo model_buffer_info = {
        .buffer = vk_model_uniform_buffer,
        .offset = 0,
        .range = sizeof(ModelUniform),
    };
    VkWriteDescriptorSet model_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_model_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &model_buffer_info,
    };

    VkWriteDescriptorSet writes[] = {
        scene_write, point_write, spot_write, model_write,
    };
    vkUpdateDescriptorSets(ctx.device, 4, writes, 0, nullptr);

    VkDescriptorSetAllocateInfo vis_alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_common_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk_vis_descriptor_set_layout,
    };
    if (vkAllocateDescriptorSets(ctx.device, &vis_alloc, &vk_vis_descriptor_set) != VK_SUCCESS) {
        std::cerr << "Failed to allocate vis descriptor set\n";
        return false;
    }
    VkDescriptorBufferInfo vis_buf_info = {
        .buffer = vk_vis_uniform_buffer,
        .offset = 0,
        .range = sizeof(VisUniforms),
    };
    VkWriteDescriptorSet vis_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_vis_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &vis_buf_info,
    };
    vkUpdateDescriptorSets(ctx.device, 1, &vis_write, 0, nullptr);

    return true;
}

bool createPipeline() {
    auto& ctx = graphics::internal::context;

    VkShaderModule vert = loadShaderModule("shaders/shader.vert.spv");
    VkShaderModule frag = loadShaderModule("shaders/shader.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" }, //точка входа в шейдерах - main
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0, //номер буфера - у нас один
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, //заливать треугольники 
        .cullMode = VK_CULL_MODE_BACK_BIT, //отсекать задние грани
        .frontFace = VK_FRONT_FACE_CLOCKWISE, //// лицевые — по часовой
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment,
    };
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
    };

    VkDescriptorSetLayout set_layouts[] = {
        vk_scene_descriptor_set_layout,
        vk_model_descriptor_set_layout,
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = set_layouts,
    };
    if (vkCreatePipelineLayout(ctx.device, &layout_info, nullptr, &vk_pipeline_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout\n";
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_pipeline_layout,
        .renderPass = ctx.render_pass,
        .subpass = 0,
    };
    if (vkCreateGraphicsPipelines(ctx.device, nullptr, 1, &pipeline_info, nullptr, &vk_pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline\n";
        return false;
    }

    vkDestroyShaderModule(ctx.device, vert, nullptr);
    vkDestroyShaderModule(ctx.device, frag, nullptr);
    return true;
}

bool createVisPipeline() {
    auto& ctx = graphics::internal::context;

    VkShaderModule vert = loadShaderModule("shaders/shader_vis.vert.spv");
    VkShaderModule frag = loadShaderModule("shaders/shader_vis.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(SimpleVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attr = {  
        .location = 0, .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(SimpleVertex, position),
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attr,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE, //рисуем все
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = true,
        .depthWriteEnable = false, //не пишем глубину
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .blendEnable = VK_TRUE, //смешивание включено, чтобы конус был полупрозрачным. Итоговый цвет = src_alpha * цвет_конуса + (1 - src_alpha) * цвет_фона (старый цвет)
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, //src * alpha и dst * (1-alpha)
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD, //Итоговая_альфа = 1.0 * альфа_нового + 0.0 * альфа_старого = альфа_нового
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment,
    };
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn_states,
    };

    VkPushConstantRange push_range = { //вместо дескрипторов
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushData),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk_vis_descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    if (vkCreatePipelineLayout(ctx.device, &layout_info, nullptr, &vk_vis_pipeline_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create vis pipeline layout\n";
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk_vis_pipeline_layout,
        .renderPass = ctx.render_pass,
        .subpass = 0,
    };
    if (vkCreateGraphicsPipelines(ctx.device, nullptr, 1, &pipeline_info, nullptr, &vk_vis_pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create vis pipeline\n";
        return false;
    }

    vkDestroyShaderModule(ctx.device, vert, nullptr);
    vkDestroyShaderModule(ctx.device, frag, nullptr);
    return true;
}

bool initialize() {
    generateCone(0.4f, 1.0f, 16);

    if (!createVertexBuffer()) return false;
    if (!createIndexBuffer()) return false;
    if (!createSceneUniformBuffer()) return false;
    if (!createModelUniformBuffer()) return false;
    if (!createPointLightBuffer()) return false;
    if (!createSpotLightBuffer()) return false;
    if (!createCubeBuffers()) return false;
    if (!createConeBuffers()) return false;
    if (!createVisUniformBuffer()) return false;
    if (!createSceneDescriptorSetLayout()) return false;
    if (!createModelDescriptorSetLayout()) return false;
    if (!createVisDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createPipeline()) return false;
    if (!createVisPipeline()) return false;
    return true;
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkQueueWaitIdle(ctx.graphics_queue);

    if (vk_pipeline) vkDestroyPipeline(ctx.device, vk_pipeline, nullptr);
    if (vk_pipeline_layout) vkDestroyPipelineLayout(ctx.device, vk_pipeline_layout, nullptr);
    if (vk_common_descriptor_pool) vkDestroyDescriptorPool(ctx.device, vk_common_descriptor_pool, nullptr);
    if (vk_scene_descriptor_set_layout) vkDestroyDescriptorSetLayout(ctx.device, vk_scene_descriptor_set_layout, nullptr);
    if (vk_model_descriptor_set_layout) vkDestroyDescriptorSetLayout(ctx.device, vk_model_descriptor_set_layout, nullptr);

    if (vk_scene_uniform_buffer) vmaDestroyBuffer(ctx.allocator, vk_scene_uniform_buffer, vk_scene_uniform_buffer_allocation);
    if (vk_model_uniform_buffer) vmaDestroyBuffer(ctx.allocator, vk_model_uniform_buffer, vk_model_uniform_buffer_allocation);
    if (vk_index_buffer) vmaDestroyBuffer(ctx.allocator, vk_index_buffer, vk_index_buffer_allocation);
    if (vk_vertex_buffer) vmaDestroyBuffer(ctx.allocator, vk_vertex_buffer, vk_vertex_buffer_allocation);
    if (vk_point_light_buffer) vmaDestroyBuffer(ctx.allocator, vk_point_light_buffer, vk_point_light_buffer_allocation);
    if (vk_spot_light_buffer)  vmaDestroyBuffer(ctx.allocator, vk_spot_light_buffer, vk_spot_light_buffer_allocation);

    if (vk_vis_pipeline) vkDestroyPipeline(ctx.device, vk_vis_pipeline, nullptr);
    if (vk_vis_pipeline_layout) vkDestroyPipelineLayout(ctx.device, vk_vis_pipeline_layout, nullptr);
    if (vk_vis_descriptor_set_layout) vkDestroyDescriptorSetLayout(ctx.device, vk_vis_descriptor_set_layout, nullptr);

    if (vk_cube_vertex_buffer) vmaDestroyBuffer(ctx.allocator, vk_cube_vertex_buffer, vk_cube_vertex_buffer_allocation);
    if (vk_cube_index_buffer)  vmaDestroyBuffer(ctx.allocator, vk_cube_index_buffer, vk_cube_index_buffer_allocation);
    if (vk_cone_vertex_buffer) vmaDestroyBuffer(ctx.allocator, vk_cone_vertex_buffer, vk_cone_vertex_buffer_allocation);
    if (vk_cone_index_buffer)  vmaDestroyBuffer(ctx.allocator, vk_cone_index_buffer, vk_cone_index_buffer_allocation);
    if (vk_vis_uniform_buffer) vmaDestroyBuffer(ctx.allocator, vk_vis_uniform_buffer, vk_vis_uniform_buffer_allocation);
}

void update(double time) {
    ImGui::Begin("Controls");

    ImGui::DragFloat3("Position", object_position, 0.1f, -5.0f, 5.0f);
    ImGui::DragFloat3("Rotation", object_rotation, 0.1f, -3.14f, 3.14f);
    ImGui::DragFloat3("Scale", object_scale, 0.01f, 0.1f, 3.0f);

    ImGui::Checkbox("Show light visualization", &show_light_visualization);

    ImGui::Checkbox("Perspective projection", &use_perspective);
    if (use_perspective) {
        ImGui::SliderFloat("FOV", &fov, 20.0f, 90.0f);
    } else {
        ImGui::SliderFloat("Ortho size", &ortho_size, 1.0f, 20.0f);
    }

    ImGui::Checkbox("Play animation", &animation_playing);
    ImGui::SliderFloat("Speed", &animation_speed, 0.1f, 5.0f);
    ImGui::SliderFloat("Radius", &animation_radius, 0.5f, 5.0f);

    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::SliderFloat3("Camera position", camera_position, -10.0f, 10.0f);
        ImGui::SliderFloat3("Camera target", camera_target, -10.0f, 10.0f);
    }

    if (ImGui::CollapsingHeader("Directional light")) {
        ImGui::DragFloat3("Direction", dir_light_direction, 0.1f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Color", dir_light_color);
        ImGui::SliderFloat("Intensity", &dir_light_intensity, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Ambient")) {
        ImGui::ColorEdit3("Ambient color", ambient_color);
    }

    if (ImGui::CollapsingHeader("Material")) {
        ImGui::ColorEdit3("Albedo", material_albedo);
        ImGui::ColorEdit3("Specular", material_specular);
        ImGui::SliderFloat("Alpha (shininess)", &material_alpha, 1.0f, 256.0f);
    }

    if (ImGui::CollapsingHeader("Point lights")) {
        if (ImGui::Button("Add point light")) {
            if (point_lights.size() < MAX_POINT_LIGHTS) {
                PointLight p{};
                p.position = glm::vec3(0.0f, 2.0f, 0.0f);
                p.color    = glm::vec3(1.0f, 1.0f, 1.0f);
                p.intensity = 1.0f;
                point_lights.push_back(p);
            }
        }
        ImGui::Text("Max: %d", MAX_POINT_LIGHTS);  

        for (size_t i = 0; i < point_lights.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::Text("Point light %d", (int)i);
            ImGui::SliderFloat3("Position", &point_lights[i].position.x, -10.0f, 10.0f);
            ImGui::ColorEdit3("Color", &point_lights[i].color.x);
            ImGui::SliderFloat("Intensity", &point_lights[i].intensity, 0.0f, 5.0f);
            if (ImGui::Button("Remove")) {
                point_lights.erase(point_lights.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Spot lights")) {
        if (ImGui::Button("Add spot light")) {
            if (spot_lights.size() < MAX_SPOT_LIGHTS) {
                SpotLight s{};
                s.position    = glm::vec3(0.0f, 3.0f, 0.0f);
                s.color       = glm::vec3(1.0f, 1.0f, 1.0f);
                s.intensity   = 1.0f;
                s.direction   = glm::vec3(0.0f, -1.0f, 0.0f);
                s.inner_angle = cos(glm::radians(15.0f));
                s.outer_angle = cos(glm::radians(30.0f));
                spot_lights.push_back(s);
            }
        }
        ImGui::Text("Max: %d", MAX_SPOT_LIGHTS);  

        for (size_t i = 0; i < spot_lights.size(); ++i) {
            ImGui::PushID((int)(100 + i)); //чтоб id не пересекались с точечными источниками 
            ImGui::Text("Spot light %d", (int)i);
            ImGui::SliderFloat3("Position", &spot_lights[i].position.x, -10.0f, 10.0f);
            ImGui::SliderFloat3("Direction", &spot_lights[i].direction.x, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", &spot_lights[i].color.x);
            ImGui::SliderFloat("Intensity", &spot_lights[i].intensity, 0.0f, 5.0f);

            float inner_deg = glm::degrees(acos(glm::clamp(spot_lights[i].inner_angle, -1.0f, 1.0f))); //clamp обрезает до [-1, 1], превращает косину обратно в радианы (у нас углы в косинусах хранятся (удобнее для шейдера))
            float outer_deg = glm::degrees(acos(glm::clamp(spot_lights[i].outer_angle, -1.0f, 1.0f)));
            if (ImGui::SliderFloat("Inner angle", &inner_deg, 1.0f, 89.0f)) {
                spot_lights[i].inner_angle = cos(glm::radians(inner_deg));
            }
            if (ImGui::SliderFloat("Outer angle", &outer_deg, 1.0f, 89.0f)) {
                spot_lights[i].outer_angle = cos(glm::radians(outer_deg));
            }
            if (ImGui::Button("Remove")) {
                spot_lights.erase(spot_lights.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::End();

    if (animation_playing) {
        object_position[0] = animation_radius * cos(time * animation_speed);
        object_position[1] = animation_radius * sin(time * animation_speed * 2.0f);
        object_position[2] = animation_radius * sin(time * animation_speed);
        object_rotation[1] = time * animation_speed;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(object_position[0], object_position[1], object_position[2])); //матрица перемещения, применяется перемещение на вектор object_position, то есть объект сдвигается на этот вектор
    model = glm::rotate(model, object_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f)); //поворот вокруг оси X
    model = glm::rotate(model, object_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, object_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(object_scale[0], object_scale[1], object_scale[2]));

    glm::mat3 n1 = glm::transpose(glm::inverse(glm::mat3(model)));

    ModelUniform model_u;
    model_u.model         = model;
    model_u.normal_matrix = glm::mat4(n1);
    model_u.albedo        = glm::vec3(material_albedo[0], material_albedo[1], material_albedo[2]);
    model_u.alpha         = material_alpha;
    model_u.specular      = glm::vec3(material_specular[0], material_specular[1], material_specular[2]);
    std::memcpy(vk_model_uniform_buffer_mapped, &model_u, sizeof(model_u));

    float aspect = float(graphics::internal::context.swapchain_extent.width) /
                   float(graphics::internal::context.swapchain_extent.height); //отношение сторон окна
    glm::mat4 projection;
    if (use_perspective) {
        projection = glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f); // всё, что ближе к камере, чем 0.1 — не рисуется, всё, что дальше от камеры, чем 100 — не рисуется.
    } else {
        float h = ortho_size;
        float w = h * aspect;
        projection = glm::ortho(-w, w, -h, h, -100.0f, 100.0f); //матрица ортографической проекции (левый край, правый, нижний верхний края, ближняя плоскость по Z, дальняя плоскость по Z)
    }
    projection[1][1] *= -1;

    glm::vec3 eye    = glm::vec3(camera_position[0], camera_position[1], camera_position[2]);
    glm::vec3 center = glm::vec3(camera_target[0], camera_target[1], camera_target[2]);
    glm::vec3 up     = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view   = glm::lookAt(eye, center, up);

    uint32_t point_count = (uint32_t)std::min(point_lights.size(), (size_t)MAX_POINT_LIGHTS);
    uint32_t spot_count  = (uint32_t)std::min(spot_lights.size(), (size_t)MAX_SPOT_LIGHTS);

    SceneUniforms scene;
    scene.view        = view;
    scene.projection  = projection;
    scene.camera_position = eye;

    glm::vec3 dir = glm::vec3(dir_light_direction[0], dir_light_direction[1], dir_light_direction[2]);
    if (glm::length(dir) > 0.0001f) dir = glm::normalize(dir);
    scene.directional_light_direction = dir;
    scene.directional_light_color     = glm::vec3(dir_light_color[0], dir_light_color[1], dir_light_color[2]) * dir_light_intensity;
    scene.ambient_light               = glm::vec3(ambient_color[0], ambient_color[1], ambient_color[2]);
    scene.point_light_count           = point_count;  
    scene.spot_light_count            = spot_count;   
    std::memcpy(vk_scene_uniform_buffer_mapped, &scene, sizeof(scene));

    if (point_count > 0) {
        std::memcpy(vk_point_light_buffer_mapped, point_lights.data(),
                    sizeof(PointLight) * point_count);
    }
    if (spot_count > 0) {
        std::memcpy(vk_spot_light_buffer_mapped, spot_lights.data(),
                    sizeof(SpotLight) * spot_count);
    }

    VisUniforms vis;
    vis.view_projection = projection * view;
    std::memcpy(vk_vis_uniform_buffer_mapped, &vis, sizeof(vis));
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //один раз буфер будет отправлен на gpu и сброшен
    };
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    VkClearValue clear_values[] = {
        { .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
        { .depthStencil = { 1.0f, 0 } },
    };
    VkRenderPassBeginInfo render_pass_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = { .extent = ctx.swapchain_extent },
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE); //открываем «сессию рисования» в конкретный framebuffer

    VkViewport viewport = {
        .x = 0, .y = 0,
        .width = float(ctx.swapchain_extent.width),
        .height = float(ctx.swapchain_extent.height),
        .minDepth = 0, .maxDepth = 1,
    };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

    VkRect2D scissor = { .extent = ctx.swapchain_extent };
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vk_vertex_buffer, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    VkDescriptorSet sets[] = {
        vk_scene_descriptor_set,
        vk_model_descriptor_set,
    };
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_pipeline_layout, 0, 2, sets, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, 24, 1, 0, 0, 0);

    if (show_light_visualization) {
        drawLightVisualization(fd.command_buffer);
    }

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

void drawLightVisualization(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_vis_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_vis_pipeline_layout, 0, 1,
                            &vk_vis_descriptor_set, 0, nullptr);

    VkDeviceSize offset = 0;

    vkCmdBindVertexBuffers(cmd, 0, 1, &vk_cube_vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd, vk_cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& light : point_lights) {
        PushData pc;
        pc.model = glm::translate(glm::mat4(1.0f), light.position);
        pc.model = glm::scale(pc.model, glm::vec3(0.2f));
        pc.color = glm::vec4(light.color, 1.0f);

        vkCmdPushConstants(cmd, vk_vis_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc); //передаем pc (матрица + цвет) прямо в шейдер, без uniform-буфера
        vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    }

    for (const auto& light : spot_lights) {
        PushData pc;
        pc.model = glm::translate(glm::mat4(1.0f), light.position);
        pc.model = glm::scale(pc.model, glm::vec3(0.2f));
        pc.color = glm::vec4(light.color, 1.0f);

        vkCmdBindVertexBuffers(cmd, 0, 1, &vk_cube_vertex_buffer, &offset);
        vkCmdBindIndexBuffer(cmd, vk_cube_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, vk_vis_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);

        glm::vec3 dir = glm::normalize(light.direction); //куда светит прожектор
        glm::vec3 up = (fabs(dir.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(up, dir)); //cross - векторное произведение, вектор, перпендикулярный dir
        glm::vec3 real_up = glm::cross(dir, right);

        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(right, 0);
        rot[1] = glm::vec4(-dir, 0);
        rot[2] = glm::vec4(real_up, 0);

        float cone_height = 2.0f;
        float cone_radius = cone_height * tan(acos(light.outer_angle)); //tan(угла) = радиус / высота

        glm::mat4 cone_model = glm::translate(glm::mat4(1.0f), light.position);
        cone_model = cone_model * rot;
        cone_model = glm::scale(cone_model, glm::vec3(cone_radius, cone_height, cone_radius));

        pc.model = cone_model;
        pc.color = glm::vec4(light.color, 0.4f); //полупрозрачный

        vkCmdBindVertexBuffers(cmd, 0, 1, &vk_cone_vertex_buffer, &offset);
        vkCmdBindIndexBuffer(cmd, vk_cone_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd, vk_vis_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmd, (uint32_t)cone_indices.size(), 1, 0, 0, 0);
    }
}

} // namespace application