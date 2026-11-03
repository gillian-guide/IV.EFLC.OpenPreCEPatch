cmake_minimum_required(VERSION 3.28)
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
option(ZP_LINK_MAP "Emit a linker .map for the ASI (development aid, not shipped)" OFF)
# Diagnostics are opt-in: the release ASI is built without them and writes no
# trace log. ZP_TRACE_INSTALLS names every installer arm; ZP_TRACE_WRITES also
# logs every game write through the trace injector.
option(ZP_TRACE_INSTALLS "Log every installer arm to ZolikaPatch.trace.log" OFF)
option(ZP_TRACE_WRITES "Log every game write to ZolikaPatch.trace.log (trace injector)" OFF)

if(ZP_BUILD_TESTS OR ZP_BUILD_DUMP OR ZP_LINK_MAP)
  enable_testing()
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)

# Third-party headers and the scanner are git submodules (see .gitmodules),
# checked out under external/.
set(ZP_EXT_INJECTOR ${CMAKE_CURRENT_SOURCE_DIR}/external/injector/include)
set(ZP_EXT_PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/external/Hooking.Patterns)
foreach(dir ${ZP_EXT_INJECTOR} ${ZP_EXT_PATTERNS})
  if(NOT EXISTS ${dir})
    message(FATAL_ERROR
      "${dir} is missing. Check out the submodules first:\n"
      "  git submodule update --init --recursive")
  endif()
endforeach()

# FusionFix-style module units (one logical piece per .ixx). The shipped ASI is
# 32-bit and the write-set dump is a 64-bit tool with its own injector shim, so
# the same units are compiled into two module libraries with different include
# paths and definitions; no target links both.
set(ZP_MODULE_UNITS
  source/common.ixx
  source/game.ixx
  source/patch.ixx
  source/log.ixx
  source/config.ixx
  source/vehicle_lights.ixx
  source/menu.ixx
  source/hook_bodies.ixx
  source/useless_checks.ixx
  source/catalogue.ixx
  source/launcher.ixx
  source/vehicles.ixx
  source/graphics.ixx
  source/stability.ixx
  source/audio.ixx
  source/input.ixx
  source/loading.ixx
  source/mission.ixx
  source/world.ixx
  source/multiplayer.ixx
  source/misc.ixx
)

# ASI-only units: the SkipLauncher implementation is the shipped launcher stage
# (32-bit hook bodies, a naked ROM body), so it belongs to the ASI-side module
# library only. The write-set dump links its own stand-ins
# (tests/stub_bodies.cpp) and must not see this unit.
set(ZP_ASI_MODULE_UNITS
  source/xliveless.ixx
)

set(ZP_SOURCES
  ${ZP_EXT_PATTERNS}/Hooking.Patterns.cpp
  src/catalogue_data.cpp
)

# The 32-bit runtime libraries exist only for the targets that are 32-bit: the
# shipped ASI, the native tests and the offline state driver. A dump-only
# configure must not even define them -- the dump is a 64-bit tool with its own
# injector shim and its own module library (zp_dump_modules), and building these
# here fails on the 32-bit-only sources (src/driveby_seh.h's #error) even though
# nothing links them.
if(ZP_BUILD_ASI OR ZP_BUILD_TESTS OR ZP_BUILD_STATE_DUMP)

# The shared runtime support library. EXCLUDE_FROM_ALL: the write-set dump
# recompiles the sources it needs against its own 64-bit injector shim, so a
# dump-only configure must not build the 32-bit xliveless module.
add_library(zp STATIC EXCLUDE_FROM_ALL ${ZP_SOURCES})
target_include_directories(zp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                     ${ZP_EXT_INJECTOR}
                                     ${ZP_EXT_PATTERNS})
target_compile_features(zp PUBLIC cxx_std_23)
target_compile_definitions(zp PUBLIC UNICODE _UNICODE NOMINMAX)
if(WIN32)
  target_link_libraries(zp PUBLIC version user32 shlwapi)
endif()
set_target_properties(zp PROPERTIES CXX_EXTENSIONS OFF)

