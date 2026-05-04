# Set minimum policy version before anything else to support older dependencies in CMake 4.x+
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

cmake_minimum_required(VERSION 3.12)

# Fix CMP0135 policy for FetchContent
cmake_policy(SET CMP0135 NEW)

project(SurvivalEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Helps IDEs find headers

# Configure Raylib before fetching
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

# Automatic Raylib download
include(FetchContent)
FetchContent_Declare(
  raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Use the recommended way for modern CMake
FetchContent_MakeAvailable(raylib)

find_package(Threads REQUIRED)

add_executable(game main.cpp)

# Link Raylib and ensure include directories are propagated
target_link_libraries(game PUBLIC raylib Threads::Threads)

# Explicitly add the include directory from the fetched source
target_include_directories(game PUBLIC ${raylib_SOURCE_DIR}/src)

# Additional system libraries for Windows
if(WIN32)
    target_link_libraries(game PUBLIC opengl32 gdi32 winmm shell32)
endif()
