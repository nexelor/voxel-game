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
find_package(glm REQUIRED)

file(GLOB_RECURSE SOURCES
    src/*.cpp
    engine/*.cpp
    game/*cpp
    # content/*cpp
)

add_executable(voxel_game ${SOURCES})

target_include_directories(voxel_game PRIVATE
    .
    vendor
)

target_link_libraries(voxel_game PRIVATE
    glfw
    Vulkan::Vulkan
    glm::glm
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

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(voxel_game PRIVATE -Werror)
endif()

# ─────────────────────────────────────────────
#  Shader compilation (custom target, not part
#  of the default build — run manually or hook
#  into a pre-build step if desired)
#
#  Usage:  cmake --build build --target shaders
# ─────────────────────────────────────────────
 
find_program(GLSLC glslc)
 
if(GLSLC)
    file(GLOB GLSL_SOURCES
        "${CMAKE_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_SOURCE_DIR}/shaders/*.frag"
        "${CMAKE_SOURCE_DIR}/shaders/*.comp"
    )
 
    # Output directory: <build>/shaders/
    set(SPIRV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${SPIRV_OUTPUT_DIR}")

    set(SPIRV_OUTPUTS "")
 
    foreach(GLSL ${GLSL_SOURCES})
        # Keep only the filename: voxel.vert → voxel.vert.spv
        get_filename_component(GLSL_NAME "${GLSL}" NAME)
        set(SPIRV "${SPIRV_OUTPUT_DIR}/${GLSL_NAME}.spv")
 
        add_custom_command(
            OUTPUT  "${SPIRV}"
            COMMAND "${GLSLC}" --target-env=vulkan1.2 -O -o "${SPIRV}" "${GLSL}"
            DEPENDS "${GLSL}"
            COMMENT "Compiling shader: ${GLSL_NAME}"
        )
        list(APPEND SPIRV_OUTPUTS "${SPIRV}")
    endforeach()
 
    add_custom_target(shaders DEPENDS ${SPIRV_OUTPUTS})
 
    # voxel_game always rebuilds shaders it needs before linking
    add_dependencies(voxel_game shaders)
else()
    message(WARNING "glslc not found — shaders must be compiled manually with compile_shaders.sh")
endif()

# ─────────────────────────────────────────────
#  Content assets (textures, etc.)
#
#  TextureAtlas reads PNGs at runtime from
#  content/assets/textures/ relative to the
#  process's working directory — same convention
#  as shaders/ above (see tools/compile_shaders.sh
#  header comment: run the game from the project
#  root, or from wherever this tree gets copied).
#
#  This symlinks (or copies, on platforms without
#  symlink support) content/ next to the built
#  executable so `cmake --build build && ./build/voxel_game`
#  works without manually shuffling assets around.
# ─────────────────────────────────────────────
 
add_custom_command(
    TARGET voxel_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_SOURCE_DIR}/content" "${CMAKE_BINARY_DIR}/content"
    COMMENT "Linking content/ into the build directory"
)