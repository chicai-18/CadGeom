# The standalone demo: a console pass over the public surface, then a real
# window with a grid and an orbit camera.
#
# It links nothing but cadgeom - not even GLFW. The window belongs to the
# engine (SurfaceKind::Glfw), which is the whole point of that backend: a host
# gets a 3D view without taking on a windowing dependency of its own. Linking
# GLFW here as well would put two copies of its process-wide state in one
# process.

add_executable(glfw_viewer main.cpp)

target_link_libraries(glfw_viewer PRIVATE CadGeom::cadgeom)
target_compile_features(glfw_viewer PRIVATE cxx_std_20)

# So the demo can report whether the engine it loaded has a renderer in it.
# This says nothing about the Vulkan headers - the demo never includes them.
target_compile_definitions(glfw_viewer PRIVATE
    CADGEOM_HAS_VULKAN=$<BOOL:${CADGEOM_ENABLE_VULKAN}>)

if(MSVC)
    target_compile_options(glfw_viewer PRIVATE /W4 /utf-8)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(glfw_viewer PRIVATE /WX)
    endif()
else()
    target_compile_options(glfw_viewer PRIVATE -Wall -Wextra)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(glfw_viewer PRIVATE -Werror)
    endif()
endif()

set_target_properties(glfw_viewer PROPERTIES FOLDER "examples")

# 它链接的只有 cadgeom，而 GLFW 是静态进 DLL 的，所以这里没有额外运行时要带。
if(CADGEOM_INSTALL_EXAMPLES)
    install(TARGETS glfw_viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
