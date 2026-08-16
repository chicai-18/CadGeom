cmake_minimum_required(VERSION 3.24)

project(CadGeom
    VERSION 0.1.0
    DESCRIPTION "Embeddable Vulkan CAD 3D engine"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(CadGeomShaders)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(CADGEOM_BUILD_EXAMPLES "Build the standalone viewer demo" ON)
option(CADGEOM_BUILD_TESTS    "Build the unit tests" ON)
option(CADGEOM_ENABLE_VULKAN  "Build the Vulkan renderer (needs the Vulkan SDK)" ON)
option(CADGEOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    # Keep the DLL beside every executable that loads it, so tests and the demo
    # run straight out of the build tree without a PATH dance.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
#
# Everything is vendored under external/ as a pinned git submodule; the only
# thing expected from the machine is the Vulkan SDK. Each submodule is wired
# into the build at the milestone that first needs it, so an early build stays
# fast instead of compiling five libraries nobody calls yet.
#
#   external/glfw                  M0 demo window backend, M1 GlfwSurface
#   external/Catch2                M0 tests
#   external/glm                   M1 shader-side float math
#   external/VulkanMemoryAllocator M1 device memory
#   external/tinyobjloader         M5 OBJ import
#   external/tinygltf              M5 glTF import/export
#   external/imgui                 M6 demo UI panels (never in the core library)
# ---------------------------------------------------------------------------

function(cadgeom_require_submodule name)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/${name}/CMakeLists.txt")
        message(FATAL_ERROR
            "Submodule external/${name} is missing or empty.\n"
            "Run:  git submodule update --init --recursive")
    endif()
endfunction()

if(CADGEOM_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING
            "Vulkan SDK not found - building without the renderer.\n"
            "  Milestone M1 onward needs LunarG Vulkan SDK >= 1.3.275 with VULKAN_SDK set.\n"
            "  M0 (engine skeleton, scene, tests) builds and runs fine without it.")
        set(CADGEOM_ENABLE_VULKAN OFF)
    elseif(NOT Vulkan_GLSLC_EXECUTABLE)
        # The renderer's shaders are compiled into the DLL, so no glslc means no
        # renderer. A driver-provided vulkan-1.dll looks like a Vulkan install
        # and lands here; saying so beats failing later with a missing header.
        message(WARNING
            "Found the Vulkan loader but not glslc - building without the renderer.\n"
            "  Install the full LunarG SDK; the runtime alone does not include the compiler.")
        set(CADGEOM_ENABLE_VULKAN OFF)
    endif()
endif()

if(CADGEOM_ENABLE_VULKAN)
    # The renderer's own dependencies. GLFW is in here rather than under the
    # examples because the engine ships a GLFW-backed ISurface: a host that
    # wants a window without writing platform code asks for SurfaceKind::Glfw
    # and the engine owns the window and its message loop
    # (docs/architecture.md §4.4).
    cadgeom_require_submodule(glfw)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw EXCLUDE_FROM_ALL)
    # Static library going into a shared one.
    set_property(TARGET glfw PROPERTY POSITION_INDEPENDENT_CODE ON)

    cadgeom_require_submodule(VulkanMemoryAllocator)
    add_subdirectory(external/VulkanMemoryAllocator EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# cadgeom - the one shipped artifact
# ---------------------------------------------------------------------------

set(CADGEOM_PUBLIC_HEADERS
    include/cadgeom/CadGeom.h
    include/cadgeom/CadGeomRAII.h
    include/cadgeom/ICamera.h
    include/cadgeom/ICommandStack.h
    include/cadgeom/IEngine.h
    include/cadgeom/IEntity.h
    include/cadgeom/IGeometryBuilder.h
    include/cadgeom/IImportExport.h
    include/cadgeom/IScene.h
    include/cadgeom/ISelection.h
    include/cadgeom/ITool.h
    include/cadgeom/IViewport.h
    include/cadgeom/Types.h
    include/cadgeom/Version.h)

set(CADGEOM_SOURCES
    src/core/Error.cpp
    src/core/Log.cpp
    src/core/ObjectTracker.cpp
    src/core/Png.cpp
    src/geom/MeshData.cpp
    src/interact/OrbitCamera.cpp
    src/api/CommandStackImpl.cpp
    src/api/EngineImpl.cpp
    src/api/EntityImpl.cpp
    src/api/Exports.cpp
    src/api/GeometryBuilderImpl.cpp
    src/api/IoRegistryImpl.cpp
    src/api/SceneImpl.cpp
    src/api/SelectionImpl.cpp
    src/api/ToolManagerImpl.cpp)

# The renderer and everything that can only exist alongside it. Without the
# Vulkan SDK these drop out and CreateViewport reports why.
set(CADGEOM_RENDER_SOURCES
    src/api/ViewportImpl.cpp
    src/render/FrameData.cpp
    src/render/GpuScene.cpp
    src/render/Renderer.cpp
    src/render/RenderSystem.cpp
    src/render/Surface.cpp
    src/render/pass/GridPass.cpp
    src/render/pass/MeshPass.cpp
    src/render/vk/Common.cpp
    src/render/vk/Context.cpp
    src/render/vk/Pipeline.cpp
    src/render/vk/Resources.cpp
    src/render/vk/Swapchain.cpp
    src/render/vk/VmaImpl.cpp)

set(CADGEOM_SHADERS
    shaders/grid.vert
    shaders/grid.frag
    shaders/mesh.vert
    shaders/mesh.frag)

if(CADGEOM_ENABLE_VULKAN)
    list(APPEND CADGEOM_SOURCES ${CADGEOM_RENDER_SOURCES})
endif()

add_library(cadgeom SHARED ${CADGEOM_SOURCES} ${CADGEOM_PUBLIC_HEADERS})
add_library(CadGeom::cadgeom ALIAS cadgeom)

generate_export_header(cadgeom
    BASE_NAME CADGEOM
    EXPORT_MACRO_NAME CADGEOM_API
    NO_EXPORT_MACRO_NAME CADGEOM_LOCAL
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/cadgeom/Export.h")

target_include_directories(cadgeom
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# C++20 inside; hosts only need C++17 to consume the headers, and nothing in
# the public surface requires more than aggregate initializers.
target_compile_features(cadgeom PRIVATE cxx_std_20 INTERFACE cxx_std_17)

set_target_properties(cadgeom PROPERTIES
    CXX_VISIBILITY_PRESET hidden        # export only what CADGEOM_API marks
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME cadgeom)

if(MSVC)
    target_compile_options(cadgeom PRIVATE /W4 /utf-8 /permissive-)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(cadgeom PRIVATE /WX)
    endif()
else()
    target_compile_options(cadgeom PRIVATE -Wall -Wextra -Wpedantic)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(cadgeom PRIVATE -Werror)
    endif()
endif()

if(CADGEOM_ENABLE_VULKAN)
    target_link_libraries(cadgeom PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glfw)
    target_compile_definitions(cadgeom PRIVATE CADGEOM_HAS_VULKAN=1)

    # GLSL -> SPIR-V -> uint32_t array compiled into the DLL, so a deployed
    # cadgeom.dll has no shader files to lose (docs/architecture.md §4.5).
    cadgeom_add_shaders(cadgeom
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        SOURCES    ${CADGEOM_SHADERS}
        INCLUDES   shaders/Common.glsl)

else()
    target_compile_definitions(cadgeom PRIVATE CADGEOM_HAS_VULKAN=0)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CADGEOM_SOURCES} ${CADGEOM_PUBLIC_HEADERS})

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------

if(CADGEOM_BUILD_EXAMPLES)
    add_subdirectory(examples/glfw_viewer)
endif()

if(CADGEOM_BUILD_TESTS)
    cadgeom_require_submodule(Catch2)
    set(CATCH_INSTALL_DOCS     OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS   OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install - a host consumes this with a single find_package(CadGeom)
# ---------------------------------------------------------------------------

install(TARGETS cadgeom
    EXPORT CadGeomTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CADGEOM_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cadgeom)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/cadgeom/Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cadgeom)

install(EXPORT CadGeomTargets
    FILE CadGeomTargets.cmake
    NAMESPACE CadGeom::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CadGeom)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CadGeomConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CadGeomConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CadGeom)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CadGeomConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CadGeomConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CadGeomConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CadGeom)

# ---------------------------------------------------------------------------

message(STATUS "CadGeom ${PROJECT_VERSION}")
message(STATUS "  Vulkan renderer : ${CADGEOM_ENABLE_VULKAN}")
message(STATUS "  Examples        : ${CADGEOM_BUILD_EXAMPLES}")
message(STATUS "  Tests           : ${CADGEOM_BUILD_TESTS}")
