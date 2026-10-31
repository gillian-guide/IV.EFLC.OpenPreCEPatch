cmake_minimum_required(VERSION 3.15)
project(ZolikaPatch LANGUAGES CXX)

# Static CRT, like the shipped ASI: the mod runs inside the game process, and the
# CRT calls its bodies make must be its own code, not an import the game owns.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(ZP_BUILD_ASI "Build the 32-bit ZolikaPatch.asi" ON)
option(ZP_BUILD_TESTS "Build the native tests" ON)
option(ZP_BUILD_DUMP "Build the write-set dump tool" OFF)
option(ZP_BUILD_STATE_DUMP "Build the offline Runtime State driver (Gate 3)" OFF)

# Third-party headers and the scanner are git submodules (see .gitmodules),
# checked out under include/.
set(ZP_INC_INJECTOR ${CMAKE_CURRENT_SOURCE_DIR}/include/injector/include)
set(ZP_INC_PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/include/Hooking.Patterns)
foreach(dir ${ZP_INC_INJECTOR} ${ZP_INC_PATTERNS})
  if(NOT EXISTS ${dir})
    message(FATAL_ERROR
      "${dir} is missing. Check out the submodules first:\n"
      "  git submodule update --init --recursive")
  endif()
endforeach()

set(ZP_SOURCES
  ${ZP_INC_PATTERNS}/Hooking.Patterns.cpp
  src/catalogue_data.cpp
  src/patch_data.cpp
  src/xlive/xliveless.cpp
)

add_library(zp STATIC ${ZP_SOURCES})
target_include_directories(zp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                     ${ZP_INC_INJECTOR}
                                     ${ZP_INC_PATTERNS})
target_compile_features(zp PUBLIC cxx_std_17)
target_compile_definitions(zp PUBLIC UNICODE _UNICODE NOMINMAX)
if(WIN32)
  target_link_libraries(zp PUBLIC version user32 shlwapi)
endif()
set_target_properties(zp PROPERTIES CXX_EXTENSIONS OFF)

if(ZP_BUILD_ASI)
  if(NOT WIN32)
    message(FATAL_ERROR "ZP_BUILD_ASI requires a Windows toolchain")
  endif()
  add_library(ZolikaPatch SHARED src/dllmain.cpp ZolikaPatch.def)
  target_link_libraries(ZolikaPatch PRIVATE zp)
  target_link_options(ZolikaPatch PRIVATE
    /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO /MAP)
  set_target_properties(ZolikaPatch PROPERTIES PREFIX "" SUFFIX ".asi")
endif()

# Write-set dump: applies the installers to a mapped copy of the game image so
# the result can be diffed against the reference implementation. 64-bit so the
# image can be mapped at its preferred base 0x400000.
if(ZP_BUILD_DUMP)
  add_executable(zp_dump tests/dump_main.cpp
                 ${ZP_INC_PATTERNS}/Hooking.Patterns.cpp src/catalogue_data.cpp src/dllmain.cpp
                 src/patch_data.cpp)
  target_link_libraries(zp_dump PRIVATE version user32 shlwapi)
  target_compile_features(zp_dump PRIVATE cxx_std_17)
  target_link_options(zp_dump PRIVATE /MAP)
  # The dump uses a 64-bit-safe injector shim (the real one is 32-bit only).
  target_include_directories(zp_dump BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/dump_injector)
  target_include_directories(zp_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                             ${ZP_INC_INJECTOR}
                                             ${ZP_INC_PATTERNS})
  target_compile_definitions(zp_dump PRIVATE UNICODE _UNICODE NOMINMAX ZP_DUMP_TOOL)
endif()

if(ZP_BUILD_TESTS)
  enable_testing()
  add_executable(zp_tests tests/test_main.cpp src/dllmain.cpp)
  target_link_libraries(zp_tests PRIVATE zp)
  # The dump harness maps GTAIV.exe at its preferred base 0x400000, so the test
  # executable must not occupy it.
  target_link_options(zp_tests PRIVATE /BASE:0x60000000)
  add_test(NAME zp_tests COMMAND zp_tests)
endif()

# Runtime State driver (Gate 3): maps game_refs/GTAIV.exe, runs the same
# load-time pipeline DllMain runs and writes the `G`/`D` lines
# tools/parity/state_diff.py compares against the original ASI. 32-bit like the
# shipped ASI (the hook bodies are x86) with /BASE:0x60000000 so the image can
# sit at 0x400000 when the address space allows it; it falls back to a relocated
# heap image otherwise.
if(ZP_BUILD_STATE_DUMP)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(ZP_PARITY_GLOBALS "${CMAKE_CURRENT_BINARY_DIR}/parity_globals.inc")
  add_custom_command(
    OUTPUT "${ZP_PARITY_GLOBALS}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_parity_globals.py"
            --header "${CMAKE_CURRENT_SOURCE_DIR}/src/rebased.h"
            --out "${ZP_PARITY_GLOBALS}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/rebased.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_parity_globals.py"
    COMMENT "Generating the parity globals table from src/rebased.h")
  add_executable(zp_state_dump tests/state_dump.cpp src/dllmain.cpp ${ZP_PARITY_GLOBALS})
  target_link_libraries(zp_state_dump PRIVATE zp)
  target_compile_features(zp_state_dump PRIVATE cxx_std_17)
  target_compile_definitions(zp_state_dump PRIVATE ZP_PARITY_DRIVER)
  target_include_directories(zp_state_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                  ${CMAKE_CURRENT_BINARY_DIR})
  target_link_options(zp_state_dump PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE
                                           /BASE:0x60000000 /INCREMENTAL:NO /MAP)
endif()
