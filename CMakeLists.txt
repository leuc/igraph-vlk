cmake_minimum_required(VERSION 3.10)
project(graph-sphere C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

find_path(CGLM_INCLUDE_DIR cglm/cglm.h)
find_program(GLSL_COMPILER NAMES glslangValidator glslc REQUIRED)

set(SHADERS
    shaders/shader.vert
    shaders/shader.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSL_COMPILER} -V ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
    )
    list(APPEND SPIRV_SHADERS ${SPIRV})
endforeach()

add_executable(graph-sphere src/main.c src/renderer.c src/sphere.c src/text.c ${SPIRV_SHADERS})

target_include_directories(graph-sphere PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${CGLM_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(graph-sphere PRIVATE 
    Vulkan::Vulkan
    glfw
    m
)

# Define shader paths for the source code
target_compile_definitions(graph-sphere PRIVATE 
    VERT_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/shader.vert.spv"
    FRAG_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/shader.frag.spv"
)
