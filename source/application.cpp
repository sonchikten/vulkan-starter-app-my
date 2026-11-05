#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "application.hpp"

#include <imgui.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <iostream>
#include "lodepng.h"
#include <algorithm>
#include <string>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct SimpleVertex {  
    glm::vec3 position;
};

struct PushData { //для передачи данных в визуализационный шейдер 
    glm::mat4 model;
    glm::vec4 color;
};

struct PipelineConsts {
    bool fancy_sampling;
    float time; //для анимации uv
};

glm::vec3 computeNormal(glm::vec3 position) {
    return glm::normalize(position);
}

glm::vec2 computeUV(glm::vec3 n) {
    float u = 0.5f + atan2(n.z, n.x) / (2.0f * 3.14159265f);
    float v = 0.5f - asin(glm::clamp(n.y, -1.0f, 1.0f)) / 3.14159265f;
    return glm::vec2(u, v);
}

const Vertex octahedron_vertices[] = {
    {glm::vec3( 0, 1, 0), computeNormal(glm::vec3(0,1,0)), computeUV(computeNormal(glm::vec3(0,1,0)))},
    {glm::vec3( 1, 0, 0), computeNormal(glm::vec3(1,0,0)), computeUV(computeNormal(glm::vec3(1,0,0)))},
    {glm::vec3( 0, 0, 1), computeNormal(glm::vec3(0,0,1)), computeUV(computeNormal(glm::vec3(0,0,1)))},
    {glm::vec3(-1, 0, 0), computeNormal(glm::vec3(-1,0,0)), computeUV(computeNormal(glm::vec3(-1,0,0)))},
    {glm::vec3( 0, 0,-1), computeNormal(glm::vec3(0,0,-1)), computeUV(computeNormal(glm::vec3(0,0,-1)))},
    {glm::vec3( 0,-1, 0), computeNormal(glm::vec3(0,-1,0)), computeUV(computeNormal(glm::vec3(0,-1,0)))},
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

struct Texture {
    VkImage image = VK_NULL_HANDLE; //пиксели в VRAM
    VmaAllocation allocation = VK_NULL_HANDLE; //память под VkImage
    VkImageView view = VK_NULL_HANDLE; // как читать (mip, layer, format)
    VkSampler sampler = VK_NULL_HANDLE; // настройки (bilinear, repeat)
};

struct Material { //набор текстур, описывающих поверхность
    Texture albedo; //базовая текстура
    Texture specular; //где блестит
    Texture emissive; //где светится (оранжевые пятна)
};

std::vector<Material> g_materials; //список текстур 
Texture g_black_texture; // служебная чёрная текстура размером 1x1 пиксель (при отсутствии тектсур для specular/emissive)
int g_selected_texture = 0;


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
std::vector<VkDescriptorSet> g_model_descriptor_sets; // массив дескриптор-сетов, по одному на материал


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


VkBuffer vk_staging_buffer = VK_NULL_HANDLE;
VmaAllocation vk_staging_buffer_allocation = VK_NULL_HANDLE;
void* vk_staging_buffer_mapped = nullptr;
VkDeviceSize vk_staging_buffer_offset = 0;

VkDeviceSize vk_model_uniform_stride = 0; //сдвиг между слотами UBO, stride кратен minUniformBufferOffsetAlignment - свойство фищического устройства
constexpr uint32_t MAX_MODELS = 16; //максимум моделей 

bool fancy_sampling_enabled = false;
double last_time = 0.0;


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
    glm::vec3 albedo;
    float alpha;
    glm::vec3 specular;
    float pad0;
    glm::vec4 uv_transform; // это аффиное преобразование UV в vertex шейдере xy = сдвиг, zw = масштаб 
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
    float outer_angle; //косинус внешнего угла  
    float padding[3];
};

struct VisUniforms {
    glm::mat4 view_projection; // матрица view * projection
};

