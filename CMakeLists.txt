cmake_minimum_required(VERSION 3.28)
project(OpenPreCEPatch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Build the folded improvements' original branches instead. Used by the parity
# matrix to compare an improved dump/ASI against the baseline without an INI.
option(OPCEP_PARITY_BASELINE "Force every improvement group off (legacy path)" OFF)

# Both shipped ASIs and their INIs are emitted into one folder. The tests and
# dump tools stay under the build tree.
set(OPCEP_DIST_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Directory for the combined OpenPreCEPatch + XLivelessAddon output")

set(OPCEP_SOURCES
    source/catalogue.cpp
    source/common.ixx
    source/settings.ixx
    source/config.cpp
    source/feature_registry.cpp
    source/fusionfix_compat.cpp
    source/game.cpp
    source/helpers.cpp
    source/hooks.cpp
    source/improvements.cpp
    source/ini_import.cpp
    source/log.cpp
    source/menu.cpp
    source/patches.cpp
    source/pattern.cpp
    source/steam_achievements.cpp
    source/preamble.cpp
    source/rebased.cpp
    source/vehicle_lights.cpp
    source/stability.ixx
    source/performance.ixx
    source/graphics.ixx
    source/vehicles.ixx
    source/input.ixx
    source/multiplayer.ixx
    source/loading.ixx
    source/compatibility.ixx
    source/interface.ixx
    source/data/model_catalogue.cpp
    source/data/useless_checks.cpp
    external/hooking/Hooking.Patterns.cpp
)

add_library(opcep_core STATIC ${OPCEP_SOURCES})
set_target_properties(opcep_core PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_include_directories(opcep_core PUBLIC
    source
    external/injector/include
    external/hooking
    external/inireader
)
target_compile_definitions(opcep_core PUBLIC UNICODE _UNICODE)
if(OPCEP_PARITY_BASELINE)
    target_compile_definitions(opcep_core PUBLIC OPCEP_PARITY_BASELINE)
endif()

add_library(OpenPreCEPatch SHARED source/dllmain.cpp)
target_link_libraries(OpenPreCEPatch PRIVATE opcep_core version user32 shlwapi)
target_link_options(OpenPreCEPatch PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:opcep_core>)
set_target_properties(OpenPreCEPatch PROPERTIES
    PREFIX ""
    OUTPUT_NAME "IV.EFLC.OpenPreCEPatch"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OPCEP_DIST_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OPCEP_DIST_DIR}"
)
target_link_options(OpenPreCEPatch PRIVATE
    /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO /MAP
)
add_custom_command(TARGET OpenPreCEPatch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/data/plugins/IV.EFLC.OpenPreCEPatch.ini"
            "${OPCEP_DIST_DIR}/IV.EFLC.OpenPreCEPatch.ini"
    COMMENT "Copying OpenPreCEPatch.ini to the combined output folder")

# XLivelessAddon successor lives in its own subproject (xlivelessaddon/) and is
# not part of the OpenPreCEPatch ASI.
add_subdirectory(xlivelessaddon)

add_executable(opcep_dump tests/dump_main.cpp)
target_link_libraries(opcep_dump PRIVATE opcep_core version user32 shlwapi)
target_link_options(opcep_dump PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:opcep_core>)
target_link_options(opcep_dump PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE /BASE:0x60000000)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
# 64-bit write-set dump: the same installer pass over a mapped copy of the game,
# with the 32-bit body registry stubbed out. See tests/dump_stubs.cpp.
add_executable(opcep_parity_dump
    tests/dump_main.cpp
    tests/dump_stubs.cpp
    source/catalogue.cpp
    source/common.ixx
    source/settings.ixx
    source/config.cpp
    source/feature_registry.cpp
    source/fusionfix_compat.cpp
    source/game.cpp
    source/helpers.cpp
    source/improvements.cpp
    source/ini_import.cpp
    source/log.cpp
    source/patches.cpp
    source/pattern.cpp
    source/steam_achievements.cpp
    source/preamble.cpp
    source/rebased.cpp
    source/vehicle_lights.cpp
    source/stability.ixx
    source/performance.ixx
    source/graphics.ixx
    source/vehicles.ixx
    source/input.ixx
    source/multiplayer.ixx
    source/loading.ixx
    source/compatibility.ixx
    source/interface.ixx
    source/data/model_catalogue.cpp
    source/data/useless_checks.cpp
    external/hooking/Hooking.Patterns.cpp
)
target_include_directories(opcep_parity_dump PRIVATE
    tests/dump_injector
    source
    external/hooking
    external/inireader
    external/injector/include
)
set_target_properties(opcep_parity_dump PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_definitions(opcep_parity_dump PRIVATE OPCEP_DUMP_TOOL UNICODE _UNICODE)
if(OPCEP_PARITY_BASELINE)
    target_compile_definitions(opcep_parity_dump PRIVATE OPCEP_PARITY_BASELINE)
endif()
target_link_libraries(opcep_parity_dump PRIVATE version user32 shlwapi)
endif()

add_executable(opcep_tests tests/test_main.cpp)
target_link_libraries(opcep_tests PRIVATE opcep_core version user32 shlwapi)
target_link_options(opcep_tests PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:opcep_core>)
target_link_options(opcep_tests PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE /BASE:0x60000000)
enable_testing()
add_test(NAME opcep_tests COMMAND opcep_tests)

# XLivelessAddon successor: the recovered SkipLauncher path is 32-bit only, so
# its native test shares the Win32 build. It links the fork's sources directly
# instead of the ASI (which would pull in DllMain).
add_executable(xla_tests
    xlivelessaddon/tests/test_xla.cpp
    xlivelessaddon/source/skip_launcher.cpp
    external/hooking/Hooking.Patterns.cpp
)
target_include_directories(xla_tests PRIVATE
    xlivelessaddon/source
    external/hooking
    external/injector/include
    external/injector/safetyhook/include
)
target_compile_definitions(xla_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xla_tests PRIVATE user32)
target_link_options(xla_tests PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE)
add_test(NAME xla_tests COMMAND xla_tests)
