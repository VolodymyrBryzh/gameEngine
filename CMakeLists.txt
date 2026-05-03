cmake_minimum_required(VERSION 3.12)
project(SurvivalEngine)

set(CMAKE_CXX_STANDARD 17)

# Автоматичне завантаження Raylib (найпростіший шлях)
include(FetchContent)
FetchContent_Declare(
  raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

add_executable(game main.cpp)

# Підключаємо Raylib до нашого проекту
target_link_libraries(game raylib)

# Налаштування для Windows (щоб не було консолі в релізній версії, за бажанням)
if(WIN32)
    target_link_libraries(game opengl32 gdi32 winmm shell32)
endif()
