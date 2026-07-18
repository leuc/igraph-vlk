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

# falling back to pkg-config
if(NOT zstd_FOUND)
    # cmake_pkg_config doesn't exist on cmake 3.22 Ubuntu 22.04
    find_package(PkgConfig QUIET)
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

set(SHADERS shaders/node.vert shaders/node.frag shaders/edge.vert shaders/edge.frag shaders/label.vert shaders/label.frag shaders/ui.vert shaders/ui.frag shaders/menu.vert shaders/menu.frag shaders/textquad.vert shaders/textquad.frag shaders/routing.comp shaders/splc.comp shaders/bcgl.comp shaders/ray.vert shaders/ray.frag)

foreach(shader ${SHADERS})
	get_filename_component(shader_name ${shader} NAME)
	set(_spirv "${CMAKE_BINARY_DIR}/shaders/${shader_name}.spv")
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
	add_custom_command(OUTPUT ${_spirv} COMMAND ${GLSL_COMPILER} -V ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${_spirv} DEPENDS ${shader} COMMENT "Compiling ${shader_name} to SPIR-V")
	list(APPEND SPIRV_SHADERS ${_spirv})
endforeach()

add_executable(
	igraph-vlk
	src/main.c
	# Renderer core
	src/vulkan/renderer_lifecycle.c
	src/vulkan/renderer_draw.c
	src/vulkan/renderer_camera.c
	src/vulkan/renderer_anim.c
	src/vulkan/renderer_labels.c
	src/vulkan/renderer_geometry.c
	src/vulkan/renderer_compute.c
	src/vulkan/renderer_ui.c
	src/vulkan/renderer_pipelines.c
	src/vulkan/pipeline_graphics.c
	src/vulkan/pipeline_ui.c
	src/vulkan/pipeline_compute.c
	src/vulkan/device.c
	src/vulkan/swapchain.c
	src/vulkan/commands.c
	src/vulkan/render_pass.c
	src/vulkan/menu.c
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
	src/vulkan/renderer_init_splc_buffers.c
	src/vulkan/renderer_bcgl.c
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
	src/graph/community_simhash.c
	src/graph/graph_filter.c
	src/graph/graph_filter_visibility.c
	src/graph/layered_sphere.c
	src/graph/layered_sphere_common.c
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
	src/graph/wrappers_paths.c
	src/graph/wrappers_splc.c
	src/graph/wrappers_structural.c
	src/graph/repo.c
	src/graph/repo_netzschleuder.c
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
	src/interaction/window.c
	# UI modules
	src/ui/hud.c
	src/ui/menu.c
	${SPIRV_SHADERS})

target_include_directories(igraph-vlk PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/graph ${Vulkan_INCLUDE_DIRS} ${CGLM_INCLUDE_DIR} ${IGRAPH_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})

target_link_libraries(igraph-vlk PRIVATE Vulkan::Vulkan glfw igraph::igraph CURL::libcurl zstd::libzstd)
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

target_compile_definitions(igraph-vlk PRIVATE NODE_VERT_SHADER_PATH="shaders/node.vert.spv" NODE_FRAG_SHADER_PATH="shaders/node.frag.spv" EDGE_VERT_SHADER_PATH="shaders/edge.vert.spv" EDGE_FRAG_SHADER_PATH="shaders/edge.frag.spv" LABEL_VERT_SHADER_PATH="shaders/label.vert.spv" LABEL_FRAG_SHADER_PATH="shaders/label.frag.spv" UI_VERT_SHADER_PATH="shaders/ui.vert.spv" UI_FRAG_SHADER_PATH="shaders/ui.frag.spv" MENU_VERT_SHADER_PATH="shaders/menu.vert.spv" MENU_FRAG_SHADER_PATH="shaders/menu.frag.spv" TEXTQUAD_VERT_SHADER_PATH="shaders/textquad.vert.spv" TEXTQUAD_FRAG_SHADER_PATH="shaders/textquad.frag.spv" ROUTING_COMP_SHADER_PATH="shaders/routing.comp.spv" SPLC_COMP_SHADER_PATH="shaders/splc.comp.spv" BCGL_COMP_SHADER_PATH="shaders/bcgl.comp.spv" RAY_VERT_SHADER_PATH="shaders/ray.vert.spv" RAY_FRAG_SHADER_PATH="shaders/ray.frag.spv")

# Standalone test for dynamic k-core maintenance (not installed)
enable_testing()
add_executable(dyn_kcore_test tests/dyn_kcore_test.c src/graph/dyn_k-core.c)
target_include_directories(dyn_kcore_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests ${IGRAPH_INCLUDE_DIRS})
target_link_libraries(dyn_kcore_test PRIVATE igraph::igraph)
if(NOT MSVC)
	target_link_libraries(dyn_kcore_test PRIVATE m)
endif()
add_test(NAME dyn_kcore_test COMMAND dyn_kcore_test)

# Standalone test for dynamic k-core HIERARCHY maintenance (not installed;
# not yet wired into any layout — see include/graph/dyn_core_tree.h)
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
add_executable(dyn_layered_sphere_test tests/dyn_layered_sphere_test.c src/graph/dyn_layered_sphere.c src/graph/layered_sphere_common.c src/graph/dyn_k-core.c src/graph/dyn_core_tree.c src/graph/dyn_core_tree_order.c src/graph/dyn_leiden.c)
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
