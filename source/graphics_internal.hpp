#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h> // ядра Vulkan (функции, структуры).

#include <vk_mem_alloc.h> // библиотека VMA для управления памятью GPU.

struct GLFWwindow;

namespace graphics::internal {

struct Context { //глобальное состояние Vulkan
	VkPhysicalDevice physical_device;
	VkDevice device;

	VmaAllocator allocator;

	VkQueue graphics_queue;
	uint32_t graphics_queue_index;

	VkFormat swapchain_format;
	VkExtent2D swapchain_extent;

	VkRenderPass render_pass;
};

struct FrameData { //данные одного кадра
	VkFramebuffer framebuffer; //контейнер для изображений (цвет + глубина). GPU рисует в него.
	VkCommandBuffer command_buffer; //список команд для GPU
};

extern Context context;

bool initialize(GLFWwindow* const window);
void shutdown();

void resize(uint32_t width, uint32_t height); //сообщает Vulkan о новом размере окна

FrameData prepare(); //получает framebuffer и командный буфер для кадра
void submitAndPresent(); //отправляет команды на GPU и показывает кадр

} // namespace graphics::internal