# ForgedLCE standalone build: compiles the loader and mods against the fake
# game layer (standalone/fakegame) and runs the tests. No LCE source needed.
#
#   cmake -S . -B build
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
#
# To build ForgedLCE into the real game instead, use scripts/apply.ps1.

cmake_minimum_required(VERSION 3.18)
project(ForgedLCE LANGUAGES CXX)

option(FLCE_EMULATE_CONSOLE
       "On Windows, build the console code path (built-in mods only, no mods folder or DLL loading)" OFF)

# The loader and mods are C++03-compatible. CI also builds with
# -DCMAKE_CXX_STANDARD=98 to keep them that way.
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FLCE_OUT ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

# The real game's Windows64 configuration defines _WINDOWS64.
set(FLCE_PLATFORM_DEFS "")
if(WIN32 AND NOT FLCE_EMULATE_CONSOLE)
  set(FLCE_PLATFORM_DEFS _WINDOWS64)
endif()

if(MSVC)
  add_compile_options(/W3 /utf-8)
  # Same as the game's projects.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS)
else()
  add_compile_options(-Wall)
endif()

# ---------------------------------------------------------------------------
# Loader + fake game
# ---------------------------------------------------------------------------
# The loader core: shared by source builds and the injected plugins.
set(FLCE_CORE_SOURCES
  loader/FLCE_Events.cpp
  loader/FLCE_Loader.cpp
  loader/FLCE_Registry.cpp
  loader/FLCE_Json.cpp
  loader/FLCE_DataMods.cpp)

# Source mode: the core + the source-build backend and platform layer,
# against the fake game (with the patches' code in it).
add_library(flce_game STATIC
  ${FLCE_CORE_SOURCES}
  loader/FLCE_GameSource.cpp
  loader/FLCE_PlatformGame.cpp
  standalone/fakegame/FakeGame.cpp)
target_include_directories(flce_game PUBLIC
  standalone/fakegame   # stdafx.h and the game header names
  loader
  include/forgedlce     # "ForgedLCE.h", as the loader includes it in the game
  include)              # "forgedlce/ForgedLCE.hpp", as mods include it
target_compile_definitions(flce_game PUBLIC ${FLCE_PLATFORM_DEFS})
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(flce_game PUBLIC Threads::Threads)
endif()

# The default (empty) built-in mod table that ships to the game: compile it
# to make sure it builds.
add_library(flce_default_table OBJECT loader/FLCE_StaticMods.cpp)
target_link_libraries(flce_default_table PRIVATE flce_game)

# ---------------------------------------------------------------------------
# Built-in mods, wrapped the same way scripts/apply.ps1 does in the game:
# stdafx.h first, then the mod with FLCE_STATIC_MOD_ID set.
# ---------------------------------------------------------------------------
set(FLCE_SLOTS "")
function(flce_static_mod modid source)
  set(slot ${CMAKE_BINARY_DIR}/slots/slot_${modid}.cpp)
  # file(CONFIGURE) only rewrites the file when its content changes.
  file(CONFIGURE OUTPUT ${slot} CONTENT
"// Generated like scripts/apply.ps1 generates FLCE_StaticModNN.cpp.
#include \"stdafx.h\"
#define FLCE_STATIC_MOD_ID ${modid}
#include \"${source}\"
" @ONLY)
  set(FLCE_SLOTS ${FLCE_SLOTS} ${slot} PARENT_SCOPE)
endfunction()

flce_static_mod(examplemod ${CMAKE_CURRENT_SOURCE_DIR}/examples/ExampleMod/ExampleMod.cpp)
flce_static_mod(testmod    ${CMAKE_CURRENT_SOURCE_DIR}/standalone/tests/TestMod.cpp)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(flce_tests
  standalone/tests/run_tests.cpp
  standalone/tests/StaticModsTable.cpp
  ${FLCE_SLOTS})
target_include_directories(flce_tests PRIVATE standalone/tests)
target_link_libraries(flce_tests PRIVATE flce_game)
set_target_properties(flce_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${FLCE_OUT})

# ---------------------------------------------------------------------------
# Code mods as modules (.dll / .so). ExampleMod.dll is also the file you would
# ship as a Windows64 mod.
# ---------------------------------------------------------------------------
add_library(examplemod_dll SHARED examples/ExampleMod/ExampleMod.cpp)
target_include_directories(examplemod_dll PRIVATE include)
set_target_properties(examplemod_dll PROPERTIES OUTPUT_NAME ExampleMod PREFIX "")

add_library(dllmod SHARED standalone/tests/DllMod.cpp)
target_include_directories(dllmod PRIVATE include)
set_target_properties(dllmod PROPERTIES OUTPUT_NAME DllMod PREFIX "")

if(WIN32 AND NOT FLCE_EMULATE_CONSOLE)
  # The source-mode tests load both from <test exe dir>\mods\. ExampleMod.dll
  # must be rejected there as a duplicate of the built-in examplemod.
  set_target_properties(examplemod_dll dllmod PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${FLCE_OUT}/mods)
  add_dependencies(flce_tests examplemod_dll dllmod)
endif()

# ---------------------------------------------------------------------------
# Injected mode: the code of the Xbox 360 / PS Vita plugins, tested against
# the fake game in RETAIL mode (no patches), with a simulated hook engine.
# ---------------------------------------------------------------------------
add_library(flce_fakegame_retail STATIC standalone/fakegame/FakeGame.cpp)
target_compile_definitions(flce_fakegame_retail PUBLIC FLCE_FAKE_RETAIL)
target_include_directories(flce_fakegame_retail PUBLIC standalone/fakegame loader include/forgedlce include)
if(NOT WIN32)
  target_link_libraries(flce_fakegame_retail PUBLIC Threads::Threads)
endif()

add_executable(flce_inject_tests
  ${FLCE_CORE_SOURCES}
  inject/FLCE_Addresses.cpp
  inject/FLCE_GameInjected.cpp
  inject/FLCE_InjectHooks.cpp
  inject/FLCE_HookPPC.cpp
  standalone/tests/FLCE_PlatformHost.cpp
  standalone/tests/InjectStaticMods.cpp
  standalone/tests/run_inject_tests.cpp
  ${CMAKE_BINARY_DIR}/slots/slot_testmod.cpp)
target_include_directories(flce_inject_tests PRIVATE inject standalone/tests)
target_link_libraries(flce_inject_tests PRIVATE flce_fakegame_retail ${CMAKE_DL_LIBS})
set_target_properties(flce_inject_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${FLCE_OUT})
add_dependencies(flce_inject_tests examplemod_dll dllmod)

enable_testing()
add_test(NAME flce_tests COMMAND flce_tests)
add_test(NAME flce_inject_tests
         COMMAND flce_inject_tests $<TARGET_FILE:dllmod> $<TARGET_FILE:examplemod_dll>
                 ${CMAKE_BINARY_DIR}/inject_work ${CMAKE_CURRENT_SOURCE_DIR}/inject/addresses/TEMPLATE.txt)
