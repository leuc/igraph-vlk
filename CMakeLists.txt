# Copyright 2026 igraph-vlk team SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)
project(igraph-vlk VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(USE_OPENXR "Enable OpenXR/VR support" ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(igraph REQUIRED)
find_package(OpenMP)
find_package(CURL REQUIRED)
find_path(CGLM_INCLUDE_DIR cglm/cglm.h)
find_program(GLSL_COMPILER NAMES glslangValidator glslc REQUIRED)
find_package(zstd QUIET)
find_package(PkgConfig QUIET)

# falling back to pkg-config
if(NOT zstd_FOUND)
    # cmake_pkg_config doesn't exist on cmake 3.22 Ubuntu 22.04
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(zstd QUIET libzstd)
    endif()
endif()

if(NOT zstd_FOUND)
    message(FATAL_ERROR "Could not find zstd package using CMake Config or PkgConfig.")
endif()

# zstd on Ubuntu 22.04 / 24.04
if(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
    add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
elseif(TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd)
    add_library(zstd::libzstd ALIAS zstd::libzstd_static)
elseif(NOT TARGET zstd::libzstd AND zstd_LINK_LIBRARIES)
    # Fallback for pkg-config which creates variables rather than targets
    add_library(zstd::libzstd UNKNOWN IMPORTED)
    set_target_properties(zstd::libzstd PROPERTIES
        IMPORTED_LOCATION "${zstd_LINK_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${zstd_INCLUDE_DIRS}"
    )
endif()

set(OPENXR_SOURCES src/xr/openxr_context.c src/xr/openxr_vulkan.c src/xr/openxr_session.c src/xr/openxr_input.c src/xr/openxr_view.c src/xr/openxr_frame.c src/vulkan/renderer_xr.c)

set(WAYLAND_COLOR_MANAGEMENT_ENABLED OFF)
if(UNIX AND NOT APPLE AND PKG_CONFIG_FOUND)
	pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
	find_program(WAYLAND_SCANNER NAMES wayland-scanner)
	if(TARGET PkgConfig::WAYLAND_CLIENT AND WAYLAND_SCANNER)
		set(WAYLAND_COLOR_MANAGEMENT_ENABLED ON)
		set(WAYLAND_COLOR_PROTOCOL_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/color-management-v1.xml)
		set(WAYLAND_COLOR_PROTOCOL_HEADER ${CMAKE_BINARY_DIR}/generated/color-management-v1-client-protocol.h)
		set(WAYLAND_COLOR_PROTOCOL_SOURCE ${CMAKE_BINARY_DIR}/generated/color-management-v1-protocol.c)
		file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
		add_custom_command(
			OUTPUT ${WAYLAND_COLOR_PROTOCOL_HEADER} ${WAYLAND_COLOR_PROTOCOL_SOURCE}
			COMMAND ${WAYLAND_SCANNER} client-header ${WAYLAND_COLOR_PROTOCOL_XML} ${WAYLAND_COLOR_PROTOCOL_HEADER}
			COMMAND ${WAYLAND_SCANNER} private-code ${WAYLAND_COLOR_PROTOCOL_XML} ${WAYLAND_COLOR_PROTOCOL_SOURCE}
			DEPENDS ${WAYLAND_COLOR_PROTOCOL_XML}
			COMMENT "Generating Wayland color-management client protocol"
		)
		add_custom_target(wayland_color_protocol DEPENDS ${WAYLAND_COLOR_PROTOCOL_HEADER} ${WAYLAND_COLOR_PROTOCOL_SOURCE})
	endif()
endif()

# Platform detection for GLFW-specific window hints
if(APPLE)
	add_compile_definitions(_GLFW_COCOA=1)
elseif(WIN32)
	add_compile_definitions(_GLFW_WIN32=1)
elseif(UNIX)
	add_compile_definitions(_GLFW_X11=1 _GLFW_WAYLAND=1)
endif()

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

set(SHADERS shaders/node.vert shaders/node.frag shaders/edge.vert shaders/edge.frag shaders/label.vert shaders/label.frag shaders/ui.vert shaders/ui.frag shaders/menu.vert shaders/menu.frag shaders/textquad.vert shaders/textquad.frag shaders/routing.comp shaders/main_path.comp shaders/bcgl.comp shaders/ray.vert shaders/ray.frag shaders/present.vert shaders/present.frag)

foreach(shader ${SHADERS})
	get_filename_component(shader_name ${shader} NAME)
	set(_spirv "${CMAKE_BINARY_DIR}/shaders/${shader_name}.spv")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
	add_custom_command(OUTPUT ${_spirv} COMMAND ${GLSL_COMPILER} -V -I${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${_spirv} DEPENDS ${shader} shaders/color_space.glsl COMMENT "Compiling ${shader_name} to SPIR-V")
	list(APPEND SPIRV_SHADERS ${_spirv})
endforeach()

# Lets test targets that load a .spv at runtime depend on the compiled shaders
# without pulling in the whole application.
add_custom_target(shaders DEPENDS ${SPIRV_SHADERS})

add_executable(
	igraph-vlk
	src/main.c
	# Renderer core
	src/vulkan/renderer_lifecycle.c
	src/vulkan/renderer_draw.c
	src/vulkan/renderer_camera.c
	src/vulkan/renderer_anim.c
	src/vulkan/renderer_anim_values.c
	src/vulkan/renderer_labels.c
	src/vulkan/renderer_geometry.c
	src/vulkan/renderer_compute.c
	src/vulkan/renderer_ui.c
	src/vulkan/renderer_pipelines.c
	src/vulkan/renderer_present.c
	src/vulkan/pipeline_graphics.c
	src/vulkan/pipeline_ui.c
	src/vulkan/pipeline_compute.c
	src/vulkan/device.c
	src/vulkan/swapchain.c
	src/vulkan/surface_format.c
	src/vulkan/color_space.c
	src/vulkan/commands.c
	src/vulkan/render_pass.c
	src/vulkan/menu.c
	src/vulkan/menu_scene.c
	# Utilities and helpers
	src/vulkan/buffers.c
	src/vulkan/images.c
	src/vulkan/utils.c
	src/os/path.c
	src/os/stream.c
	src/vulkan/text.c
	# Extracted refactor modules
	src/vulkan/renderer_cleanup.c
	src/vulkan/renderer_update_node_labels.c
	src/vulkan/renderer_criticality.c
	src/vulkan/renderer_bcgl.c
	src/vulkan/renderer_transition.c
	# Graph modules
	src/graph/graph_core.c
	src/graph/graph_io.c
	src/graph/stream.c
	src/graph/ncol_parse.c
	src/graph/dyn_k-core.c
	src/graph/dyn_core_tree.c
	src/graph/dyn_core_tree_order.c
	src/graph/dyn_leiden.c
	src/graph/dyn_layered_sphere.c
	src/graph/dyn_ls_sphere_rotation.c
	src/graph/community_simhash.c
	src/graph/graph_filter.c
	src/graph/graph_filter_visibility.c
	src/graph/graph_color.c
	src/graph/layered_sphere.c
	src/graph/layered_sphere_common.c
	src/graph/graph_actions.c
	src/graph/graph_animation.c
	src/graph/main_path.c
	src/graph/main_path_cache.c
	src/graph/main_path_search.c
	src/graph/rotor_routing.c
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
	src/graph/layouts_vk_bcgl.c
	src/graph/layouts_bcgl.c
	src/graph/layouts_apply.c
	src/graph/layouts_seed.c
	src/graph/wrappers_community.c
	src/graph/wrappers_centrality.c
	src/graph/wrappers_constructors.c
	src/graph/wrappers_cycles.c
	src/graph/wrappers_filter.c
	src/graph/wrappers_flow.c
	src/graph/wrappers_kcore_tree.c
	src/graph/wrappers_path_cover.c
	src/graph/wrappers_paths.c
	src/graph/wrappers_rotor.c
	src/graph/wrappers_structural.c
	src/graph/repo.c
	src/graph/repo_netzschleuder.c
	src/graph/worker_thread.c
	src/graph/command_registry.c
	# Interaction modules
	src/interaction/camera.c
	src/interaction/display_hdr.c
	src/interaction/input.c
	src/interaction/menu.c
	src/interaction/menu_state.c
	src/interaction/picking.c
	src/interaction/spatial.c
	src/interaction/state.c
	src/interaction/gamepad.c
	src/interaction/window.c
	# UI modules
	src/ui/hud.c
	src/ui/menu.c
	src/ui/menu_metrics.c
	${WAYLAND_COLOR_PROTOCOL_SOURCE}
	${SPIRV_SHADERS})

target_include_directories(igraph-vlk PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/graph ${Vulkan_INCLUDE_DIRS} ${CGLM_INCLUDE_DIR} ${IGRAPH_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})

target_link_libraries(igraph-vlk PRIVATE Vulkan::Vulkan glfw igraph::igraph CURL::libcurl zstd::libzstd)
if(WAYLAND_COLOR_MANAGEMENT_ENABLED)
	add_dependencies(igraph-vlk wayland_color_protocol)
	target_compile_definitions(igraph-vlk PRIVATE HAVE_WAYLAND_COLOR_MANAGEMENT=1)
	target_include_directories(igraph-vlk PRIVATE ${CMAKE_BINARY_DIR}/generated)
	target_link_libraries(igraph-vlk PRIVATE PkgConfig::WAYLAND_CLIENT)
endif()
if(OpenMP_C_FOUND)
	target_link_libraries(igraph-vlk PRIVATE OpenMP::OpenMP_C)
endif()
if(NOT MSVC)
	target_link_libraries(igraph-vlk PRIVATE m)
endif()

if(USE_OPENXR)
	target_sources(igraph-vlk PRIVATE ${OPENXR_SOURCES})
	target_link_libraries(igraph-vlk PRIVATE OpenXR::openxr_loader)
endif()

target_compile_definitions(igraph-vlk PRIVATE NODE_VERT_SHADER_PATH="shaders/node.vert.spv" NODE_FRAG_SHADER_PATH="shaders/node.frag.spv" EDGE_VERT_SHADER_PATH="shaders/edge.vert.spv" EDGE_FRAG_SHADER_PATH="shaders/edge.frag.spv" LABEL_VERT_SHADER_PATH="shaders/label.vert.spv" LABEL_FRAG_SHADER_PATH="shaders/label.frag.spv" UI_VERT_SHADER_PATH="shaders/ui.vert.spv" UI_FRAG_SHADER_PATH="shaders/ui.frag.spv" MENU_VERT_SHADER_PATH="shaders/menu.vert.spv" MENU_FRAG_SHADER_PATH="shaders/menu.frag.spv" TEXTQUAD_VERT_SHADER_PATH="shaders/textquad.vert.spv" TEXTQUAD_FRAG_SHADER_PATH="shaders/textquad.frag.spv" ROUTING_COMP_SHADER_PATH="shaders/routing.comp.spv" MAIN_PATH_COMP_SHADER_PATH="shaders/main_path.comp.spv" BCGL_COMP_SHADER_PATH="shaders/bcgl.comp.spv" RAY_VERT_SHADER_PATH="shaders/ray.vert.spv" RAY_FRAG_SHADER_PATH="shaders/ray.frag.spv" PRESENT_VERT_SHADER_PATH="shaders/present.vert.spv" PRESENT_FRAG_SHADER_PATH="shaders/present.frag.spv")

# Standalone test for dynamic k-core maintenance (not installed)
enable_testing()
add_executable(dyn_kcore_test tests/dyn_kcore_test.c src/graph/dyn_k-core.c)
target_include_directories(dyn_kcore_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_kcore_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_kcore_test PRIVATE m)
endif()
add_test(NAME dyn_kcore_test COMMAND dyn_kcore_test)

# Dynamic k-core hierarchy tests.
add_executable(dyn_core_tree_test tests/dyn_core_tree_test.c src/graph/dyn_core_tree.c src/graph/dyn_k-core.c)
target_include_directories(dyn_core_tree_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_core_tree_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_core_tree_test PRIVATE m)
endif()
add_test(NAME dyn_core_tree_test COMMAND dyn_core_tree_test)

# Standalone test for the heuristic crossing-reduction rank maintainer paired
# with the k-core hierarchy tree (see include/graph/dyn_core_tree_order.h)
add_executable(dyn_core_tree_order_test tests/dyn_core_tree_order_test.c src/graph/dyn_core_tree_order.c src/graph/dyn_core_tree.c src/graph/dyn_k-core.c)
target_include_directories(dyn_core_tree_order_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_core_tree_order_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_core_tree_order_test PRIVATE m)
endif()
add_test(NAME dyn_core_tree_order_test COMMAND dyn_core_tree_order_test)

# Standalone test for dynamic Leiden community maintenance (not installed)
add_executable(dyn_leiden_test tests/dyn_leiden_test.c src/graph/dyn_leiden.c)
target_include_directories(dyn_leiden_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_leiden_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_leiden_test PRIVATE m)
endif()
add_test(NAME dyn_leiden_test COMMAND dyn_leiden_test)

# Standalone test for the layered-sphere layout's common helpers (not installed)
add_executable(layered_sphere_common_test tests/layered_sphere_common_test.c src/graph/layered_sphere_common.c)
target_include_directories(layered_sphere_common_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(layered_sphere_common_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(layered_sphere_common_test PRIVATE m)
endif()
add_test(NAME layered_sphere_common_test COMMAND layered_sphere_common_test)

# Standalone test for dynamic (streaming) Layered Sphere layout maintenance (not installed)
add_executable(dyn_layered_sphere_test tests/dyn_layered_sphere_test.c src/graph/dyn_layered_sphere.c src/graph/dyn_ls_sphere_rotation.c src/graph/layered_sphere_common.c src/graph/dyn_k-core.c src/graph/dyn_core_tree.c src/graph/dyn_core_tree_order.c src/graph/dyn_leiden.c)
target_include_directories(dyn_layered_sphere_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_layered_sphere_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_layered_sphere_test PRIVATE m)
endif()
add_test(NAME dyn_layered_sphere_test COMMAND dyn_layered_sphere_test)

# Standalone test for community SimHash (order-independent fingerprint + color)
add_executable(community_simhash_test tests/community_simhash_test.c src/graph/community_simhash.c)
target_include_directories(community_simhash_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(community_simhash_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(community_simhash_test PRIVATE m)
endif()
add_test(NAME community_simhash_test COMMAND community_simhash_test)

# Standalone test for NCOL line parsing (not installed)
add_executable(ncol_parse_test tests/ncol_parse_test.c src/graph/ncol_parse.c)
target_include_directories(ncol_parse_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(ncol_parse_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(ncol_parse_test PRIVATE m)
endif()
add_test(NAME ncol_parse_test COMMAND ncol_parse_test)

add_executable(rotor_routing_test tests/rotor_routing_test.c src/graph/rotor_routing.c)
target_include_directories(rotor_routing_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(rotor_routing_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(rotor_routing_test PRIVATE m)
endif()
add_test(NAME rotor_routing_test COMMAND rotor_routing_test)

# Standalone test for menu/camera spatial basis math (not installed)
add_executable(spatial_test tests/spatial_test.c src/interaction/spatial.c)
target_include_directories(spatial_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS} ${CGLM_INCLUDE_DIR})
target_link_libraries(spatial_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(spatial_test PRIVATE m)
endif()
add_test(NAME spatial_test COMMAND spatial_test)

# Standalone test for ray-picking geometry math (not installed)
add_executable(picking_test tests/picking_test.c src/interaction/picking.c)
target_include_directories(picking_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${Vulkan_INCLUDE_DIRS} ${CGLM_INCLUDE_DIR} ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(picking_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(picking_test PRIVATE m)
endif()
add_test(NAME picking_test COMMAND picking_test)

# Headless Vulkan test for shaders/main_path.comp (arXiv:2512.12355 §2.5).
# Compute-only, so it needs no window or surface — any Vulkan ICD will do,
# including the lavapipe software rasterizer.
add_executable(criticality_test tests/criticality_test.c tests/criticality_test_harness.c)
add_dependencies(criticality_test shaders)
target_include_directories(criticality_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(criticality_test PRIVATE CRITICALITY_TEST_SHADER_PATH="${CMAKE_BINARY_DIR}/shaders/main_path.comp.spv")
target_link_libraries(criticality_test PRIVATE ${Vulkan_LIBRARIES})
if(NOT MSVC)
	target_link_libraries(criticality_test PRIVATE m)
endif()
add_test(NAME criticality_test COMMAND criticality_test)

add_executable(main_path_cache_test tests/main_path_cache_test.c src/graph/main_path_cache.c)
target_include_directories(main_path_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(main_path_cache_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(main_path_cache_test PRIVATE m)
endif()
add_test(NAME main_path_cache_test COMMAND main_path_cache_test)

# Pure selection-logic tests for the Local/Backward-Local/Multiple/Key-Route/Network main-path
# variants (Liu & Lu 2012) -- CPU-only, no Vulkan/GPU dependency.
add_executable(main_path_search_test tests/main_path_search_test.c src/graph/main_path_search.c src/graph/main_path_cache.c)
target_include_directories(main_path_search_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(main_path_search_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(main_path_search_test PRIVATE m)
endif()
add_test(NAME main_path_search_test COMMAND main_path_search_test)

# Pure animation timing and presentation-strength helpers.
add_executable(renderer_anim_values_test tests/renderer_anim_values_test.c src/vulkan/renderer_anim_values.c)
target_include_directories(renderer_anim_values_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(NOT MSVC)
	target_link_libraries(renderer_anim_values_test PRIVATE m)
endif()
add_test(NAME renderer_anim_values_test COMMAND renderer_anim_values_test)

add_executable(surface_format_test tests/surface_format_test.c src/vulkan/surface_format.c)
target_include_directories(surface_format_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${Vulkan_INCLUDE_DIRS} ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(surface_format_test PRIVATE igraph::igraph)
add_test(NAME surface_format_test COMMAND surface_format_test)

add_executable(display_hdr_test tests/display_hdr_test.c src/interaction/display_hdr.c)
target_include_directories(display_hdr_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${Vulkan_INCLUDE_DIRS} ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(display_hdr_test PRIVATE igraph::igraph)
add_test(NAME display_hdr_test COMMAND display_hdr_test)

add_executable(color_space_test tests/color_space_test.c src/vulkan/color_space.c)
target_include_directories(color_space_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(color_space_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(color_space_test PRIVATE m)
endif()
add_test(NAME color_space_test COMMAND color_space_test)

add_executable(menu_scene_test tests/menu_scene_test.c src/vulkan/menu_scene.c src/interaction/menu_state.c src/ui/menu_metrics.c)
target_include_directories(menu_scene_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CGLM_INCLUDE_DIR} ${IGRAPH_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
target_link_libraries(menu_scene_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(menu_scene_test PRIVATE m)
endif()
add_test(NAME menu_scene_test COMMAND menu_scene_test)

install(TARGETS igraph-vlk DESTINATION bin)
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/ DESTINATION share/igraph-vlk/shaders FILES_MATCHING PATTERN "*.spv")
if(EXISTS ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt)
	install(FILES ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt DESTINATION share/igraph-vlk)
endif()
if(NOT WIN32 AND NOT APPLE)
	install(FILES ${CMAKE_SOURCE_DIR}/packaging/igraph-vlk.desktop DESTINATION share/applications)
	install(FILES ${CMAKE_SOURCE_DIR}/packaging/igraph-vlk.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(NOT CPACK_PACKAGE_VERSION)
		set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
	endif()
	set(CPACK_PACKAGE_NAME "igraph-vlk")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D network graph viewer with Vulkan rendering")
	set(CPACK_PACKAGE_VENDOR "leuc")
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "leuc")
	set(CPACK_DEBIAN_PACKAGE_SECTION "science")
	set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/leuc/igraph-vlk")
	set(_openmp_deb_pkg "")
	if(OpenMP_C_FOUND)
		if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
			set(_openmp_deb_pkg "libgomp1,")
		elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
			set(_openmp_deb_pkg "libomp5,")
		endif()
	endif()
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1, libglfw3, ${_openmp_deb_pkg} fonts-inconsolata, libzstd1")
	set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libopenxr-loader1")
	set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
	include(CPack)
endif()
