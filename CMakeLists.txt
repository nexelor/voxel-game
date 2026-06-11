cmake_minimum_required(VERSION 3.28)

project(
    voxel_game
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)

file(GLOB_RECURSE SOURCES
    src/*.cpp
    engine/*.cpp
)

add_executable(voxel_game ${SOURCES})

target_include_directories(voxel_game PRIVATE
    .
)

target_link_libraries(voxel_game PRIVATE
    glfw
    Vulkan::Vulkan
)

target_compile_options(voxel_game PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(voxel_game PRIVATE
        -g
        -O0
    )
endif()