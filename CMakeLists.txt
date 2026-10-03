cmake_minimum_required(VERSION 3.16)
project(raylib_doom C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  if(EMSCRIPTEN)
    # Debug info would make the .wasm several times larger.
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
  endif()
endif()

# The browser build (emcmake cmake -B build-web) uses raylib's web
# platform whether or not -DPLATFORM=Web is given.
if(EMSCRIPTEN)
  set(PLATFORM Web CACHE STRING "raylib platform" FORCE)
endif()

# Android APK (android/): its Gradle build configures this with the
# NDK and -DRAYLIB_DOOM_ANDROID=ON, which makes the game
# libraylibdoom.so, loaded by NativeActivity. RAYLIB_DOOM_ANDROID_XR
# adds the OpenXR output (i_xr.c on EGL) with the Khronos loader.
option(RAYLIB_DOOM_ANDROID "Build libraylibdoom.so for the Android APK" OFF)
option(RAYLIB_DOOM_ANDROID_XR "Add OpenXR (Android XR) to the Android build" OFF)
if(RAYLIB_DOOM_ANDROID)
  if(NOT ANDROID)
    message(FATAL_ERROR "RAYLIB_DOOM_ANDROID needs the NDK's toolchain; build android/ with Gradle")
  endif()
  set(PLATFORM Android CACHE STRING "raylib platform" FORCE)
  if(RAYLIB_DOOM_ANDROID_XR)
    # OpenXR runtimes want OpenGL ES 3.
    set(OPENGL_VERSION "ES 3.0" CACHE STRING "raylib OpenGL version" FORCE)
  endif()
elseif(RAYLIB_DOOM_ANDROID_XR)
  message(FATAL_ERROR "RAYLIB_DOOM_ANDROID_XR needs RAYLIB_DOOM_ANDROID")
endif()

# Use an installed raylib if there is one, otherwise build it.
# Distribution builds have no network: they turn the download off
# (-DRAYLIB_DOOM_FETCH_RAYLIB=OFF), so a missing raylib is an error.
# FETCHCONTENT_SOURCE_DIR_RAYLIB=DIR builds the raylib source in DIR
# without downloading it.
option(RAYLIB_DOOM_FETCH_RAYLIB
  "Download and build raylib when no installed one is found" ON)
if(FETCHCONTENT_SOURCE_DIR_RAYLIB)
  # Build that source, even when raylib is installed.
elseif(RAYLIB_DOOM_FETCH_RAYLIB)
  find_package(raylib 5.0 QUIET)
else()
  find_package(raylib 5.0 REQUIRED)
endif()
if(NOT raylib_FOUND)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)
endif()

set(DOOM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10)

set(DOOM_SOURCES
  am_map.c d_items.c d_main.c d_net.c doomdef.c doomstat.c dstrings.c
  f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c
  i_main.c i_music.c i_net.c i_raylib.c i_sound.c i_system.c i_video.c info.c
  m_argv.c m_bbox.c m_cheat.c m_fixed.c m_menu.c m_misc.c m_random.c m_swap.c
  p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c
  p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c
  p_spec.c p_switch.c p_telept.c p_tick.c p_user.c
  r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c
  s_sound.c sounds.c st_lib.c st_stuff.c tables.c v_video.c w_wad.c
  u_mapinfo.c wi_stuff.c z_zone.c
  opl3.c)
list(TRANSFORM DOOM_SOURCES PREPEND ${DOOM_DIR}/)

if(RAYLIB_DOOM_ANDROID)
  add_library(raylibdoom SHARED ${DOOM_SOURCES} ${DOOM_DIR}/i_android.c)
  target_include_directories(raylibdoom PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)
  # Keep the glue's entry point, which NativeActivity looks up.
  target_link_options(raylibdoom PRIVATE -u ANativeActivity_onCreate)
  target_link_libraries(raylibdoom PRIVATE android log)
  if(RAYLIB_DOOM_ANDROID_XR)
    # From the openxr_loader_for_android AAR, through prefab.
    find_package(OpenXR REQUIRED CONFIG)
    target_sources(raylibdoom PRIVATE ${DOOM_DIR}/i_xr.c)
    target_compile_definitions(raylibdoom PRIVATE DOOM_XR)
    target_link_libraries(raylibdoom PRIVATE OpenXR::openxr_loader EGL GLESv3)
  endif()
else()
  add_executable(raylibdoom ${DOOM_SOURCES})
endif()
target_compile_definitions(raylibdoom PRIVATE NORMALUNIX LINUX)
target_link_libraries(raylibdoom PRIVATE raylib m)
if(WIN32)
  target_link_libraries(raylibdoom PRIVATE ws2_32)
  target_sources(raylibdoom PRIVATE ${DOOM_DIR}/i_win32.c)
endif()
if(APPLE)
  # i_main.c shows a dialog when raylibDOOM.app finds no IWAD.
  target_link_libraries(raylibdoom PRIVATE "-framework CoreFoundation")
endif()
if(EMSCRIPTEN)
  include(web/web.cmake)
endif()

