cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED BINARY OR NOT DEFINED APPDIR)
    message(FATAL_ERROR "BINARY and APPDIR must be set")
endif()

set(LIBDIR "${APPDIR}/usr/lib")
file(MAKE_DIRECTORY "${LIBDIR}")

execute_process(
    COMMAND ldd "${BINARY}"
    OUTPUT_VARIABLE LDD_OUTPUT
    ERROR_QUIET
)

string(REPLACE "\n" ";" LDD_LINES "${LDD_OUTPUT}")
foreach(LINE IN LISTS LDD_LINES)
    if(LINE MATCHES "=> (/[^ ]+)")
        set(LIB_PATH "${CMAKE_MATCH_1}")
        get_filename_component(LIB_NAME "${LIB_PATH}" NAME)
        if(LIB_NAME MATCHES "lib(igraph|glfw|cglm)")
            file(COPY "${LIB_PATH}" DESTINATION "${LIBDIR}")
            message(STATUS "Bundled: ${LIB_NAME}")
        endif()
    endif()
endforeach()
