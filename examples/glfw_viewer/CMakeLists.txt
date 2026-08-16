# The standalone demo. At M0 it is a console harness that exercises the whole
# public surface and reports which milestone each unimplemented area is waiting
# on; M1 turns it into an actual window with a grid and an orbit camera, which
# is why it already links GLFW.

add_executable(glfw_viewer main.cpp)

target_link_libraries(glfw_viewer PRIVATE CadGeom::cadgeom glfw)
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
