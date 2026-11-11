# ForgedLCE PS Vita plugin (taiHEN), built with the free VitaSDK.
#
#   export VITASDK=/usr/local/vitasdk
#   cmake -S inject/vita -B build-vita && cmake --build build-vita
#
# Produces ForgedLCE.suprx. UNTESTED on hardware; see docs/VITA_PLUGIN.md.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "VitaSDK toolchain")
  else()
    message(FATAL_ERROR "Set the VITASDK environment variable to your VitaSDK folder")
  endif()
endif()

project(ForgedLCEVita C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(ForgedLCE
  ${ROOT}/loader/FLCE_Events.cpp
  ${ROOT}/loader/FLCE_Loader.cpp
  ${ROOT}/loader/FLCE_Registry.cpp
  ${ROOT}/loader/FLCE_Json.cpp
  ${ROOT}/loader/FLCE_DataMods.cpp
  ${ROOT}/loader/FLCE_StaticMods.cpp
  ${ROOT}/inject/FLCE_Addresses.cpp
  ${ROOT}/inject/FLCE_GameInjected.cpp
  ${ROOT}/inject/FLCE_InjectHooks.cpp
  FLCE_PluginVita.cpp)

target_include_directories(ForgedLCE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}   # stdafx.h for this plugin
  ${ROOT}/loader
  ${ROOT}/include/forgedlce
  ${ROOT}/include
  ${ROOT}/inject)

target_compile_options(ForgedLCE PRIVATE -O2 -Wall -fno-strict-aliasing)

target_link_libraries(ForgedLCE
  taihen_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
  SceKernelModulemgr_stub
  SceIofilemgr_stub
  stdc++
  gcc)

# Plugins have no C runtime start-up code; FLCE_PluginVita.cpp runs the C++
# global constructors itself.
set_target_properties(ForgedLCE PROPERTIES LINK_FLAGS "-nostartfiles")

vita_create_self(ForgedLCE.suprx ForgedLCE CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml UNSAFE)
