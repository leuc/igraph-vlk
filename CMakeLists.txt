cmake_minimum_required(VERSION 3.15)
project(igraph-vlk C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(igraph REQUIRED)

option(USE_OPENXR "Enable OpenXR/VR support" ON)

set(OPENXR_SOURCES
    src/xr/openxr_context.c
    src/xr/openxr_vulkan.c
    src/xr/openxr_session.c
    src/xr/openxr_input.c
    src/xr/openxr_view.c
    src/xr/openxr_frame.c
)

if(USE_OPENXR)
    find_package(OpenXR)
    if(OpenXR_FOUND)
        message(STATUS "OpenXR support enabled")
        add_compile_definitions(USE_OPENXR=1)
    else()
        message(STATUS "OpenXR not found, disabling VR support")
        set(USE_OPENXR OFF CACHE BOOL "Enable OpenXR/VR support" FORCE)
    endif()
endif()

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
    shaders/menu.vert
    shaders/menu.frag
    shaders/transparent_sphere.vert
    shaders/transparent_sphere.frag
    shaders/routing.comp
    shaders/ray.vert
    shaders/ray.frag
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

    # Renderer core
    src/vulkan/renderer.c
    src/vulkan/renderer_geometry.c
    src/vulkan/renderer_compute.c
    src/vulkan/renderer_ui.c
    src/vulkan/renderer_pipelines.c
    src/vulkan/pipeline_graphics.c
    src/vulkan/pipeline_ui.c
    src/vulkan/pipeline_compute.c
    src/vulkan/vulkan_device.c
    src/vulkan/vulkan_swapchain.c
    src/vulkan/vulkan_commands.c
    src/vulkan/vulkan_render_pass.c
    src/vulkan/menu.c
    # Utilities and helpers
    src/vulkan/utils.c
    src/vulkan/app_path.c
    src/vulkan/polyhedron.c
    src/vulkan/text.c
    src/vulkan/animation_manager.c

    # Graph modules
    src/graph/graph_core.c
    src/graph/graph_io.c
    src/graph/graph_clustering.c
    src/graph/graph_filter.c
    src/graph/graph_layout.c
    src/graph/layout_openord.c
    src/graph/layered_sphere.c
    src/graph/graph_actions.c
     # Layout wrappers (split by algorithm)
     src/graph/layouts_force_fr.c
     src/graph/layouts_drl.c
     src/graph/layouts_davidson_harel.c
     src/graph/layouts_tree.c
     src/graph/layouts_sugiyama.c
     src/graph/layouts_basic.c
     src/graph/layouts_mds.c
     src/graph/layouts_bipartite.c
     src/graph/layouts_umap.c
     src/graph/layouts_graphopt.c
     src/graph/layouts_gem.c
     src/graph/layouts_yifan_hu.c
     src/graph/layouts_forceatlas2.c
     src/graph/layouts_tsne.c
     src/graph/layouts_apply.c
     src/graph/wrappers_community.c
     src/graph/wrappers_centrality.c
src/graph/wrappers_constructors.c
      src/graph/wrappers_paths.c
      src/graph/wrappers_structural.c
      src/graph/worker_thread.c

    src/graph/command_registry.c

    # Interaction modules
    src/interaction/camera.c
    src/interaction/input.c
    src/interaction/menu.c
    src/interaction/picking.c
    src/interaction/spatial.c
    src/interaction/state.c
    src/interaction/gamepad.c

    # UI modules
    src/ui/hud.c
    src/ui/menu.c

    ${SPIRV_SHADERS}
)

target_include_directories(igraph-vlk PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/graph
    ${Vulkan_INCLUDE_DIRS}
    ${CGLM_INCLUDE_DIR}
    ${IGRAPH_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(igraph-vlk PRIVATE
    Vulkan::Vulkan
    glfw
    igraph::igraph
    m
    OpenMP::OpenMP_C
)

if(USE_OPENXR)
    target_sources(igraph-vlk PRIVATE ${OPENXR_SOURCES})
    target_link_libraries(igraph-vlk PRIVATE OpenXR::openxr_loader)
endif()

target_compile_definitions(igraph-vlk PRIVATE
    VERT_SHADER_PATH="shaders/shader.vert.spv"
    FRAG_SHADER_PATH="shaders/shader.frag.spv"
    EDGE_VERT_SHADER_PATH="shaders/edge.vert.spv"
    EDGE_FRAG_SHADER_PATH="shaders/edge.frag.spv"
    LABEL_VERT_SHADER_PATH="shaders/label.vert.spv"
    LABEL_FRAG_SHADER_PATH="shaders/label.frag.spv"
    UI_VERT_SHADER_PATH="shaders/ui.vert.spv"
    UI_FRAG_SHADER_PATH="shaders/ui.frag.spv"
    MENU_VERT_SHADER_PATH="shaders/menu.vert.spv"
    MENU_FRAG_SHADER_PATH="shaders/menu.frag.spv"
    SPHERE_VERT_SHADER_PATH="shaders/transparent_sphere.vert.spv"
    SPHERE_FRAG_SHADER_PATH="shaders/transparent_sphere.frag.spv"
    ROUTING_COMP_SHADER_PATH="shaders/routing.comp.spv"
    RAY_VERT_SHADER_PATH="shaders/ray.vert.spv"
    RAY_FRAG_SHADER_PATH="shaders/ray.frag.spv"
)

install(TARGETS igraph-vlk DESTINATION bin)
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION share/igraph-vlk/shaders FILES_MATCHING PATTERN "*.spv")

add_custom_target(appdir
    COMMENT "Building AppDir for AppImage..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/AppDir/usr/bin
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/AppDir/usr/share/igraph-vlk/shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/AppDir/usr/share/icons/hicolor/scalable/apps
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:igraph-vlk> ${CMAKE_BINARY_DIR}/AppDir/usr/bin/igraph-vlk
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/shaders ${CMAKE_BINARY_DIR}/AppDir/usr/share/igraph-vlk/shaders
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/packaging/igraph-vlk.desktop ${CMAKE_BINARY_DIR}/AppDir/usr/share/applications/igraph-vlk.desktop
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/packaging/igraph-vlk.svg ${CMAKE_BINARY_DIR}/AppDir/usr/share/icons/igraph-vlk.svg
    COMMAND ${CMAKE_COMMAND} -E copy /usr/share/fonts/truetype/inconsolata/Inconsolata.otf ${CMAKE_BINARY_DIR}/AppDir/usr/share/fonts/truetype/inconsolata/Inconsolata.otf
    COMMAND ${CMAKE_COMMAND}
        -DBINARY=$<TARGET_FILE:igraph-vlk>
        -DAPPDIR=${CMAKE_BINARY_DIR}/AppDir
        -P ${CMAKE_SOURCE_DIR}/packaging/bundle-deps.cmake
    COMMAND ${CMAKE_COMMAND} -E create_symlink usr/bin/igraph-vlk ${CMAKE_BINARY_DIR}/AppDir/AppRun
    DEPENDS igraph-vlk
)