# Plain char is unsigned on Linux arm/aarch64, ppc64el, riscv64 and
# s390x, but the code assumes it is signed, as on x86: ticcmd_t's
# forwardmove and sidemove are char, so backward and left moves came
# out as big forward/right ones and demos desynced. Where char is
# already signed this changes nothing. d_ticcmd.h refuses to compile
# without it.
set(DOOM_SIGNED_CHAR "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(DOOM_SIGNED_CHAR -fsigned-char)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # The 1993 code relies on signed overflow wrapping and on
  # type punning through pointers; keep the optimizer honest.
  target_compile_options(raylibdoom PRIVATE
    -fwrapv -fno-strict-aliasing -Wall -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-unused-const-variable)
  target_compile_options(raylibdoom PRIVATE ${DOOM_SIGNED_CHAR})
endif()

# cmake --install: the game, its desktop entry, icon, AppStream
# metadata and manual page, for distribution packages. Debian puts
# the game in /usr/games (-DRAYLIB_DOOM_INSTALL_BINDIR=games). With
# a raylib built here, --component raylibdoom leaves raylib's own
# library and headers out.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT RAYLIB_DOOM_ANDROID)
  include(GNUInstallDirs)
  set(RAYLIB_DOOM_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE STRING
    "Where cmake --install puts raylibdoom, relative to the prefix")
  set(LINUX_PKG ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux)
  install(TARGETS raylibdoom DESTINATION ${RAYLIB_DOOM_INSTALL_BINDIR}
    COMPONENT raylibdoom)
  install(FILES ${LINUX_PKG}/raylibdoom.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT raylibdoom)
  install(FILES ${LINUX_PKG}/raylibdoom.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT raylibdoom)
  install(FILES ${LINUX_PKG}/io.github.raydelto.raylibdoom.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT raylibdoom)
  install(FILES ${LINUX_PKG}/raylibdoom.6
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man6 COMPONENT raylibdoom)
endif()

# Parser regression tests: cmake --build build && ctest --test-dir build
# They use the host's sanitizers where it has them; some Debian
# architectures don't, so -DRAYLIB_DOOM_TEST_SANITIZERS=OFF builds
# them plain. The browser and Android builds have none.
if(EMSCRIPTEN OR RAYLIB_DOOM_ANDROID)
  return()
endif()
option(RAYLIB_DOOM_TEST_SANITIZERS
  "Build the tests with -fsanitize=address,undefined" ON)
enable_testing()
add_executable(midi_register_test tests/midi_register_test.c ${DOOM_DIR}/opl3.c)
target_include_directories(midi_register_test PRIVATE ${DOOM_DIR})
target_compile_definitions(midi_register_test PRIVATE NORMALUNIX LINUX)
target_link_libraries(midi_register_test PRIVATE m)
target_compile_options(midi_register_test PRIVATE ${DOOM_SIGNED_CHAR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32
   AND RAYLIB_DOOM_TEST_SANITIZERS)
  # No -fwrapv here: it would hide the signed-shift overflows that
  # the sanitizer is meant to catch.
  target_compile_options(midi_register_test PRIVATE
    -fsanitize=undefined -fno-sanitize-recover=undefined)
  target_link_options(midi_register_test PRIVATE -fsanitize=undefined)
endif()
add_test(NAME midi_register COMMAND midi_register_test)

add_executable(umapinfo_test tests/umapinfo_test.c ${DOOM_DIR}/u_mapinfo.c)
target_include_directories(umapinfo_test PRIVATE ${DOOM_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32
   AND RAYLIB_DOOM_TEST_SANITIZERS)
  target_compile_options(umapinfo_test PRIVATE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  target_link_options(umapinfo_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME umapinfo COMMAND umapinfo_test)

# OpenXR headset build: cmake -B build -DRAYLIB_DOOM_XR=ON
# Adds raylib_doom_xr next to raylibdoom, which is unchanged. The
# game is shown on a virtual screen in the headset; see i_xr.c.
option(RAYLIB_DOOM_XR "Also build raylib_doom_xr, for OpenXR headsets" OFF)
if(RAYLIB_DOOM_XR)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "RAYLIB_DOOM_XR supports only Linux (OpenGL on X11)")
  endif()

  # The system loader (libopenxr-dev on Debian/Ubuntu), found by
  # its CMake config or, for loaders built without one, by
  # pkg-config; else the SDK's.
  find_package(OpenXR CONFIG QUIET)
  if(NOT TARGET OpenXR::openxr_loader)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(OPENXR QUIET IMPORTED_TARGET GLOBAL openxr)
    endif()
    if(OPENXR_FOUND)
      message(STATUS "Using the OpenXR loader found by pkg-config")
      add_library(OpenXR::openxr_loader ALIAS PkgConfig::OPENXR)
    endif()
  endif()
  if(NOT TARGET OpenXR::openxr_loader)
    message(STATUS "No OpenXR SDK installed, building the OpenXR loader")
    include(FetchContent)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      OpenXR
      GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
      GIT_TAG release-1.0.34
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(OpenXR)
  endif()
  find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX)
  find_package(X11 REQUIRED)

  add_executable(raylib_doom_xr ${DOOM_SOURCES} ${DOOM_DIR}/i_xr.c)
  target_compile_definitions(raylib_doom_xr PRIVATE NORMALUNIX LINUX DOOM_XR)
  target_link_libraries(raylib_doom_xr PRIVATE
    raylib OpenXR::openxr_loader OpenGL::GLX X11::X11 m)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raylib_doom_xr PRIVATE
      -fwrapv -fno-strict-aliasing -Wall -Wno-unused-variable
      -Wno-unused-but-set-variable -Wno-unused-const-variable
      ${DOOM_SIGNED_CHAR})
  endif()

  # i_xr.c against a fake runtime: enumeration bounds and
  # swapchain image failures. Needs only rlgl.h, not raylib.
  add_executable(xr_test tests/xr_test.c)
  target_include_directories(xr_test PRIVATE ${DOOM_DIR}
    $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
  target_link_libraries(xr_test PRIVATE
    OpenXR::openxr_loader OpenGL::GLX X11::X11 m)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xr_test PRIVATE
      -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    target_link_options(xr_test PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME xr COMMAND xr_test)
endif()
