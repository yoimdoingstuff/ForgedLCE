cmake_minimum_required(VERSION 3.15)
project(ExampleMod CXX)

# Builds the Windows64 DLL form of the mod. (For consoles, and optionally
# Windows64, the mod is instead compiled into the game: see
# scripts/apply.ps1 -StaticMods.)
#
# Mods only talk to the game through the C ABI in ForgedLCE.h, so any
# 64-bit Windows compiler works; it does not have to match the game's VS2012.
add_library(ExampleMod SHARED ExampleMod.cpp)
target_include_directories(ExampleMod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_compile_features(ExampleMod PRIVATE cxx_std_11)

# Static CRT: the DLL does not need a matching runtime installed.
set_property(TARGET ExampleMod PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
