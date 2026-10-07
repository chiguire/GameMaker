cmake_minimum_required(VERSION 3.20)
project(gamemaker_port C CXX)

set(CMAKE_C_STANDARD 99)
if(MSVC)
  add_compile_options(/Zi)           # debug info for crash reports
  add_link_options(/DEBUG)
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# ymfm (BSD-3-Clause): cycle-accurate Yamaha FM chips; the OPL2 (YM3812) is the Sound Blaster's FM synthesizer.
# It has no CMake project of its own, so its sources are added directly.
FetchContent_Declare(ymfm
  GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
  GIT_TAG 81aec25ccbb98f4873a255f7551ac4dadac59b4a)
FetchContent_GetProperties(ymfm)
if(NOT ymfm_POPULATED)
  FetchContent_Populate(ymfm)
endif()

add_library(gmplat STATIC src/fb.c src/gif.c src/audio.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp)
target_include_directories(gmplat PRIVATE ${ymfm_SOURCE_DIR}/src)
set_target_properties(gmplat PROPERTIES CXX_STANDARD 17)
target_include_directories(gmplat PUBLIC src)
target_link_libraries(gmplat PUBLIC raylib)

add_executable(gmview src/gmview.c)
target_link_libraries(gmview PRIVATE gmplat)

# Reader/comparer for frame dumps from the instrumented DOS playgame.
add_executable(fdump tools/fdump.c)
target_link_libraries(fdump PRIVATE raylib)

# ---------------------------------------------------------------------------------------------
# The original GameMaker engine (code/), compiled in place as C++ behind shim/gmcompat.h
# (Borland 16-bit compatibility: 16-bit int, far pointers into an emulated 1 MB address space).
# ---------------------------------------------------------------------------------------------
set(GM_CODE ${CMAKE_CURRENT_SOURCE_DIR}/../code)
set(GM_ASM_REPLACEMENTS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx_asm.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/text_asm.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/input_asm.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/vgaobj.cpp)
set(GM_ENGINE_SOURCES
  ${GM_CODE}/GM/PLAYGAME.C ${GM_CODE}/GM/WINDIO.C ${GM_CODE}/GM/GENC.C
  ${GM_CODE}/GM/GIFC.C ${GM_CODE}/GM/PALC.C ${CMAKE_CURRENT_SOURCE_DIR}/src/svga_port.cpp ${GM_CODE}/GM/HISCOREL.C
  ${GM_CODE}/GM/JSTICKC.C ${GM_CODE}/GM/DIRTRECT.C ${GM_CODE}/GM/GRAPHC.C ${GM_CODE}/GM/FINDFILE.CPP
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sound_port.cpp ${GM_CODE}/GM/SBMUSIC.C ${GM_CODE}/DRV/FILECLSS.CPP ${GM_CODE}/Image/fli/FLI.CPP)
set_source_files_properties(${GM_ENGINE_SOURCES} PROPERTIES LANGUAGE CXX)

add_library(engine_objs OBJECT ${GM_ENGINE_SOURCES} ${GM_ASM_REPLACEMENTS})
target_include_directories(engine_objs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GM_CODE}/GM ${GM_CODE}/DRV ${GM_CODE}/Image/fli)
target_compile_options(engine_objs PRIVATE
  /FI${CMAKE_CURRENT_SOURCE_DIR}/shim/gmcompat.h /permissive /w /GS-)   # /GS-: the 1994 code has unchecked buffers
# Same replay frame-dump hook as the instrumented DOS build (see baseline/README.md).
target_compile_definitions(engine_objs PRIVATE FRAMEDUMP)

# Platform layer (DOS services on raylib) joins the framebuffer library.
target_sources(gmplat PRIVATE src/dosplat.c src/dosfind.c)

add_executable(gmplay src/gmplay_main.cpp $<TARGET_OBJECTS:engine_objs>)
target_link_libraries(gmplay PRIVATE gmplat)

# Timeline of a captured WAV (GM_WAV=...), to check audio output without listening.
add_executable(wavstat tools/wavstat.c)
if(NOT MSVC)
  target_link_libraries(wavstat PRIVATE m)
endif()

# Offline CMF renderer: the engine's own music code (SBMUSIC.C + FM driver) on a manual clock, writing a WAV.
add_executable(cmf2wav tools/cmf2wav.cpp tools/cmf2wav_main.cpp src/sound_port.cpp ${GM_CODE}/GM/SBMUSIC.C)
set_source_files_properties(tools/cmf2wav.cpp src/sound_port.cpp ${GM_CODE}/GM/SBMUSIC.C PROPERTIES
  LANGUAGE CXX COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/shim/gmcompat.h;/permissive;/w;/GS-")
target_include_directories(cmf2wav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GM_CODE}/GM ${GM_CODE}/DRV ${GM_CODE}/Image/fli)
target_link_libraries(cmf2wav PRIVATE gmplat)

# Offline VOC decoder test: runs a .voc through the mixer and writes a WAV.
add_executable(voc2wav tools/voc2wav.cpp)
target_include_directories(voc2wav PRIVATE src)
target_link_libraries(voc2wav PRIVATE gmplat)

# Self-checking audio test (speaker pitch, OPL2 note, VOC playback); no audio device needed.
add_executable(audiotest tools/audiotest.cpp)
target_include_directories(audiotest PRIVATE src)
target_link_libraries(audiotest PRIVATE gmplat)
