# The GameMaker design tools (code/GM), built like the player: the 1994 sources behind shim/gmcompat.h, with the
# assembly modules replaced by C++ (src/*_asm.cpp). The editors are compiled with GM_EDITOR, which turns off the
# playgame-only "old mouse" mode of GENC.C / WINDIO.C (see shim/gmcompat.h).
#
#   gm_editor(<name> [NEWMOUSE] MAIN <file in code/GM> SOURCES <files in code/GM> [PORT <files in port/src>])
#   NEWMOUSE: the program uses the TRANMOUS.HPP mouse stack instead of the INT 33h wrapper (OLDMOUSE.ASM)
#
# makes the target gm<name>. Sources are built per program, because the same files are compiled differently for the
# player (MOUSE) and the editors.

set(GM_EDITOR_ASM_REPLACEMENTS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx_asm.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/text_asm.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/input_asm.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/editor_globals.cpp)

function(gm_editor name)
  cmake_parse_arguments(E "NEWMOUSE" "MAIN" "SOURCES;PORT" ${ARGN})
  set(srcs)
  foreach(f ${E_SOURCES})
    list(APPEND srcs ${GM_SRC_GM}/${f})
  endforeach()
  foreach(f ${E_PORT})
    list(APPEND srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/${f})
  endforeach()
  # <main>.C followed by the entry point (edit_entry.h): one generated translation unit
  set(entry ${CMAKE_BINARY_DIR}/editors/${name}_entry.cpp)
  file(WRITE ${entry} "#include \"${GM_SRC_GM}/${E_MAIN}\"\n#include \"edit_entry.h\"\n")
  add_library(gm${name}_objs OBJECT ${entry} ${srcs} ${GM_EDITOR_ASM_REPLACEMENTS})
  set_source_files_properties(${entry} ${srcs} PROPERTIES LANGUAGE CXX)
  target_include_directories(gm${name}_objs PRIVATE ${GM_ENGINE_INCLUDES})
  target_compile_options(gm${name}_objs PRIVATE ${GM_SHIM_OPTIONS})
  target_compile_definitions(gm${name}_objs PRIVATE GM_EDITOR=1)
  if(E_NEWMOUSE)
    target_compile_definitions(gm${name}_objs PRIVATE GM_NEWMOUSE=1)
  endif()
  set_target_properties(gm${name}_objs PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED OFF)
  add_executable(gm${name} ${CMAKE_CURRENT_SOURCE_DIR}/src/gmedit_main.cpp $<TARGET_OBJECTS:gm${name}_objs>)
  set_target_properties(gm${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(gm${name} PRIVATE gmplat)
  if(NOT WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(gm${name} PRIVATE ${CMAKE_DL_LIBS} m)
  endif()
endfunction()

# Programs that use the old (INT 33h) mouse interface
gm_editor(menu    MAIN MENU.C    SOURCES WINDIO.C GENC.C GRAPHC.C FINDFILE.CPP)
gm_editor(utility MAIN UTILITY.C SOURCES WINDIO.C GENC.C GRAPHC.C FINDFILE.CPP JSTICKC.C)

# Programs that use the new (interrupt-driven, class based) mouse and input stack of TRANMOUS.HPP / GENINPUT.CPP
set(GM_EDITOR_INPUT_SOURCES GENCLASS.CPP GASCLASS.CPP GENINPUT.CPP TIMER.CPP WINDCLSS.CPP FACELIFT.C)
gm_editor(palchos NEWMOUSE MAIN PALCHOS.C
  SOURCES WINDIO.C GENC.C GRAPHC.C FINDFILE.CPP PALC.C ${GM_EDITOR_INPUT_SOURCES}
  PORT newmouse_port.cpp)
