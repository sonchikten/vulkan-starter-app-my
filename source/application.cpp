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
    glm::vec3 color;
};

glm::vec3 computeColor(glm::vec3 position) {
    return (position + glm::vec3(1.0f)) / 2.0f;
}

const Vertex octahedron_vertices[] = {
    {glm::vec3( 0.0f,  1.0f,  0.0f), computeColor(glm::vec3( 0.0f,  1.0f,  0.0f))},
    {glm::vec3( 1.0f,  0.0f,  0.0f), computeColor(glm::vec3( 1.0f,  0.0f,  0.0f))},
    {glm::vec3( 0.0f,  0.0f,  1.0f), computeColor(glm::vec3( 0.0f,  0.0f,  1.0f))},
    {glm::vec3(-1.0f,  0.0f,  0.0f), computeColor(glm::vec3(-1.0f,  0.0f,  0.0f))},
    {glm::vec3( 0.0f,  0.0f, -1.0f), computeColor(glm::vec3( 0.0f,  0.0f, -1.0f))},
    {glm::vec3( 0.0f, -1.0f,  0.0f), computeColor(glm::vec3( 0.0f, -1.0f,  0.0f))},
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

namespace application {

VkPipeline vk_pipeline = VK_NULL_HANDLE;
VkPipelineLayout vk_pipeline_layout = VK_NULL_HANDLE;
VkBuffer vk_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vk_vertex_buffer_allocation = VK_NULL_HANDLE;
VkBuffer vk_index_buffer = VK_NULL_HANDLE;
VmaAllocation vk_index_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_scene_uniform_buffer = VK_NULL_HANDLE; 
VkBuffer vk_model_uniform_buffer = VK_NULL_HANDLE;
VkBuffer vk_model_uniform_buffer_2 = VK_NULL_HANDLE;

VmaAllocation vk_scene_uniform_buffer_allocation = VK_NULL_HANDLE;
VmaAllocation vk_model_uniform_buffer_allocation = VK_NULL_HANDLE;
VmaAllocation vk_model_uniform_buffer_allocation_2 = VK_NULL_HANDLE;

void* vk_scene_uniform_buffer_mapped = nullptr; 
void* vk_model_uniform_buffer_mapped = nullptr;
void* vk_model_uniform_buffer_mapped_2 = nullptr;

VkDescriptorSetLayout vk_scene_descriptor_set_layout = VK_NULL_HANDLE;  
VkDescriptorSetLayout vk_model_descriptor_set_layout = VK_NULL_HANDLE; 

VkDescriptorPool vk_common_descriptor_pool = VK_NULL_HANDLE;

VkDescriptorSet vk_scene_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet vk_model_descriptor_set = VK_NULL_HANDLE;  
VkDescriptorSet vk_model_descriptor_set_2 = VK_NULL_HANDLE;



struct SceneUniforms {
    glm::mat4 view;
    glm::mat4 projection;
};

struct ModelUniform {
    glm::mat4 model;
    glm::vec3 color_multiplier;
    float padding; // для выравнивания
};

float object_rotation[3] = {0.0f, 0.0f, 0.0f};
float object_position[3] = {0.0f, 0.0f, 0.0f};
float object_scale[3] = {1.0f, 1.0f, 1.0f};

float object_rotation_2[3] = {0.0f, 0.0f, 0.0f};
float object_position_2[3] = {2.0f, 0.0f, 0.0f};
float object_scale_2[3] = {1.0f, 1.0f, 1.0f};

int selected_object = 0;
bool use_perspective = true; 
float fov = 45.0f; //field of view - угол обзора
float ortho_size = 10.0f; //размер области видимости для ортографической проекции

bool animation_playing = true;
float animation_speed = 1.0f;
float animation_radius = 2.0f;

float color_multiplier_2[3] = {1.0f, 1.0f, 1.0f};
float color_multiplier[3] = {1.0f, 1.0f, 1.0f};

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
    //создаем шейдерный модуль
    if (vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module) != VK_SUCCESS) { //graphics::internal::context.device - на каком GPU создать, nullptr - аллокатор не используется, куда записать handle шейдерного модуля
        return VK_NULL_HANDLE;
    }
    return module;
}

