cmake_minimum_required(VERSION 3.20)
project(spacebeam-obs VERSION 0.1.0 LANGUAGES C)

# Builds against an *installed* OBS Studio. We do not compile libobs from
# source: the import library in deps/lib was generated from obs.dll with
# dumpbin + lib.exe, and the headers come from the matching obs-studio tag.
#
# That keeps this build to a few hundred KB instead of a multi-GB obs-studio
# checkout. If you ever need symbols obs.dll does not export, switch to the
# full obs-studio CMake build.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT OBS_ROOT)
  set(OBS_ROOT "C:/Program Files/obs-studio")
endif()

set(SB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SB_SRC "${SB_ROOT}/src/spacebeam.c")
set(SB_HEADERS "${SB_ROOT}/libobs")
set(SB_IMPLIB "${SB_ROOT}/deps/lib/obs.lib")

if(NOT EXISTS "${SB_IMPLIB}")
  message(FATAL_ERROR
    "Missing ${SB_IMPLIB}.\n"
    "Regenerate it from the installed obs.dll:\n"
    "  dumpbin /exports \"${OBS_ROOT}/bin/64bit/obs.dll\" > exports.txt\n"
    "  then build deps/obs.def from the names and run\n"
    "  lib /def:deps/obs.def /out:deps/lib/obs.lib /machine:x64")
endif()

add_library(spacebeam MODULE "${SB_SRC}")

target_include_directories(spacebeam PRIVATE
  "${SB_HEADERS}"
  "${SB_HEADERS}/graphics"
  "${SB_HEADERS}/util"
  "${SB_HEADERS}/media-io"
  "${SB_HEADERS}/callback"
  "${SB_ROOT}/deps/include")

target_compile_definitions(spacebeam PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  _CRT_SECURE_NO_WARNINGS)

target_link_libraries(spacebeam PRIVATE
  "${SB_IMPLIB}"
  user32
  shell32
  ole32
  advapi32)

if(MSVC)
  target_compile_options(spacebeam PRIVATE /W3 /O2 /utf-8 /MP)
  target_link_options(spacebeam PRIVATE /INCREMENTAL:NO)
  set_target_properties(spacebeam PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

set_target_properties(spacebeam PROPERTIES
  PREFIX ""
  OUTPUT_NAME "spacebeam"
  SUFFIX ".dll")

# Install layout matches an OBS plugin folder:
#   obs-plugins/64bit/spacebeam.dll
#   data/obs-plugins/spacebeam/**
install(TARGETS spacebeam
        RUNTIME DESTINATION "obs-plugins/64bit"
        LIBRARY DESTINATION "obs-plugins/64bit")
install(DIRECTORY "${SB_ROOT}/data/locale/" DESTINATION "data/obs-plugins/spacebeam/locale")
install(DIRECTORY "${SB_ROOT}/data/shaders/" DESTINATION "data/obs-plugins/spacebeam/shaders")
