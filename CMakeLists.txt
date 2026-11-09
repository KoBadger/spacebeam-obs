cmake_minimum_required(VERSION 3.20)

# Single source of truth for the plugin version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SB_VERSION LIMIT_COUNT 1)
string(STRIP "${SB_VERSION}" SB_VERSION)
if(NOT SB_VERSION)
  set(SB_VERSION "0.0.0")
endif()

project(spacebeam-obs VERSION ${SB_VERSION} LANGUAGES C CXX)

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
  _CRT_SECURE_NO_WARNINGS
  SB_VERSION="${SB_VERSION}")

target_link_libraries(spacebeam PRIVATE
  "${SB_IMPLIB}"
  user32
  shell32
  ole32
  advapi32
  comdlg32)

if(MSVC)
  target_compile_options(spacebeam PRIVATE /W3 /O2 /utf-8 /MP)
  target_link_options(spacebeam PRIVATE /INCREMENTAL:NO)
  set_target_properties(spacebeam PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# ---------------------------------------------------------------- dock panel
# The dock is a Qt widget, so it needs C++ and the Qt headers/libs that match
# OBS's own Qt build. When Qt is missing the filter still builds, it just has no
# dock.
#
# The Qt link is DELAY-LOADED on purpose. OBS ships its own Qt DLLs, so the
# plugin resolves against whatever version OBS was built with; if OBS is later
# upgraded to a different Qt minor, an eager import would make the whole plugin
# fail to load. Delay-loading keeps the filter working and lets the dock disable
# itself at runtime (see qt_available() in spacebeam_dock.cpp).
if(NOT SB_QT_ROOT)
  set(_qt_candidates
    "$ENV{QTDIR}"
    "$ENV{Qt6_DIR}"
    "$ENV{SB_QT_ROOT}"
    "C:/Qt/6.11.1/msvc2022_64"
    "C:/Qt/6.11.1/msvc2019_64"
    "$ENV{USERPROFILE}/QtSDK")
  foreach(_c IN LISTS _qt_candidates)
    if(_c AND EXISTS "${_c}/include/QtWidgets/QWidget")
      set(SB_QT_ROOT "${_c}")
      break()
    endif()
  endforeach()
endif()

set(SB_FRONTEND_IMPLIB "${SB_ROOT}/deps/lib/obs-frontend-api.lib")

# The main plugin uses only the frontend *event* API (not Qt), so it links the
# frontend library regardless of whether the dock is built.
if(EXISTS "${SB_FRONTEND_IMPLIB}")
  target_link_libraries(spacebeam PRIVATE "${SB_FRONTEND_IMPLIB}")
endif()

option(SB_NO_DOCK "Build without the Qt dock panel" OFF)

if(NOT SB_NO_DOCK AND SB_QT_ROOT AND EXISTS "${SB_QT_ROOT}/include/QtWidgets/QWidget" AND EXISTS "${SB_FRONTEND_IMPLIB}")
  message(STATUS "SpaceBeam: building dock panel (Qt at ${SB_QT_ROOT})")

  # The dock lives in its own DLL on purpose. OBS ships its own Qt, so the dock
  # resolves against whatever Qt OBS was built with; if OBS is upgraded to a
  # different Qt minor the dock DLL simply fails to load and the main plugin
  # (which imports no Qt at all) keeps working. MSVC cannot delay-load Qt
  # anyway - Qt imports data symbols such as QWidget::staticMetaObject.
  add_library(spacebeam_dock SHARED "${SB_ROOT}/src/spacebeam_dock.cpp")
  target_compile_definitions(spacebeam_dock PRIVATE SB_DOCK_DLL)
  target_include_directories(spacebeam_dock PRIVATE
    "${SB_ROOT}/libobs"
    "${SB_ROOT}/deps/include"
    "${SB_QT_ROOT}/include"
    "${SB_QT_ROOT}/include/QtCore"
    "${SB_QT_ROOT}/include/QtGui"
    "${SB_QT_ROOT}/include/QtWidgets")
  target_link_libraries(spacebeam_dock PRIVATE
    "${SB_IMPLIB}"
    "${SB_FRONTEND_IMPLIB}"
    "${SB_QT_ROOT}/lib/Qt6Widgets.lib"
    "${SB_QT_ROOT}/lib/Qt6Gui.lib"
    "${SB_QT_ROOT}/lib/Qt6Core.lib")
  target_compile_definitions(spacebeam_dock PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS)
  target_compile_options(spacebeam_dock PRIVATE /W3 /O2 /utf-8 /MP /Zc:__cplusplus /permissive- /EHa)
  target_link_options(spacebeam_dock PRIVATE /INCREMENTAL:NO)
  set_target_properties(spacebeam_dock PROPERTIES
    PREFIX ""
    OUTPUT_NAME "spacebeam_dock"
    SUFFIX ".dll"
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

  # The main plugin loads the dock DLL at runtime, so it needs no Qt headers.
  target_compile_definitions(spacebeam PRIVATE SB_WITH_DOCK)
else()
  message(STATUS "SpaceBeam: dock panel disabled (Qt or obs-frontend-api.lib missing)")
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
