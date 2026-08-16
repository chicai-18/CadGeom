# GLSL -> SPIR-V -> C array, compiled into the library
# (docs/architecture.md §4.5).
#
#   cadgeom_add_shaders(cadgeom
#       OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
#       SOURCES    shaders/grid.vert shaders/grid.frag
#       INCLUDES   shaders/Common.glsl)
#
# Every shader becomes ${OUTPUT_DIR}/shaders/<name>.spv.h holding a
# `static const uint32_t <name>_spv[]`, and OUTPUT_DIR goes on the target's
# include path, so a pass writes `#include "shaders/grid.vert.spv.h"`.
#
# INCLUDES lists files pulled in with #include: glslc does not report its
# dependencies, so editing a shared .glsl only triggers a rebuild because it is
# named here.

set(CADGEOM_BIN2C_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/bin2c.cmake")

function(cadgeom_add_shaders target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES;INCLUDES" ${ARGN})

    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR
            "cadgeom_add_shaders(${target}): glslc was not found.\n"
            "Install the full LunarG Vulkan SDK - the runtime alone does not include it.")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "cadgeom_add_shaders(${target}): no SOURCES given")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()

    set(absoluteIncludes "")
    foreach(include IN LISTS ARG_INCLUDES)
        get_filename_component(absolute "${include}" ABSOLUTE)
        list(APPEND absoluteIncludes "${absolute}")
    endforeach()

    set(generatedHeaders "")
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(absoluteSource "${source}" ABSOLUTE)
        get_filename_component(sourceDir "${absoluteSource}" DIRECTORY)
        get_filename_component(name "${source}" NAME)
        string(MAKE_C_IDENTIFIER "${name}_spv" symbol)

        set(spirv "${ARG_OUTPUT_DIR}/shaders/${name}.spv")
        set(header "${ARG_OUTPUT_DIR}/shaders/${name}.spv.h")

        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}/shaders"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
                    --target-env=vulkan1.3 -O
                    -I "${sourceDir}"
                    -o "${spirv}" "${absoluteSource}"
            DEPENDS "${absoluteSource}" ${absoluteIncludes}
            COMMENT "Compiling shader ${name}"
            VERBATIM)

        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DINPUT=${spirv}" "-DOUTPUT=${header}" "-DSYMBOL=${symbol}"
                    -P "${CADGEOM_BIN2C_SCRIPT}"
            DEPENDS "${spirv}" "${CADGEOM_BIN2C_SCRIPT}"
            COMMENT "Embedding ${name}.spv"
            VERBATIM)

        list(APPEND generatedHeaders "${header}")
    endforeach()

    # A separate target rather than target_sources on ${target}: the generated
    # headers are included, never compiled, so nothing in ${target} would depend
    # on them and the build order would be a race.
    add_custom_target(${target}_shaders
        DEPENDS ${generatedHeaders}
        SOURCES ${ARG_SOURCES} ${ARG_INCLUDES})
    set_target_properties(${target}_shaders PROPERTIES FOLDER "shaders")
    add_dependencies(${target} ${target}_shaders)

    target_include_directories(${target} PRIVATE "${ARG_OUTPUT_DIR}")
endfunction()
