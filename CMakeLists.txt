cmake_minimum_required(VERSION 3.28)
project(ZolikaPatch LANGUAGES CXX)

# A Win32 ASI: 32-bit, MSVC, static CRT, fixed image base, windowed subsystem.
if(NOT MSVC)
  message(FATAL_ERROR "The ASI is built with MSVC; configure with a Visual Studio generator.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The ASI is 32-bit (PE32); configure with -A Win32.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release, Debug, and Trace: the shipped binary (silent, like the original) and
# the same binary with the parity trace switched on. Trace compiles like Release.
if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Trace" CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_TRACE "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_TRACE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_TRACE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

set(ZP_NAME "ZolikaPatch")
set(ZP_VERSION_MAJOR 7)
set(ZP_VERSION_MINOR 65)
set(ZP_VERSION_BUILD 0)
set(ZP_VERSION_REVISION 0)

find_package(Git QUIET)
set(ZP_GIT_SHA "")
if(Git_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ZP_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(ZP_PRODUCT_VERSION "${ZP_VERSION_MAJOR}.${ZP_VERSION_MINOR}")
if(ZP_GIT_SHA)
  string(APPEND ZP_PRODUCT_VERSION "-${ZP_GIT_SHA}")
endif()

set(ZP_VERSION_DEFINITIONS
  "rsc_CompanyName=\"${ZP_NAME}\""
  "rsc_FileDescription=\"${ZP_NAME}\""
  "rsc_InternalName=\"${ZP_NAME}\""
  "rsc_OriginalFilename=\"${ZP_NAME}.asi\""
  "rsc_ProductName=\"${ZP_NAME}\""
  "rsc_LegalCopyright=\"GPL-3.0\""
  "rsc_FileVersion_MAJOR=${ZP_VERSION_MAJOR}"
  "rsc_FileVersion_MINOR=${ZP_VERSION_MINOR}"
  "rsc_FileVersion_BUILD=${ZP_VERSION_BUILD}"
  "rsc_FileVersion_REVISION=${ZP_VERSION_REVISION}"
  "rsc_FileVersion=\"${ZP_VERSION_MAJOR}.${ZP_VERSION_MINOR}.${ZP_VERSION_BUILD}\""
  "rsc_ProductVersion=\"${ZP_PRODUCT_VERSION}\""
  "rsc_GitSHA1=\"${ZP_GIT_SHA}\"")

add_library(zp_modules STATIC
  external/hooking/Hooking.Patterns.cpp)
set_target_properties(zp_modules PROPERTIES CXX_SCAN_FOR_MODULES ON CXX_EXTENSIONS OFF)
target_sources(zp_modules PUBLIC
  FILE_SET CXX_MODULES FILES
    source/common.ixx
    source/improvements.ixx
    source/bytes.ixx
    source/bodies.ixx
    source/patch.ixx
    source/config.ixx
    source/game.ixx
    source/options.ixx
    source/loader.ixx
    source/shared.ixx
    source/native.ixx
    source/stability.ixx
    source/vehicles.ixx
    source/useless_checks.ixx
    source/graphics.ixx
    source/launcher.ixx
    source/loading.ixx
    source/audio.ixx
    source/misc.ixx
    source/menu.ixx
    source/ooc.ixx)
target_include_directories(zp_modules PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/external/injector/include
  ${CMAKE_CURRENT_SOURCE_DIR}/external/hooking)
target_compile_features(zp_modules PUBLIC cxx_std_23)
target_compile_definitions(zp_modules PUBLIC
  UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
  ${ZP_VERSION_DEFINITIONS})
target_compile_options(zp_modules PUBLIC /utf-8 /Zc:__cplusplus /W3)
target_link_libraries(zp_modules PUBLIC version shlwapi)

add_library(${ZP_NAME} SHARED
  source/dllmain.cpp
  source/resources/version.rc)
set_target_properties(${ZP_NAME} PROPERTIES
  CXX_SCAN_FOR_MODULES ON
  PREFIX ""
  SUFFIX ".asi")
target_link_libraries(${ZP_NAME} PRIVATE zp_modules)
# A linker map is a development aid: the transcribed bodies are proved against
# the original ASI at the addresses it records, so the map is what gives a body
# its candidate VA. It is not shipped and changes no byte of the image.
option(ZP_LINK_MAP "Emit a linker map for the ASI" ON)
target_link_options(${ZP_NAME} PRIVATE
  /MACHINE:X86 /SUBSYSTEM:WINDOWS /BASE:0x10000000 /INCREMENTAL:NO)
if(ZP_LINK_MAP)
  target_link_options(${ZP_NAME} PRIVATE /MAP)
endif()
# The trace build writes the diagnostic log the parity tools read; the shipped
# ASI is silent, exactly like the original.
option(ZP_TRACE "Write ZolikaPatch.trace.log" OFF)
if(ZP_TRACE)
  target_compile_definitions(${ZP_NAME} PRIVATE ZP_TRACE)
endif()
target_compile_definitions(${ZP_NAME} PRIVATE $<$<CONFIG:Trace>:ZP_TRACE>)

enable_testing()

add_executable(zp_tests tests/test_main.cpp)
set_target_properties(zp_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_tests PRIVATE zp_modules)
add_dependencies(zp_tests ${ZP_NAME})
add_test(NAME zp_tests COMMAND zp_tests $<TARGET_FILE:${ZP_NAME}>)
set_tests_properties(zp_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The OPCEP_IMPROVEMENTS switch and the failsafe primitives: the parser's
# documented values, the one-time environment resolver, the image-containment and
# body checks, the P13 match-count rule, and the same null dereference in a child
# process under both configurations (off must die with 0xC0000005, on must exit 0).
add_executable(zp_failsafe tests/failsafe.cpp)
set_target_properties(zp_failsafe PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_failsafe PRIVATE zp_modules)
add_test(NAME zp_failsafe COMMAND zp_failsafe)
set_tests_properties(zp_failsafe PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The two per-model light tables the loader preamble reads from
# [PopupLightAngles] / [HeadlightConeOffsets]: the recovered grammar, defaults and
# faults, on documents only -- no game image and no ASI load.
add_executable(zp_model_tables tests/model_tables.cpp)
set_target_properties(zp_model_tables PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_model_tables PRIVATE zp_modules)
add_test(NAME zp_model_tables COMMAND zp_model_tables)
set_tests_properties(zp_model_tables PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The save converter's behavioural test: the eighteen-chunk walk and the three patch
# values it selects, on every real save under game_refs/saves and on synthetic layouts,
# with the test's own parser as the oracle. Offline: no game image, no ASI load, and the
# walk it calls patches nothing.
add_executable(zp_save_walk tests/save_walk.cpp)
set_target_properties(zp_save_walk PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_save_walk PRIVATE zp_modules)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/saves)
  add_test(NAME zp_save_walk
    COMMAND zp_save_walk ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/saves)
  set_tests_properties(zp_save_walk PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

# The native layer's two guarded entries (source/native.ixx), asserted by behaviour
# instead of by their disassembly: the normal answers of the invoke (calls the
# handler, answers 1) and of the resolve (answers the handler address, does not call
# it), a fault swallowed inside the guarded region, the invoke's null-handler skip,
# and the null resolver whose zero the *caller* faults on, in a child process.
add_executable(zp_native_guard tests/native_guard.cpp)
set_target_properties(zp_native_guard PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_native_guard PRIVATE zp_modules)
add_test(NAME zp_native_guard COMMAND zp_native_guard)
set_tests_properties(zp_native_guard PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The NewMemorySystem arena, asserted by behaviour instead of by its disassembly: the
# three rebuilt vtable methods (allocate 0x10038360, free 0x10038430, membership
# 0x100385A0) driven directly through loading.ixx's test-only seam. The scheme (unit,
# want, the 2*unit slack, the distance range), the distance word, the 0xCD fill,
# membership, free and the null-returning failed request, all on the process heap.
# Offline: no game image, no ASI load, and it writes no files.
add_executable(zp_nms_arena tests/nms_arena.cpp)
set_target_properties(zp_nms_arena PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_nms_arena PRIVATE zp_modules)
add_test(NAME zp_nms_arena COMMAND zp_nms_arena)
set_tests_properties(zp_nms_arena PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The DFA close-handle body (0x10004790, class B in docs/rebuilt.md) and its four
# transcribed siblings, asserted by behaviour instead of by their disassembly: a real
# 32-byte file opened through the create wrapper, sized, seeked and read through the
# other three, then closed through the rebuilt one; the closed handle's answer (FALSE
# and ERROR_INVALID_HANDLE 6) from the transcribed reader and from DuplicateHandle;
# null, double close and a never-opened value, which have no guard because the body's
# only instructions before the call are the frame and `push [ebp+8]`; the pass-through
# equality with CloseHandle itself; and the frame's registration (`fs:[0]` unchanged
# across the call). The unreachable half -- the filter's EXCEPTION_CONTINUE_EXECUTION
# retrying a faulting call instead of answering -- runs in a child process whose
# CloseHandle import slot is replaced with a callee that writes one probe byte on entry
# and then faults: the parent's watchdog requires the child to be *still running*, the
# probe byte to be there and no answer to have been written, then terminates and reaps
# it. Offline: no game image, no ASI load; a fixture file, a report and a probe file in
# the binary directory, all removed.
add_executable(zp_dfa_close tests/dfa_close.cpp)
set_target_properties(zp_dfa_close PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_dfa_close PRIVATE zp_modules)
add_test(NAME zp_dfa_close COMMAND zp_dfa_close)
set_tests_properties(zp_dfa_close PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  TIMEOUT 120)

# The SpeedupBugFix body (0x10050540, class B in docs/rebuilt.md), asserted by
# behaviour instead of by its disassembly: the four derived floats against the
# formula recomputed in the test at five frequencies (bit patterns, not
# approximations), the eight published slots by the ASI dword each one is, the
# original's store order through overlapping destinations, the clock call at
# `match - 51`, the missing-scan fault on 0xFFFFFFCD in-process, and the second
# call's frequency-once/no-store guarantee. Offline: no game image, no ASI load,
# no files.
add_executable(zp_speedup_clock tests/speedup_clock.cpp)
set_target_properties(zp_speedup_clock PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_speedup_clock PRIVATE zp_modules)
add_test(NAME zp_speedup_clock COMMAND zp_speedup_clock)
set_tests_properties(zp_speedup_clock PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The OutOfCommissionFix init (sub_1004B750, class B in docs/rebuilt.md), asserted
# by behaviour instead of by its disassembly: the one-shot guard that has no writer
# (three calls build three archives, none ever freed), the cursor slot left equal to
# the archive's 182,272 bytes with the pinned fnv1a and header words, the
# registration that happens even when the decode's own checks fail (a damaged
# Adler-32 trailer, and a first opcode that never terminates), and the fake-pak
# entry's name read out of the module's own vector. Offline: no game image, no ASI
# load, no files; the resource is damaged in place through VirtualProtect and
# restored, and `zp_dump --ooc-selftest` remains the shipped path's proof.
add_executable(zp_ooc_init tests/ooc_init.cpp)
set_target_properties(zp_ooc_init PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_ooc_init PRIVATE zp_modules)
add_test(NAME zp_ooc_init COMMAND zp_ooc_init)
set_tests_properties(zp_ooc_init PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The documented preserved faults of the INI loader, asserted by their observable
# behaviour instead of by prose: the two conversion throws by *exact* type
# (std::invalid_argument on `Key=abc` and `Key=<space>`, std::out_of_range past
# `int`), the key-before-section null-section store (a child process that must exit
# 0xC0000005 *after* its diagnostic line), the BOM line that is not a section header
# and whose following key line dies in the same null store (also a child), and the
# line buffer: a 255-byte line parses and its child exits 0, while a 256-byte line
# leaves the parse spinning on the same 255 bytes -- that child must *not* exit
# within the test's timeout, and the test terminates it, so a re-introduced split
# fails the check instead of hanging the suite. The ctest timeout below is the
# backstop. Offline: no game image, no ASI load, no file beyond the children's
# reports.
add_executable(zp_fault_parity tests/fault_parity.cpp)
set_target_properties(zp_fault_parity PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_link_libraries(zp_fault_parity PRIVATE zp_modules)
add_test(NAME zp_fault_parity COMMAND zp_fault_parity)
set_tests_properties(zp_fault_parity PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  TIMEOUT 300)

# A tool that maps a copy of the game image must pin where the copy lands: a
# Win32 process cannot own the image's preferred base (0x00400000) -- the loader
# maps the NLS section there before user code -- so `tests/dump_main.cpp` pins the
# copy at `kFixedImageBase` (0x30000000) and the relocation delta is a constant
# instead of the allocator's choice; the dump's bytes are a function of the source
# image, not of the run's ASLR. An executable's default base is 0x00400000, so the
# image-mapping tools are linked at 0x20000000 and with ASLR off; otherwise a
# relocated tool could land on the address the copy needs and the dump would fail
# instead of being reproducible. (The ASI keeps its own base; nothing here changes
# it.)
add_executable(zp_dump tests/dump_main.cpp)
set_target_properties(zp_dump PROPERTIES
  CXX_SCAN_FOR_MODULES ON
  LINK_FLAGS "/BASE:0x20000000 /DYNAMICBASE:NO")
target_link_libraries(zp_dump PRIVATE zp_modules)
add_dependencies(zp_dump ${ZP_NAME})

# The loader preamble's salt rule, pinned on the real images: the probe hits on
# 1.0.8.0 (shipped `.data` values stand) and misses on 1.0.0.0 / 1.0.4.0 (the ten
# fallback stores run). Not a game launch: the images are files. It maps each
# image at its preferred base like the dump, so it gets the same link pin.
add_executable(zp_salt_check tests/salt_check.cpp)
set_target_properties(zp_salt_check PROPERTIES
  CXX_SCAN_FOR_MODULES ON
  LINK_FLAGS "/BASE:0x20000000 /DYNAMICBASE:NO")
target_link_libraries(zp_salt_check PRIVATE zp_modules)
add_dependencies(zp_salt_check ${ZP_NAME})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV_1000.exe
   AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV_1040.exe)
  add_test(NAME zp_salt_probe
    COMMAND zp_salt_check ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
endif()

# The empty baseline: with no arm installed the pipeline must change nothing on
# every supported image. Every later arm is diffed against that.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe)
  add_test(NAME zp_dump_empty
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_empty_dump.py
            $<TARGET_FILE:zp_dump> ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
endif()

add_executable(zp_pe_check tests/pe_check.cpp)
add_dependencies(zp_pe_check ${ZP_NAME})
add_test(NAME zp_pe_check COMMAND zp_pe_check $<TARGET_FILE:${ZP_NAME}>)
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
  add_test(NAME zp_body_proof_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
            --selftest --original ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
  # The transcribed bodies, one manifest row each: instruction-for-instruction
  # against the original ASI, with absolute operands compared as slots.
  add_test(NAME zp_body_proof
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_bodies.py
            --original ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off
            --candidate $<TARGET_FILE:${ZP_NAME}>
            --bodies ${CMAKE_CURRENT_SOURCE_DIR}/tests/bodies.tsv
            --map $<TARGET_FILE_DIR:${ZP_NAME}>/${ZP_NAME}.map)
  set_tests_properties(zp_body_proof PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(Python3_Interpreter_FOUND)
  # The generated artefacts must not drift from their sources.
  add_test(NAME zp_catalogue_drift
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_options.py --check)
  add_test(NAME zp_body_targets_drift
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_body_targets.py --check)
  add_test(NAME zp_ini_drift
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_ini.py --check)
  add_test(NAME zp_oracle_verified
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_oracle_verified.py)
  add_test(NAME zp_fault_dialogs
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_fault_dialogs.py)
  # A recovered signature whose fixed bytes cover a .reloc dword can match on one
  # launch and miss on another (the game's base is arbitrary every run), so the arm
  # silently fires or misses depending on ASLR. This is the lint for that class:
  # every pattern-like literal in source/*.ixx, including the table rows, is
  # measured at six relocation deltas, and its hit set must be identical at each.
  # A literal whose hit set changes is classified against the key's recovered
  # original string: `MiscFixes`' first row is the original's own string and is a
  # preserved fault (docs/parity.md), anything else is a failure to fix.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/GTAIV.exe)
    add_test(NAME zp_relocated_literals
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_relocated_literals.py
              --game ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
  endif()
  # The other half of "does the scan mean what the original's does": a shortened
  # copy of a handler's signature picks the original's first match on 1.0.8.0 and a
  # different function elsewhere. This compares our per-image hit sets with the
  # original handler's own signature (docs/parity.md, signature parity).
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/ZolikaPatch_original.asi.off)
    add_test(NAME zp_signature_parity
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_signature_parity.py
              --game ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
  endif()
endif()
if(Python3_Interpreter_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs/EFLC_1110.exe)
  add_test(NAME zp_launcher_slots
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_launcher_slots.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../game_refs)
endif()
