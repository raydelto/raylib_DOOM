cmake_minimum_required(VERSION 3.16)
project(raylib_doom C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Use an installed raylib if there is one, otherwise build it.
find_package(raylib 5.0 QUIET)
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
  wi_stuff.c z_zone.c
  opl3.c)
list(TRANSFORM DOOM_SOURCES PREPEND ${DOOM_DIR}/)

add_executable(raylibdoom ${DOOM_SOURCES})
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

# Plain char is unsigned on Linux arm/aarch64, but the code assumes it
# is signed, as on x86: ticcmd_t's forwardmove and sidemove are char,
# so backward and left moves came out as big forward/right ones and
# demos desynced. x86 and Apple/Windows arm64 already have a signed
# char, so this changes only builds where it was unsigned.
# d_ticcmd.h refuses to compile without it.
set(DOOM_SIGNED_CHAR "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64|arm)")
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

# Parser regression tests: cmake --build build && ctest --test-dir build
enable_testing()
add_executable(midi_register_test tests/midi_register_test.c ${DOOM_DIR}/opl3.c)
target_include_directories(midi_register_test PRIVATE ${DOOM_DIR})
target_compile_definitions(midi_register_test PRIVATE NORMALUNIX LINUX)
target_link_libraries(midi_register_test PRIVATE m)
target_compile_options(midi_register_test PRIVATE ${DOOM_SIGNED_CHAR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
  # No -fwrapv here: it would hide the signed-shift overflows that
  # the sanitizer is meant to catch.
  target_compile_options(midi_register_test PRIVATE
    -fsanitize=undefined -fno-sanitize-recover=undefined)
  target_link_options(midi_register_test PRIVATE -fsanitize=undefined)
endif()
add_test(NAME midi_register COMMAND midi_register_test)
