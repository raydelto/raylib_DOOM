# Browser build: emcmake cmake -B build-web && cmake --build build-web
# Makes raylibdoom.html, .js, .wasm and .data (Freedoom Phase 1),
# plus freedoom-COPYING.txt. See the README for serving it.

set(FREEDOOM_VERSION 0.13.0)
set(FREEDOOM_SHA256
  3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59)
set(FREEDOOM_ZIP "" CACHE FILEPATH
  "freedoom-${FREEDOOM_VERSION}.zip to use instead of downloading it")

# Freedoom is BSD licensed and may be bundled. It is fetched here
# rather than kept in the repository.
set(FREEDOOM_DIR ${CMAKE_BINARY_DIR}/freedoom)
set(FREEDOOM_WAD ${FREEDOOM_DIR}/freedoom-${FREEDOOM_VERSION}/freedoom1.wad)
set(FREEDOOM_COPYING ${FREEDOOM_DIR}/freedoom-${FREEDOOM_VERSION}/COPYING.txt)
if(NOT EXISTS ${FREEDOOM_WAD})
  if(NOT FREEDOOM_ZIP)
    set(FREEDOOM_ZIP ${FREEDOOM_DIR}/freedoom-${FREEDOOM_VERSION}.zip)
    message(STATUS "Downloading Freedoom ${FREEDOOM_VERSION}")
    file(DOWNLOAD
      https://github.com/freedoom/freedoom/releases/download/v${FREEDOOM_VERSION}/freedoom-${FREEDOOM_VERSION}.zip
      ${FREEDOOM_ZIP}
      EXPECTED_HASH SHA256=${FREEDOOM_SHA256}
      STATUS status)
    list(GET status 0 code)
    if(code)
      message(FATAL_ERROR "Could not download Freedoom: ${status}")
    endif()
  else()
    file(SHA256 ${FREEDOOM_ZIP} hash)
    if(NOT hash STREQUAL FREEDOOM_SHA256)
      message(FATAL_ERROR "${FREEDOOM_ZIP} is not freedoom-${FREEDOOM_VERSION}.zip")
    endif()
  endif()
  file(MAKE_DIRECTORY ${FREEDOOM_DIR})
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf ${FREEDOOM_ZIP}
      freedoom-${FREEDOOM_VERSION}/freedoom1.wad
      freedoom-${FREEDOOM_VERSION}/COPYING.txt
    WORKING_DIRECTORY ${FREEDOOM_DIR}
    RESULT_VARIABLE code)
  if(code OR NOT EXISTS ${FREEDOOM_WAD})
    message(FATAL_ERROR "Could not unpack ${FREEDOOM_ZIP}")
  endif()
endif()

set(DOOM_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)

set_target_properties(raylibdoom PROPERTIES
  SUFFIX .html
  LINK_DEPENDS "${DOOM_SHELL};${FREEDOOM_WAD}")

target_link_options(raylibdoom PRIVATE
  # The level data and the zone heap (16 MB) fit in the first 64 MB;
  # PWADs load into the zone too, so let it grow.
  -sINITIAL_MEMORY=64MB
  -sALLOW_MEMORY_GROWTH=1
  -sSTACK_SIZE=1MB
  -sENVIRONMENT=web
  # The page starts the game on the first click, so the audio
  # context can start and the arguments can be chosen first.
  -sINVOKE_RUN=0
  # raylib's miniaudio reads Module.HEAPF32 from its Web Audio callback.
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,addRunDependency,removeRunDependency,HEAPF32
  -lidbfs.js
  --shell-file ${DOOM_SHELL}
  --preload-file ${FREEDOOM_WAD}@/doom/freedoom1.wad)

add_custom_command(TARGET raylibdoom POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FREEDOOM_COPYING}
    $<TARGET_FILE_DIR:raylibdoom>/freedoom-COPYING.txt)