float object_rotation[3] = {0.0f, 0.0f, 0.0f};
float object_position[3] = {0.0f, 0.0f, 0.0f};
float object_scale[3] = {1.0f, 1.0f, 1.0f};

float object2_position[3] = {2.5f, 0.0f, 0.0f};
float object2_rotation[3] = {0.0f, 0.0f, 0.0f};
float object2_scale[3]    = {0.8f, 0.8f, 0.8f};
int   object2_material_idx = 1; 

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


bool createStagingBuffer() {
    auto& ctx = graphics::internal::context;
    VkDeviceSize size = 64 * 1024 * 1024;
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //для копирования
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, //доступ из одной очереди 
    };
    VmaAllocationCreateInfo ai = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
               | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, //VMA сразу мапит память и возвращает pMappedData (указатель для CPU), CPU пишет последовательно
        .usage = VMA_MEMORY_USAGE_AUTO, //VMA сам выбирает тип памяти
    };
    VmaAllocationInfo out;
    if (vmaCreateBuffer(ctx.allocator, &info, &ai,
                        &vk_staging_buffer, &vk_staging_buffer_allocation, &out) != VK_SUCCESS) {
        std::cerr << "Failed to create staging buffer\n";
        return false;
    }
    vk_staging_buffer_mapped = out.pMappedData;
    vk_staging_buffer_offset = 0;
    return true;
}


VkShaderModule loadShaderModule(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); // открыть в бинарном виде и сразу перейти в конец файла, чтобы опрееделить размер
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

// копирование буфера из staging в VRAM
bool uploadBufferToGPU(const void* data, // указатель на данные RAM
                         VkDeviceSize size,
                        VkBufferUsageFlags usage,
                       VkBuffer& out_buf, VmaAllocation& out_alloc) {
    auto& ctx = graphics::internal::context;

    VkBufferCreateInfo binfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //буфер может быть целью копирования
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo bai = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    if (vmaCreateBuffer(ctx.allocator, &binfo, &bai,
                        &out_buf, &out_alloc, nullptr) != VK_SUCCESS) {
        std::cerr << "Failed to create GPU buffer\n";
        return false;
    }

    VkDeviceSize off = vk_staging_buffer_offset;
    std::memcpy((uint8_t*)vk_staging_buffer_mapped + off, data, size);
    vk_staging_buffer_offset += size;

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, //одноразовый пул 
        .queueFamilyIndex = ctx.graphics_queue_index,
    };
    VkCommandPool pool; //пул командных буферов 
    vkCreateCommandPool(ctx.device, &pci, nullptr, &pool);

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(ctx.device, &cbai, &cb);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &bi); //начинаем запись

    VkBufferCopy region = { .srcOffset = off, .dstOffset = 0, .size = size }; //копирование данных
    vkCmdCopyBuffer(cb, vk_staging_buffer, out_buf, 1, &region);
    vkEndCommandBuffer(cb);

    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(ctx.device, &fci, nullptr, &fence);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    vkQueueSubmit(ctx.graphics_queue, 1, &si, fence);
    vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(ctx.device, fence, nullptr);
    vkDestroyCommandPool(ctx.device, pool, nullptr);
    return true;
}


bool createVertexBuffer() {
    return uploadBufferToGPU(octahedron_vertices, sizeof(octahedron_vertices),
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             vk_vertex_buffer, vk_vertex_buffer_allocation);
}

bool createIndexBuffer() {
    return uploadBufferToGPU(octahedron_indices, sizeof(octahedron_indices),
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             vk_index_buffer, vk_index_buffer_allocation);
}

bool createCubeBuffers() {
    if (!uploadBufferToGPU(cube_vertices, sizeof(cube_vertices),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           vk_cube_vertex_buffer, vk_cube_vertex_buffer_allocation)) return false;
    if (!uploadBufferToGPU(cube_indices, sizeof(cube_indices),
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           vk_cube_index_buffer, vk_cube_index_buffer_allocation)) return false;
    return true;
}