bool createVertexBuffer() {
    auto& ctx = graphics::internal::context;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(octahedron_vertices),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // назначение (буфер вершин)
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // доступ из одной очереди 
    };

    VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,// сразу отобразить память (получить указатель) и сpu будет писать последовательно 
        .usage = VMA_MEMORY_USAGE_AUTO, //VMA сам выбирет тип памяти 
    };

    VmaAllocationInfo allocation_info; //пустая структура, куда VMA запишет информацию о выделенной памяти, через нее будем получать доступ к памяти gpu

    if (vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info,
                        &vk_vertex_buffer, &vk_vertex_buffer_allocation, &allocation_info) != VK_SUCCESS) { //менеджер памяти VMA, 
        std::cerr << "Failed to create vertex buffer\n";
        return false;
    }
    std::memcpy(allocation_info.pMappedData, octahedron_vertices, sizeof(octahedron_vertices)); //куда копирвоать, что копировать и сколько - копия байт 
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

bool createModelUniformBuffer2() {
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
                        &vk_model_uniform_buffer_2, &vk_model_uniform_buffer_allocation_2,
                        &allocation_info) != VK_SUCCESS) {
        std::cerr << "Failed to create model uniform buffer 2\n";
        return false;
    }
    vk_model_uniform_buffer_mapped_2 = allocation_info.pMappedData;
    return true;
}

bool createSceneDescriptorSetLayout() {
    auto& ctx = graphics::internal::context;

    VkDescriptorSetLayoutBinding binding = { //описание одного дескриптора в наборе 
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, //используется в вершиной шейдере
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding, //указатель на binding = 0 
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
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,  
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,  
    };

    if (vkCreateDescriptorSetLayout(ctx.device, &layout_info, nullptr,
                                    &vk_model_descriptor_set_layout) != VK_SUCCESS) {
        std::cerr << "Failed to create scene descriptor set layout\n";
        return false;
    }
    return true;

}

bool createDescriptorSets() {
    auto& ctx = graphics::internal::context;

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 3,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr,
                               &vk_common_descriptor_pool) != VK_SUCCESS) {
        std::cerr << "Failed to create common descriptor pool\n";
        return false;
    }

    VkDescriptorSetLayout layouts[] = {
        vk_scene_descriptor_set_layout,
        vk_model_descriptor_set_layout,
        vk_model_descriptor_set_layout,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_common_descriptor_pool,
        .descriptorSetCount = 3,
        .pSetLayouts = layouts,
    };

    VkDescriptorSet sets[3];

    if (vkAllocateDescriptorSets(ctx.device, &alloc_info, sets) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor sets\n";
        return false;
    }

    vk_scene_descriptor_set = sets[0];
    vk_model_descriptor_set = sets[1];
    vk_model_descriptor_set_2 = sets[2];

    VkDescriptorBufferInfo scene_buffer_info = {
        .buffer = vk_scene_uniform_buffer,
        .offset = 0, //от начала
        .range = sizeof(SceneUniforms), //до конца
    };

    VkWriteDescriptorSet scene_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_scene_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &scene_buffer_info,
    };

    VkDescriptorBufferInfo model1_buffer_info = {
        .buffer = vk_model_uniform_buffer,
        .offset = 0,
        .range = sizeof(ModelUniform),
    };

    VkWriteDescriptorSet model1_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_model_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &model1_buffer_info,
    };

    VkDescriptorBufferInfo model2_buffer_info = {
        .buffer = vk_model_uniform_buffer_2,
        .offset = 0,
        .range = sizeof(ModelUniform),
    };

    VkWriteDescriptorSet model2_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk_model_descriptor_set_2,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &model2_buffer_info,
    };

    VkWriteDescriptorSet writes[] = { scene_write, model1_write, model2_write };
    vkUpdateDescriptorSets(ctx.device, 3, writes, 0, nullptr); //ноль копий и нет массива для копий

    return true;
}