# The ASI-side module library. CXX_SCAN_FOR_MODULES makes CMake scan the .ixx
# files and propagate their BMIs to the consumers.
add_library(zp_modules STATIC)
set_target_properties(zp_modules PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
target_sources(zp_modules PUBLIC FILE_SET CXX_MODULES FILES ${ZP_MODULE_UNITS}
                                                        ${ZP_ASI_MODULE_UNITS})
target_sources(zp_modules PRIVATE source/hook_bodies.cpp source/xliveless.cpp)
# The compiled helpers the bodies call: ordinary C++, split out of the assembly
# unit so it holds only `__declspec(naked)` bodies, their published data and the
# resolver table. ASI/test-side only - the dump never runs a body.
target_sources(zp_modules PRIVATE
  source/new_saves.cpp
  source/nms_allocator.cpp
  source/ooc.cpp
  source/script_calls.cpp
  source/vehicle_steer.cpp)
target_include_directories(zp_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source
                                             ${CMAKE_CURRENT_SOURCE_DIR}/src
                                             ${ZP_EXT_INJECTOR}
                                             ${ZP_EXT_PATTERNS})
target_compile_features(zp_modules PUBLIC cxx_std_23)
target_compile_definitions(zp_modules PUBLIC UNICODE _UNICODE NOMINMAX)
target_link_libraries(zp PUBLIC zp_modules)

endif()  # ZP_BUILD_ASI OR ZP_BUILD_TESTS OR ZP_BUILD_STATE_DUMP

if(ZP_BUILD_ASI)
  if(NOT WIN32)
    message(FATAL_ERROR "ZP_BUILD_ASI requires a Windows toolchain")
  endif()
  add_library(ZolikaPatch SHARED src/dllmain.cpp ZolikaPatch.def)
  set_target_properties(ZolikaPatch PROPERTIES CXX_SCAN_FOR_MODULES ON)
  target_link_libraries(ZolikaPatch PRIVATE zp)
  target_link_options(ZolikaPatch PRIVATE
    /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO)
  # A linker .map is a development aid (symbol order/addresses), never shipped.
  if(ZP_LINK_MAP)
    target_link_options(ZolikaPatch PRIVATE /MAP)
  endif()
  set_target_properties(ZolikaPatch PROPERTIES PREFIX "" SUFFIX ".asi")
  if(ZP_TRACE_INSTALLS)
    target_compile_definitions(ZolikaPatch PRIVATE ZP_TRACE_INSTALLS)
  endif()
  if(ZP_TRACE_WRITES)
    target_compile_definitions(ZolikaPatch PRIVATE ZP_TRACE_INSTALLS ZP_TRACE_WRITES)
  endif()
  # Body-byte proof: the write-set dump only calls the installers, so it cannot
  # see a hook body. With the linker map available, compare every `@exact` body
  # in kAbiBodies instruction-for-instruction with the original ASI (capstone),
  # and report the compiled replacements with their documented instruction
  # ranges. Registered as a test only when the original ASI is present; the ASI
  # and map are build products of this target.
  if(ZP_LINK_MAP AND Python3_Interpreter_FOUND
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
    add_custom_target(verify_bodies
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
              --candidate $<TARGET_FILE:ZolikaPatch>
              --map $<TARGET_FILE_DIR:ZolikaPatch>/ZolikaPatch.map
      DEPENDS ZolikaPatch
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Proving the rebuilt hook bodies against the original ASI")
    add_test(NAME zp_body_proof
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
                     --candidate $<TARGET_FILE:ZolikaPatch>
                     --map $<TARGET_FILE_DIR:ZolikaPatch>/ZolikaPatch.map)
    set_tests_properties(zp_body_proof PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
endif()

# Write-set dump: applies the installers to a mapped copy of the game image so
# the result can be diffed against the reference implementation. It must be a
# 64-bit process: a Win32 process has its heap/loader in the low address space
# (from 0x004D0000 up), so the 25 MB image cannot be mapped at its preferred
# base 0x400000 there (VirtualAlloc fails with ERROR_INVALID_ADDRESS, 487).
# The hook bodies are 32-bit assembly, so dllmain.cpp compiles them out under
# ZP_DUMP_TOOL and tests/stub_bodies.cpp supplies their addresses instead.
if(ZP_BUILD_DUMP)
  if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR
      "zp_dump must be built 64-bit (-A x64): a Win32 process cannot map\n"
      "GTAIV.exe at its preferred base 0x400000, so the write-set dump would\n"
      "fault before producing an oracle.")
  endif()
  # The same module units, compiled for the 64-bit tool: the dump injector shim
  # must be the first include path, and the installer bodies see ZP_DUMP_TOOL.
  add_library(zp_dump_modules STATIC)
  set_target_properties(zp_dump_modules PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
  target_sources(zp_dump_modules PUBLIC FILE_SET CXX_MODULES FILES ${ZP_MODULE_UNITS})
  target_sources(zp_dump_modules PRIVATE source/hook_bodies.cpp)
  target_include_directories(zp_dump_modules BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dump_injector)
  target_include_directories(zp_dump_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                    ${ZP_EXT_INJECTOR}
                                                    ${ZP_EXT_PATTERNS})
  target_compile_features(zp_dump_modules PUBLIC cxx_std_23)
  target_compile_definitions(zp_dump_modules PUBLIC UNICODE _UNICODE NOMINMAX ZP_DUMP_TOOL)

  add_executable(zp_dump tests/dump_main.cpp tests/stub_bodies.cpp
                 ${ZP_EXT_PATTERNS}/Hooking.Patterns.cpp src/catalogue_data.cpp src/dllmain.cpp
)
  set_target_properties(zp_dump PROPERTIES CXX_SCAN_FOR_MODULES ON)
  target_link_libraries(zp_dump PRIVATE zp_dump_modules)
  target_link_libraries(zp_dump PRIVATE version user32 shlwapi psapi)
  target_compile_features(zp_dump PRIVATE cxx_std_23)
  # /MAP is a development aid; ASLR stays on, because a fixed base would make the
  # loader place other mappings over 0x400000 (observed: a 72 KB anonymous mapping
  # at 0x400000, VirtualAlloc fails 487). The dump is layout-independent anyway:
  # it canonicalises hook-body branch destinations to their reference VAs.
  target_link_options(zp_dump PRIVATE /MAP)
  # The dump uses a 64-bit-safe injector shim (the real one is 32-bit only).
  target_include_directories(zp_dump BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/dump_injector)
  target_include_directories(zp_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                             ${ZP_EXT_INJECTOR}
                                             ${ZP_EXT_PATTERNS})
  target_compile_definitions(zp_dump PRIVATE UNICODE _UNICODE NOMINMAX ZP_DUMP_TOOL)
  # The oracle is only meaningful with the real image present. Run from the
  # source tree so the dump finds ../game_refs/GTAIV.exe, and use the
  # repository's own INI (not the live one) so the config is reproducible.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe)
    add_test(NAME zp_dump_full
             COMMAND zp_dump ${CMAKE_CURRENT_BINARY_DIR}/dump-full.txt
                     ${CMAKE_CURRENT_SOURCE_DIR}/ZolikaPatch.ini)
    # The ordering guard: RemoveUselessChecks must stay ahead of MiscFixes and
    # RestoreDeathMusic (its tables destroy both signatures, as in the original).
    add_test(NAME zp_dump_ordering
             COMMAND zp_dump --ordering ${CMAKE_CURRENT_SOURCE_DIR}/ZolikaPatch.ini)
    set_tests_properties(zp_dump_full zp_dump_ordering PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
endif()

if(ZP_BUILD_TESTS)
  add_executable(zp_tests tests/test_main.cpp src/dllmain.cpp)
  set_target_properties(zp_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
  target_link_libraries(zp_tests PRIVATE zp)
  # The dump harness maps GTAIV.exe at its preferred base 0x400000, so the test
  # executable must not occupy it.
  target_link_options(zp_tests PRIVATE /BASE:0x60000000)
  add_test(NAME zp_tests COMMAND zp_tests)
  # The RemoveUselessChecks data file is generated from the shipped ASI; this
  # checks the per-array hashes recorded in its provenance header without needing
  # the ASI present (--verify re-extracts when it is).
  if(Python3_Interpreter_FOUND)
    add_test(NAME zp_useless_checks_data
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract_useless_checks.py --print-sha)
  endif()
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
  target_compile_features(zp_state_dump PRIVATE cxx_std_23)
  target_compile_definitions(zp_state_dump PRIVATE ZP_PARITY_DRIVER)
  target_include_directories(zp_state_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                                  ${CMAKE_CURRENT_BINARY_DIR})
  target_link_options(zp_state_dump PRIVATE /MACHINE:X86 /SUBSYSTEM:CONSOLE
                                           /BASE:0x60000000 /INCREMENTAL:NO /MAP)
endif()
