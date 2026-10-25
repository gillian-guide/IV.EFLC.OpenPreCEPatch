cmake_minimum_required(VERSION 3.28)
project(OpenPreCEPatch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    source/ini_import.cpp
    source/log.cpp
    source/menu.cpp
    source/patches.cpp
    source/pattern.cpp
    source/skip_launcher.cpp
    source/preamble.cpp
    source/rebased.cpp
    source/skip_launcher.cpp
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
    source/xliveless_features.ixx
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

add_library(OpenPreCEPatch SHARED source/dllmain.cpp)
target_link_libraries(OpenPreCEPatch PRIVATE opcep_core version user32 shlwapi)
target_link_options(OpenPreCEPatch PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:opcep_core>)
set_target_properties(OpenPreCEPatch PROPERTIES
    PREFIX ""
    OUTPUT_NAME "IV.EFLC.OpenPreCEPatch"
    SUFFIX ".asi"
)
target_link_options(OpenPreCEPatch PRIVATE
    /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO /MAP
)

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
    source/ini_import.cpp
    source/log.cpp
    source/patches.cpp
    source/pattern.cpp
    source/skip_launcher.cpp
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
    source/xliveless_features.ixx
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
target_link_libraries(opcep_parity_dump PRIVATE version user32 shlwapi)
endif()

# Inert XLivelessAddon marker: satisfy FusionFix's module-presence check without
# loading the mod or patching anything. The main module recognises the marker
# through the OpcepCompatShim export.
add_library(XLivelessAddon SHARED source/compat/xliveless_proxy.cpp)
set_target_properties(XLivelessAddon PROPERTIES
    PREFIX ""
    OUTPUT_NAME "XLivelessAddon"
    SUFFIX ".asi"
)
target_link_options(XLivelessAddon PRIVATE /MACHINE:X86 /SUBSYSTEM:WINDOWS /INCREMENTAL:NO)

add_executable(opcep_tests tests/test_main.cpp)
target_link_libraries(opcep_tests PRIVATE opcep_core version user32 shlwapi)
target_link_options(opcep_tests PRIVATE /WHOLEARCHIVE:$<TARGET_FILE:opcep_core>)
target_link_options(opcep_tests PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE /BASE:0x60000000)
enable_testing()
add_test(NAME opcep_tests COMMAND opcep_tests)
