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

add_library(gmplat STATIC src/fb.c src/gif.c)
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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sound_port.cpp ${GM_CODE}/DRV/FILECLSS.CPP ${GM_CODE}/Image/fli/FLI.CPP)
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

# Build-time helper: turns a Windows raster .FON into the C glyph arrays in src/fonts.h.
add_executable(fon2h tools/fon2h.c)
