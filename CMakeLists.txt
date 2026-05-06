cmake_minimum_required(VERSION 3.12)
project(SurvivalEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure Raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG master
)
FetchContent_Declare(
  rlimgui
  GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
  GIT_TAG main
)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)

FetchContent_MakeAvailable(raylib rlimgui imgui)

# 1. CORE ENGINE LIBRARY
add_library(survival_engine STATIC
    src/engine/engine.cpp
    src/engine/chunk_manager.h
)

target_include_directories(survival_engine PUBLIC 
    src/engine
    sdk
    ${raylib_SOURCE_DIR}/src
)
target_link_libraries(survival_engine PUBLIC raylib)

# 2. EDITOR APP
add_executable(survival_editor 
    src/editor/main.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_link_libraries(survival_editor PRIVATE survival_engine)
target_include_directories(survival_editor PRIVATE 
    src/editor
    ${rlimgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

# 3. GAME APP
add_executable(survival_game 
    src/game/main.cpp
)
target_link_libraries(survival_game PRIVATE survival_engine)
target_include_directories(survival_game PRIVATE src/game)

# Platform specific
if(WIN32)
    target_link_libraries(survival_editor PUBLIC opengl32 gdi32 winmm shell32)
    target_link_libraries(survival_game PUBLIC opengl32 gdi32 winmm shell32)
endif()
