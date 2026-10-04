#pragma once

#include "graphics_internal.hpp"

namespace application {

bool initialize(); //создаёт объекты (pipeline, буферы)
void shutdown(); //уничтожает объекты

void update(double time); //обновляет интерфейс и динамические данные 
void render(const graphics::internal::FrameData& fd); //записывает команды в командный буфер

} // namespace application