bool createConeBuffers() {
    if (cone_vertices.empty() || cone_indices.empty()) return true;
    if (!uploadBufferToGPU(cone_vertices.data(), sizeof(SimpleVertex) * cone_vertices.size(),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           vk_cone_vertex_buffer, vk_cone_vertex_buffer_allocation)) return false;
    if (!uploadBufferToGPU(cone_indices.data(), sizeof(uint32_t) * cone_indices.size(),
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           vk_cone_index_buffer, vk_cone_index_buffer_allocation)) return false;
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
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
               | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
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

    VkPhysicalDeviceProperties props{}; //запрашиваем свойства GPU
    vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
    VkDeviceSize align = props.limits.minUniformBufferOffsetAlignment; //требуемое выравнивание для динамических UBO
    vk_model_uniform_stride = (sizeof(ModelUniform) + align - 1) & ~(align - 1); //округли sizeof вверх до ближайшего кратного align

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vk_model_uniform_stride * MAX_MODELS,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
               | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocation_info;
    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_model_uniform_buffer, &vk_model_uniform_buffer_allocation,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create dynamic model uniform buffer\n";
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

bool uploadTextureRGBA(const unsigned char* pixels, unsigned width, unsigned height, Texture& out) {
    auto& ctx = graphics::internal::context;

    VkDeviceSize image_size = VkDeviceSize(width) * height * 4;
    if (vk_staging_buffer_offset + image_size > 64ull * 1024 * 1024) { //если в staging не хватит места - ошибка
        std::cerr << "Staging buffer overflow (texture too big)\n";
        return false;
    }

    VkDeviceSize off = vk_staging_buffer_offset;
    std::memcpy((uint8_t*)vk_staging_buffer_mapped + off, pixels, image_size); //копируем пиксели в staging
    vk_staging_buffer_offset += image_size;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1, //без min-map
        .arrayLayers = 1,//один слой 
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, //копирование и чтение в шейдере
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo img_alloc_ai = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    if (vmaCreateImage(ctx.allocator, &image_info, &img_alloc_ai,
                       &out.image, &out.allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan image\n";
        return false;
    }

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = ctx.graphics_queue_index,
    };
    VkCommandPool pool;
    vkCreateCommandPool(ctx.device, &pci, nullptr, &pool);

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(ctx.device, &cbai, &cb);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b1 = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = out.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b1);

    VkBufferImageCopy region = {
        .bufferOffset = off,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {0,0,0},
        .imageExtent = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(cb, vk_staging_buffer, out.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier b2 = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, //жду окончания записи 
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT, //разрешаю чтение 
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = out.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b2);

    vkEndCommandBuffer(cb);

    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(ctx.device, &fci, nullptr, &fence);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    vkQueueSubmit(ctx.graphics_queue, 1, &si, fence);
    vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(ctx.device, fence, nullptr);
    vkDestroyCommandPool(ctx.device, pool, nullptr);

    VkImageViewCreateInfo view_info = { //как смотреть на пиксели 
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCreateImageView(ctx.device, &view_info, nullptr, &out.view);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
    vkCreateSampler(ctx.device, &sampler_info, nullptr, &out.sampler);

    return true;
}

bool createTextureFromFile(const char* path, Texture& out) {
    std::vector<unsigned char> pixels;
    unsigned width = 0, height = 0;
    if (lodepng::decode(pixels, width, height, path) != 0) {
        std::cerr << "Failed to decode PNG: " << path << "\n";
        return false;
    }
    return uploadTextureRGBA(pixels.data(), width, height, out);
}

bool createSolidTexture(unsigned char r, unsigned char g, unsigned char b, Texture& out) {
    unsigned char px[4] = { r, g, b, 255 };
    return uploadTextureRGBA(px, 1, 1, out);
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
    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {  
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {   
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {   
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = bindings,
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

    const uint32_t material_count = (uint32_t)g_materials.size();
    if (material_count == 0) {
        std::cerr << "No materials\n";
        return false;
    }

    VkDescriptorPoolSize pool_sizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },              // scene
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = material_count },  // model x N
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },              // point+spot
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = material_count * 3 }, // albedo+spec+emit
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1 + material_count + 1, // scene + N моделей + vis
        .poolSizeCount = 4,
        .pPoolSizes = pool_sizes,
    };
    if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr,
                               &vk_common_descriptor_pool) != VK_SUCCESS) {
        std::cerr << "Failed to create common descriptor pool\n";
        return false;
    }

    {
        VkDescriptorSetAllocateInfo a = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk_common_descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk_scene_descriptor_set_layout,
        };
        if (vkAllocateDescriptorSets(ctx.device, &a, &vk_scene_descriptor_set) != VK_SUCCESS) return false;
    }

    g_model_descriptor_sets.resize(material_count);
    {
        std::vector<VkDescriptorSetLayout> layouts(material_count, vk_model_descriptor_set_layout);
        VkDescriptorSetAllocateInfo a = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk_common_descriptor_pool,
            .descriptorSetCount = material_count,
            .pSetLayouts = layouts.data(),
        };
        if (vkAllocateDescriptorSets(ctx.device, &a, g_model_descriptor_sets.data()) != VK_SUCCESS) return false;
    }

    VkDescriptorBufferInfo scene_b = { vk_scene_uniform_buffer, 0, sizeof(SceneUniforms) };
    VkDescriptorBufferInfo point_b = { vk_point_light_buffer, 0, sizeof(PointLight) * MAX_POINT_LIGHTS };
    VkDescriptorBufferInfo spot_b  = { vk_spot_light_buffer,  0, sizeof(SpotLight)  * MAX_SPOT_LIGHTS };
    VkWriteDescriptorSet scene_writes[] = {
        { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=vk_scene_descriptor_set,
          .dstBinding=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo=&scene_b },
        { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=vk_scene_descriptor_set,
          .dstBinding=1, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo=&point_b },
        { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=vk_scene_descriptor_set,
          .dstBinding=2, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo=&spot_b },
    };
    vkUpdateDescriptorSets(ctx.device, 3, scene_writes, 0, nullptr);


    VkDescriptorBufferInfo model_b = { vk_model_uniform_buffer, 0, sizeof(ModelUniform) };
    for (uint32_t i = 0; i < material_count; ++i) {
        VkDescriptorImageInfo alb  = { g_materials[i].albedo.sampler,   g_materials[i].albedo.view,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo spec = { g_materials[i].specular.sampler, g_materials[i].specular.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo emit = { g_materials[i].emissive.sampler, g_materials[i].emissive.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w[] = {
            { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=g_model_descriptor_sets[i],
              .dstBinding=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
              .pBufferInfo=&model_b },
            { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=g_model_descriptor_sets[i],
              .dstBinding=1, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .pImageInfo=&alb },
            { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=g_model_descriptor_sets[i],
              .dstBinding=2, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .pImageInfo=&spec },
            { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=g_model_descriptor_sets[i],
              .dstBinding=3, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .pImageInfo=&emit },
        };
        vkUpdateDescriptorSets(ctx.device, 4, w, 0, nullptr);
    }

    {
        VkDescriptorSetAllocateInfo a = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk_common_descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk_vis_descriptor_set_layout,
        };
        if (vkAllocateDescriptorSets(ctx.device, &a, &vk_vis_descriptor_set) != VK_SUCCESS) return false;
    }
    VkDescriptorBufferInfo vis_b = { vk_vis_uniform_buffer, 0, sizeof(VisUniforms) };
    VkWriteDescriptorSet vis_w = {
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=vk_vis_descriptor_set,
        .dstBinding=0, .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo=&vis_b,
    };
    vkUpdateDescriptorSets(ctx.device, 1, &vis_w, 0, nullptr);

    return true;
}

void selectTexture(int idx) {
    if (idx < 0 || idx >= (int)g_materials.size()) return;
    g_selected_texture = idx;
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
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 3,
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

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PipelineConsts),
    };
    VkDescriptorSetLayout set_layouts[] = {
        vk_scene_descriptor_set_layout,
        vk_model_descriptor_set_layout,
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
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

    if (!createStagingBuffer()) return false;

    if (!createSolidTexture(0, 0, 0, g_black_texture)) return false;

    struct MatDesc {
        const char* albedo;
        const char* specular;   
        const char* emissive;   
    };
    const MatDesc materials_desc[] = {
        // Bricks: кирпич + шахматка (блик) + оранжевый (свечение)
        {"textures/bricks.png", "textures/spec_checker.png", "textures/bricks_emit.png"},

        // Green: трава + без блика + зелёный (свечение)
        {"textures/green.png", nullptr, "textures/green_emit.png"},

        // Zeepkats: картинка + пятна (блик) + синий (свечение)
        {"textures/zeepkats.png", "textures/spec_spots.png", "textures/zeepkats_emit.png"},

        // Fire: огонь + без блика + без свечения
        {"textures/fire.png", "textures/spec_spots.png", nullptr},
    };

    for (auto& md : materials_desc) {
        Material mat{};
        if (!createTextureFromFile(md.albedo, mat.albedo)) {
            std::cerr << "Skipping missing albedo: " << md.albedo << "\n";
            continue;
        }
        if (md.specular && !createTextureFromFile(md.specular, mat.specular)) {
            std::cerr << "Missing specular, using black: " << md.specular << "\n";
            mat.specular = g_black_texture;
        } else if (!md.specular) {
            mat.specular = g_black_texture;
        }
        if (md.emissive && !createTextureFromFile(md.emissive, mat.emissive)) {
            std::cerr << "Missing emissive, using black: " << md.emissive << "\n";
            mat.emissive = g_black_texture;
        } else if (!md.emissive) {
            mat.emissive = g_black_texture;
        }
        g_materials.push_back(mat);
    }
    if (g_materials.empty()) {
        std::cerr << "No textures loaded, aborting\n";
        return false;
    }

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
    vkQueueWaitIdle(ctx.graphics_queue); //блокирует CPU, пока GPU не закончит все команды в очереди

    for (auto& m : g_materials) {
        for (Texture* t : { &m.albedo, &m.specular, &m.emissive }) {
            if (t->sampler) vkDestroySampler(ctx.device, t->sampler, nullptr);
            if (t->view)    vkDestroyImageView(ctx.device, t->view, nullptr);
            if (t->image)   vmaDestroyImage(ctx.allocator, t->image, t->allocation);
        }
    }
    g_materials.clear();

    if (g_black_texture.sampler) vkDestroySampler(ctx.device, g_black_texture.sampler, nullptr);
    if (g_black_texture.view)    vkDestroyImageView(ctx.device, g_black_texture.view, nullptr);
    if (g_black_texture.image)   vmaDestroyImage(ctx.allocator, g_black_texture.image, g_black_texture.allocation);

    if (vk_staging_buffer)
        vmaDestroyBuffer(ctx.allocator, vk_staging_buffer, vk_staging_buffer_allocation);

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
    last_time = time;

    ImGui::Begin("Controls");

    if (ImGui::CollapsingHeader("Object 1", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", object_position, 0.1f, -5.0f, 5.0f);
        ImGui::DragFloat3("Rotation", object_rotation, 0.1f, -3.14f, 3.14f);
        ImGui::DragFloat3("Scale",    object_scale,    0.01f, 0.1f, 3.0f);

        ImGui::Separator();

        if (!g_materials.empty()) {
            std::string preview = "Tex " + std::to_string(g_selected_texture);
            if (ImGui::BeginCombo("Albedo", preview.c_str())) {
                for (int i = 0; i < (int)g_materials.size(); ++i) {
                    bool selected = (i == g_selected_texture);
                    std::string label = "Tex " + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        selectTexture(i);
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Checkbox("Fancy sampling", &fancy_sampling_enabled);
    }

    if (ImGui::CollapsingHeader("Object 2 (second octahedron)")) {
        ImGui::DragFloat3("Obj2 Position", object2_position, 0.1f, -10.0f, 10.0f);
        ImGui::DragFloat3("Obj2 Rotation", object2_rotation, 0.1f, -3.14f, 3.14f);
        ImGui::DragFloat3("Obj2 Scale",    object2_scale,    0.01f, 0.1f, 3.0f);

        ImGui::Separator();

        if (!g_materials.empty()) {
            std::string preview = "Obj2 Tex " + std::to_string(object2_material_idx);
            if (ImGui::BeginCombo("Obj2 Albedo", preview.c_str())) {
                for (int i = 0; i < (int)g_materials.size(); ++i) {
                    bool selected = (i == object2_material_idx);
                    std::string label = "Obj2 Tex " + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        object2_material_idx = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    if (ImGui::CollapsingHeader("Animation")) {
        ImGui::Checkbox("Play animation", &animation_playing);
        ImGui::SliderFloat("Speed",  &animation_speed,  0.1f, 5.0f);
        ImGui::SliderFloat("Radius", &animation_radius, 0.5f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::Checkbox("Perspective projection", &use_perspective);
        if (use_perspective) {
            ImGui::SliderFloat("FOV", &fov, 20.0f, 90.0f);
        } else {
            ImGui::SliderFloat("Ortho size", &ortho_size, 1.0f, 20.0f);
        }
        ImGui::SliderFloat3("Camera position", camera_position, -10.0f, 10.0f);
        ImGui::SliderFloat3("Camera target",   camera_target,   -10.0f, 10.0f);
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
                p.position  = glm::vec3(0.0f, 2.0f, 0.0f);
                p.color     = glm::vec3(1.0f, 1.0f, 1.0f);
                p.intensity = 1.0f;
                point_lights.push_back(p);
            }
        }
        ImGui::Text("Max: %d", MAX_POINT_LIGHTS);

        for (size_t i = 0; i < point_lights.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::Text("Point light %d", (int)i);
            ImGui::SliderFloat3("Position",  &point_lights[i].position.x, -10.0f, 10.0f);
            ImGui::ColorEdit3  ("Color",     &point_lights[i].color.x);
            ImGui::SliderFloat ("Intensity", &point_lights[i].intensity, 0.0f, 5.0f);
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
            ImGui::PushID((int)(100 + i));
            ImGui::Text("Spot light %d", (int)i);
            ImGui::SliderFloat3("Position",  &spot_lights[i].position.x,  -10.0f, 10.0f);
            ImGui::SliderFloat3("Direction", &spot_lights[i].direction.x, -1.0f, 1.0f);
            ImGui::ColorEdit3  ("Color",     &spot_lights[i].color.x);
            ImGui::SliderFloat ("Intensity", &spot_lights[i].intensity, 0.0f, 5.0f);

            float inner_deg = glm::degrees(acos(glm::clamp(spot_lights[i].inner_angle, -1.0f, 1.0f)));
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

    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Show light visualization", &show_light_visualization);
    }

    ImGui::End();

    if (animation_playing) {
        object_position[0] = animation_radius * cos(time * animation_speed);
        object_position[1] = animation_radius * sin(time * animation_speed * 2.0f);
        object_position[2] = animation_radius * sin(time * animation_speed);
        object_rotation[1] = time * animation_speed;

        object2_position[0] = -animation_radius * cos(time * animation_speed);
        object2_position[1] =  animation_radius * sin(time * animation_speed * 2.0f + 1.0f); // смещение фазы
        object2_position[2] = -animation_radius * sin(time * animation_speed);
        object2_rotation[1] = time * animation_speed;
    }

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(object_position[0], object_position[1], object_position[2])); //матрица перемещения, применяется перемещение на вектор object_position, то есть объект сдвигается на этот вектор
    model = glm::rotate(model, object_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f)); //поворот вокруг оси X
    model = glm::rotate(model, object_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, object_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(object_scale[0], object_scale[1], object_scale[2]));

    glm::mat3 n1 = glm::transpose(glm::inverse(glm::mat3(model)));

    {
        uint8_t* dst = (uint8_t*)vk_model_uniform_buffer_mapped + 0 * vk_model_uniform_stride;
        ModelUniform* model_u = (ModelUniform*)dst;

        model_u->model = model;
        model_u->normal_matrix = glm::mat4(n1);
        model_u->albedo = glm::vec3(material_albedo[0], material_albedo[1], material_albedo[2]);
        model_u->alpha = material_alpha;
        model_u->specular = glm::vec3(material_specular[0], material_specular[1], material_specular[2]);
        model_u->uv_transform = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    {
        glm::mat4 model2 = glm::mat4(1.0f);
        model2 = glm::translate(model2, glm::vec3(object2_position[0], object2_position[1], object2_position[2]));
        model2 = glm::rotate(model2, object2_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
        model2 = glm::rotate(model2, object2_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
        model2 = glm::rotate(model2, object2_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
        model2 = glm::scale(model2, glm::vec3(object2_scale[0], object2_scale[1], object2_scale[2]));

        glm::mat3 n2 = glm::transpose(glm::inverse(glm::mat3(model2)));

        uint8_t* dst = (uint8_t*)vk_model_uniform_buffer_mapped + 1 * vk_model_uniform_stride;
        ModelUniform* model_u = (ModelUniform*)dst;

        model_u->model = model2;
        model_u->normal_matrix = glm::mat4(n2);
        model_u->albedo = glm::vec3(1.0f, 1.0f, 1.0f);   // белый, чтобы текстура доминировала
        model_u->alpha = material_alpha;;
        model_u->specular = glm::vec3(1.0f);
        model_u->uv_transform = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    }

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
    scene.view = view;
    scene.projection = projection;
    scene.camera_position = eye;

    glm::vec3 dir = glm::vec3(dir_light_direction[0], dir_light_direction[1], dir_light_direction[2]);
    if (glm::length(dir) > 0.0001f) dir = glm::normalize(dir);
    scene.directional_light_direction = dir;
    scene.directional_light_color = glm::vec3(dir_light_color[0], dir_light_color[1], dir_light_color[2]) * dir_light_intensity;
    scene.ambient_light = glm::vec3(ambient_color[0], ambient_color[1], ambient_color[2]);
    scene.point_light_count = point_count;  
    scene.spot_light_count = spot_count;   
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

    {
        VkDescriptorSet sets[] = {
            vk_scene_descriptor_set,
            g_model_descriptor_sets[g_selected_texture],   // выбранная в UI текстура
        };
        uint32_t dynamic_offset = 0 * (uint32_t)vk_model_uniform_stride;
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_pipeline_layout, 0, 2, sets,
                                1, &dynamic_offset);

        PipelineConsts pc = {
            .fancy_sampling = fancy_sampling_enabled,
            .time = (float)last_time,
        };
        vkCmdPushConstants(fd.command_buffer, vk_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDrawIndexed(fd.command_buffer, 24, 1, 0, 0, 0);
    }

    {
        VkDescriptorSet sets[] = {
            vk_scene_descriptor_set,
            g_model_descriptor_sets[object2_material_idx],  // своя текстура
        };
        uint32_t dynamic_offset = 1 * (uint32_t)vk_model_uniform_stride;
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_pipeline_layout, 0, 2, sets,
                                1, &dynamic_offset);

        PipelineConsts pc = {
            .fancy_sampling = false,         // у второй модели обычное сэмплирование
            .time = (float)last_time,
        };
        vkCmdPushConstants(fd.command_buffer, vk_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDrawIndexed(fd.command_buffer, 24, 1, 0, 0, 0);
    }

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
