# Unit tests. Deliberately free of any Vulkan dependency so they stay runnable
# in CI and on a machine with no SDK installed (docs/architecture.md §8).
#
# Everything here goes through the public headers, exercising the DLL exactly
# the way a host would — the one exception is core/Math.h, which is header-only
# and has no linkage to speak of.

add_executable(cadgeom_tests
    test_abi.cpp
    test_math.cpp
    test_scene.cpp
    test_geometry.cpp
    test_extrude.cpp
    test_picking.cpp
    test_commands.cpp
    test_units.cpp
    test_io_registry.cpp
    test_io.cpp)

target_link_libraries(cadgeom_tests PRIVATE CadGeom::cadgeom Catch2::Catch2WithMain)
target_include_directories(cadgeom_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(cadgeom_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(cadgeom_tests PRIVATE /W4 /utf-8)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(cadgeom_tests PRIVATE /WX)
    endif()
else()
    target_compile_options(cadgeom_tests PRIVATE -Wall -Wextra)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(cadgeom_tests PRIVATE -Werror)
    endif()
endif()

set_target_properties(cadgeom_tests PROPERTIES FOLDER "tests")

add_test(NAME cadgeom_tests COMMAND cadgeom_tests)