bool createPipeline() {
    auto& ctx = graphics::internal::context;

    VkShaderModule vert = loadShaderModule("shaders/shader.vert.spv");
    VkShaderModule frag = loadShaderModule("shaders/shader.frag.spv");
    if (!vert || !frag) return false;

    VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription binding = {  
        .binding = 0, //номер буфера 
        .stride = sizeof(Vertex), // размер одной вершины
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX, // читать по вершине за раз 
    };
 
    VkVertexInputAttributeDescription attributes[] = { 
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, //один буфер вершин
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
        .viewportCount = 1, .scissorCount = 1, // динамически задаются в render 
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, //заливать треугольники
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE, //порядок обхода по часовой
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = { // сглаживание 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, //1 сэмпл на пиксель без сглаживания
    };

    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = true, //включить проверку глубины
        .depthWriteEnable = true, //записывать глубину в буфер
        .depthCompareOp = VK_COMPARE_OP_LESS, //ближе — рисуется поверх дальнего
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
        .stageCount = 2, .pStages = stages, //две стадии - два шейдера 
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

    if (vkCreateGraphicsPipelines(ctx.device, nullptr, 1, &pipeline_info, nullptr, &vk_pipeline) != VK_SUCCESS) { //pipline cache, сколько pipeline создать, 
        std::cerr << "Failed to create graphics pipeline\n";
        return false;
    }

    vkDestroyShaderModule(ctx.device, vert, nullptr);
    vkDestroyShaderModule(ctx.device, frag, nullptr);
    return true;
}

bool initialize() {
    if (!createVertexBuffer()) return false;
    if (!createIndexBuffer()) return false;
    if (!createSceneUniformBuffer()) return false;
    if (!createModelUniformBuffer()) return false;
    if (!createModelUniformBuffer2()) return false;
    if (!createSceneDescriptorSetLayout()) return false;
    if (!createModelDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createPipeline()) return false;
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
    if (vk_model_uniform_buffer_2) vmaDestroyBuffer(ctx.allocator, vk_model_uniform_buffer_2, vk_model_uniform_buffer_allocation_2);
    if (vk_index_buffer) vmaDestroyBuffer(ctx.allocator, vk_index_buffer, vk_index_buffer_allocation);
    if (vk_vertex_buffer) vmaDestroyBuffer(ctx.allocator, vk_vertex_buffer, vk_vertex_buffer_allocation);
}

void update(double time) {
    ImGui::Begin("Controls");
    ImGui::Text("Selected object:");
    ImGui::RadioButton("Object 1", &selected_object, 0);
    ImGui::SameLine(); //поставить второй переключатель на той же строке 
    ImGui::RadioButton("Object 2", &selected_object, 1);

    float* pos = (selected_object == 0) ? object_position : object_position_2;
    float* rot = (selected_object == 0) ? object_rotation : object_rotation_2;
    float* scl = (selected_object == 0) ? object_scale : object_scale_2;

    ImGui::SliderFloat3("Position", pos, -5.0f, 5.0f);
    ImGui::SliderFloat3("Rotation", rot, -3.14f, 3.14f);
    ImGui::SliderFloat3("Scale", scl, 0.1f, 3.0f);

    ImGui::Checkbox("Perspective projection", &use_perspective);
    if (use_perspective) {
        ImGui::SliderFloat("FOV", &fov, 20.0f, 90.0f);
    }

    if (!use_perspective) {
        ImGui::SliderFloat("Ortho size", &ortho_size, 1.0f, 20.0f);
    }

    ImGui::Checkbox("Play animation", &animation_playing);
    ImGui::SliderFloat("Speed", &animation_speed, 0.1f, 5.0f);
    ImGui::SliderFloat("Radius", &animation_radius, 0.5f, 5.0f);

    if (selected_object == 0) {
        ImGui::ColorEdit3("Color", color_multiplier);
    } else {
        ImGui::ColorEdit3("Color", color_multiplier_2);
    }

    ImGui::End();

    if (animation_playing) {
        //чтобы объект летал по горизонтальному кругу, меняем x, z
        object_position[0] = animation_radius * cos(time * animation_speed); //время растет - угол растет 
        object_position[2] = animation_radius * sin(time * animation_speed);
        object_rotation[1] = time * animation_speed; //вращаемся вокруг оси Y

        object_position_2[0] = animation_radius * cos(time * animation_speed + 3.14f);
        object_position_2[2] = animation_radius * sin(time * animation_speed + 3.14f);
        object_rotation_2[1] = -time * animation_speed;
    }

    glm::mat4 model = glm::mat4(1.0f); 
    model = glm::translate(model, glm::vec3(object_position[0], object_position[1], object_position[2])); //матрица перемещения, применяется перемещение на вектор object_position, то есть объект сдвигается на этот вектор
    model = glm::rotate(model, object_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f)); //поворот вокруг оси X
    model = glm::rotate(model, object_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, object_rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(object_scale[0], object_scale[1], object_scale[2]));

    ModelUniform model_u;
    model_u.model = model;
    model_u.color_multiplier = glm::vec3(color_multiplier[0], color_multiplier[1], color_multiplier[2]);
    std::memcpy(vk_model_uniform_buffer_mapped, &model_u, sizeof(model_u));

    glm::mat4 model_2 = glm::mat4(1.0f);
    model_2 = glm::translate(model_2, glm::vec3(object_position_2[0], object_position_2[1], object_position_2[2]));
    model_2 = glm::rotate(model_2, object_rotation_2[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model_2 = glm::rotate(model_2, object_rotation_2[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model_2 = glm::rotate(model_2, object_rotation_2[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model_2 = glm::scale(model_2, glm::vec3(object_scale_2[0], object_scale_2[1], object_scale_2[2]));

    ModelUniform model_u_2;
    model_u_2.model = model_2; 
    model_u_2.color_multiplier = glm::vec3(color_multiplier_2[0], color_multiplier_2[1], color_multiplier_2[2]);
    std::memcpy(vk_model_uniform_buffer_mapped_2, &model_u_2, sizeof(model_u_2));

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

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, center, up);

    SceneUniforms scene;
    scene.view = view;
    scene.projection = projection;
    std::memcpy(vk_scene_uniform_buffer_mapped, &scene, sizeof(scene));
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //что буфер команд для gpu будет отправлен один раз и сброшен
    };
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    VkClearValue clear_values[] = { 
        { .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
        { .depthStencil = { 1.0f, 0 } },
    };

    VkRenderPassBeginInfo render_pass_begin = { //очищаем экран перед рисованием 
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = { .extent = ctx.swapchain_extent },
        .clearValueCount = 2,
        .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .x = 0, .y = 0, //левый верхний угол 
        .width = float(ctx.swapchain_extent.width),
        .height = float(ctx.swapchain_extent.height),
        .minDepth = 0, .maxDepth = 1,
    };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

    VkRect2D scissor = { .extent = ctx.swapchain_extent };
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline); //привязываем пайплайн

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vk_vertex_buffer, &offset); //binding = 0, 1 буфер
    vkCmdBindIndexBuffer(fd.command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);//0 - смещение

    VkDescriptorSet sets_1[] = {
        vk_scene_descriptor_set,
        vk_model_descriptor_set,
    };
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vk_pipeline_layout, 0, 2, sets_1, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, 24, 1, 0, 0, 0);

    VkDescriptorSet sets_2[] = {
        vk_scene_descriptor_set,
        vk_model_descriptor_set_2,
    };
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vk_pipeline_layout, 0, 2, sets_2, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, 24, 1, 0, 0, 0);

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}
} // namespace application