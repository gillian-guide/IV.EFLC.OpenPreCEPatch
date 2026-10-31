cmake_minimum_required(VERSION 3.31)

project(OpenPreCEPatch
    VERSION 1.0.0
    DESCRIPTION "OpenPreCEPatch - GTA IV / EFLC quality-of-life and crash-fix plugin"
    LANGUAGES CXX)

# The shipped ASI is 32-bit. The 64-bit configure exists only for the write-set
# parity dump, which maps the game image without executing any hook body.
if(NOT MSVC)
    message(FATAL_ERROR
        "OpenPreCEPatch builds with MSVC (Visual Studio 2022 17.10+ or newer). "
        "Configure with -A Win32 for the shipped ASI.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(OPCEP_BUILD_TESTS "Build the native test suite" ON)
option(OPCEP_BUILD_DUMP "Build the 32-bit write-set dump tool" ON)
option(OPCEP_BUILD_PARITY_DUMP "Build the 64-bit parity dump tool" ON)

# The shipped ASI and its INI land in one folder next to GTAIV.exe. Tools stay
# under the build tree.
set(OPCEP_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin" CACHE PATH
    "Directory for the shipped OpenPreCEPatch output")

set(OPCEP_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

# One file per subsystem; the .ixx files are C++23 module interfaces exposed by
# the core library through a CXX_MODULES file set.
set(OPCEP_MODULES
    source/common.ixx
    source/log.ixx
    source/patch.ixx
    source/pipeline.ixx
    source/config.ixx
    source/data/model_catalogue.ixx
    source/steam.ixx
    source/game.ixx
    source/settings.ixx
    source/stability.ixx
    source/performance.ixx
    source/graphics.ixx
    source/vehicles.ixx
    source/input.ixx
    source/multiplayer.ixx
    source/loading.ixx
    source/compatibility.ixx
    source/interface.ixx
)

set(OPCEP_CORE_SOURCES
    source/hooks.cpp
    external/hooking/Hooking.Patterns.cpp
)

# Shared compiler contract for every target in this tree.
function(opcep_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_compile_options(${target} PUBLIC
        /Zc:__cplusplus
        /utf-8
        /permissive-
        /W4
        /external:W0)
    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/source")
    target_include_directories(${target} SYSTEM PUBLIC
        "${OPCEP_EXTERNAL_DIR}/injector/include"
        "${OPCEP_EXTERNAL_DIR}/hooking"
        "${OPCEP_EXTERNAL_DIR}/inireader")
    target_compile_definitions(${target} PUBLIC
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
endfunction()

# ---------------------------------------------------------------------------
# 32-bit: the shipped ASI, its tests and the write-set dump.
# ---------------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(opcep_core STATIC ${OPCEP_CORE_SOURCES})
    target_sources(opcep_core PUBLIC FILE_SET CXX_MODULES FILES ${OPCEP_MODULES})
    opcep_configure_target(opcep_core)

    add_library(OpenPreCEPatch SHARED source/dllmain.cpp)
    opcep_configure_target(OpenPreCEPatch)
    target_link_libraries(OpenPreCEPatch PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,opcep_core>"
        version user32 shlwapi)
    target_link_options(OpenPreCEPatch PRIVATE
        /MACHINE:X86
        /SUBSYSTEM:WINDOWS
        /BASE:0x10000000
        /INCREMENTAL:NO
        /MAP)
    set_target_properties(OpenPreCEPatch PROPERTIES
        PREFIX ""
        OUTPUT_NAME "IV.EFLC.OpenPreCEPatch"
        SUFFIX ".asi"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OPCEP_DIST_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_command(TARGET OpenPreCEPatch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/plugins/IV.EFLC.OpenPreCEPatch.ini"
                "${OPCEP_DIST_DIR}/IV.EFLC.OpenPreCEPatch.ini"
        COMMENT "Copying IV.EFLC.OpenPreCEPatch.ini to ${OPCEP_DIST_DIR}")

    if(OPCEP_BUILD_DUMP)
        add_executable(opcep_dump tests/dump_main.cpp)
        opcep_configure_target(opcep_dump)
        target_compile_definitions(opcep_dump PRIVATE OPCEP_DUMP_TOOL)
        target_link_libraries(opcep_dump PRIVATE
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,opcep_core>"
            version user32 shlwapi)
        target_link_options(opcep_dump PRIVATE
            /MACHINE:X86
            /SUBSYSTEM:CONSOLE
            /BASE:0x60000000)
    endif()

    if(OPCEP_BUILD_TESTS)
        enable_testing()
        add_executable(opcep_tests tests/test_main.cpp)
        opcep_configure_target(opcep_tests)
        target_compile_definitions(opcep_tests PRIVATE
            OPCEP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/plugins"
            OPCEP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(opcep_tests PRIVATE
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,opcep_core>"
            version user32 shlwapi)
        target_link_options(opcep_tests PRIVATE
            /MACHINE:X86
            /SUBSYSTEM:CONSOLE
            /BASE:0x60000000)
        add_test(NAME opcep_tests COMMAND opcep_tests)

        # Every option's absent-key default must equal the shipped INI value, so
        # deleting a line behaves like the shipped configuration. The table is
        # regenerated from the call sites and compared here; no game images are
        # needed, so this runs in CI as well.
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME opcep_option_defaults
                COMMAND ${Python3_EXECUTABLE}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_option_defaults.py"
                        --check
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()

        # The address-site checker needs the on-disk game images (game_refs, not
        # part of this repository), so it is only registered where they exist;
        # CI without them must not report a false failure.
        if(Python3_Interpreter_FOUND
           AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/address_sites.tsv"
           AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe")
            add_test(NAME opcep_sigcheck
                COMMAND ${Python3_EXECUTABLE}
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/opcep_sigcheck.py"
                        --candidates "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/address_sites.tsv"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endif()

    install(TARGETS OpenPreCEPatch RUNTIME DESTINATION .)
    install(FILES data/plugins/IV.EFLC.OpenPreCEPatch.ini DESTINATION .)
else()
    # -----------------------------------------------------------------------
    # 64-bit: only the parity dump. hooks.cpp is replaced by
    # tests/dump_stubs.cpp so no 32-bit body is ever executed.
    # -----------------------------------------------------------------------
    if(OPCEP_BUILD_PARITY_DUMP)
        set(OPCEP_DUMP_SOURCES ${OPCEP_CORE_SOURCES})
        list(REMOVE_ITEM OPCEP_DUMP_SOURCES source/hooks.cpp)

        add_executable(opcep_parity_dump
            tests/dump_main.cpp
            tests/dump_stubs.cpp
            ${OPCEP_DUMP_SOURCES})
        opcep_configure_target(opcep_parity_dump)
        target_compile_definitions(opcep_parity_dump PRIVATE OPCEP_DUMP_TOOL)
        target_sources(opcep_parity_dump PRIVATE
            FILE_SET CXX_MODULES FILES ${OPCEP_MODULES})
        target_link_libraries(opcep_parity_dump PRIVATE version user32 shlwapi)
        target_link_options(opcep_parity_dump PRIVATE
            /MACHINE:X64
            /SUBSYSTEM:CONSOLE)
    endif()
endif()

message(STATUS "OpenPreCEPatch ${PROJECT_VERSION}: C++${CMAKE_CXX_STANDARD}, "
               "${CMAKE_GENERATOR} platform ${CMAKE_VS_PLATFORM_NAME}")
message(STATUS "Vendor submodules: ${OPCEP_EXTERNAL_DIR}")
message(STATUS "Dist output: ${OPCEP_DIST_DIR}")
