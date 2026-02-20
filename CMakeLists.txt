cmake_minimum_required(VERSION 3.10)
project(igraph-vlk C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(IGRAPH REQUIRED igraph)

find_path(CGLM_INCLUDE_DIR cglm/cglm.h)
find_program(GLSL_COMPILER NAMES glslangValidator glslc REQUIRED)

set(SHADERS
    shaders/shader.vert
    shaders/shader.frag
    shaders/edge.vert
    shaders/edge.frag
    shaders/label.vert
    shaders/label.frag
    shaders/ui.vert
    shaders/ui.frag
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

find_package(OpenMP REQUIRED)

add_executable(igraph-vlk 
    src/main.c 
    src/renderer.c 
    src/polyhedron.c 
    src/text.c
    src/graph_loader.c
    src/layout_openord.c
    src/layered_sphere.c
    ${SPIRV_SHADERS}
)

target_include_directories(igraph-vlk PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${CGLM_INCLUDE_DIR}
    ${IGRAPH_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(igraph-vlk PRIVATE 
    Vulkan::Vulkan
    glfw
    ${IGRAPH_LIBRARIES}
    m
    OpenMP::OpenMP_C
)

target_compile_definitions(igraph-vlk PRIVATE 
    VERT_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/shader.vert.spv"
    FRAG_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/shader.frag.spv"
    EDGE_VERT_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/edge.vert.spv"
    EDGE_FRAG_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/edge.frag.spv"
    LABEL_VERT_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/label.vert.spv"
    LABEL_FRAG_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/label.frag.spv"
    UI_VERT_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/ui.vert.spv"
    UI_FRAG_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/ui.frag.spv"
)
