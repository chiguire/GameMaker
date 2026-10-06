/* Borland C++ 3.1 (16-bit, large model) compatibility for building the original GameMaker sources
 * with a modern compiler. Force-included (/FI) before every engine source file.
 *
 * Key decisions:
 *  - `int` becomes 16 bits, because game files are read as raw structs written by 16-bit code.
 *    All system headers are included FIRST so the macro never touches them.
 *  - far/near/huge vanish; far pointers are ordinary pointers into the emulated 1 MB address space
 *    (gm_dosmem), so MK_FP/FP_SEG/FP_OFF keep working and VGA memory at A000:0 is a plain buffer.
 *  - Hardware access (ports, interrupt vectors, BIOS keyboard) is routed to the platform layer.
 */
#ifndef GM_COMPAT_H
#define GM_COMPAT_H

#define GM_PORT 1
#define GM_ENUM16 : short   /* Borland enums are 16 bits wide; several end up inside on-disk structs */
#define MOUSE 1          /* playgame uses the "old mouse" interface (see PLAYGAME.C #error) */
#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_WARNINGS 1
#define _USE_MATH_DEFINES 1

#ifdef __cplusplus
#include <new>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <process.h>
#include <direct.h>
#include <signal.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "dosplat.h"

/* ---- Borland keywords and modifiers ---- */
#define far
#define near
#define huge
#define _far
#define _near
#define _huge
#define _FAR
#define _NEAR
#define _Cdecl
#define _cdecl
#define interrupt
#define _interrupt
#define pascal
#define cdecl

/* ---- far pointers ---- */
#define MK_FP(seg, off) ((void *)(gm_dosmem + ((uint32_t)(uint16_t)(seg) << 4) + (uint16_t)(off)))
#define FP_SEG(p) ((uint16_t)gm_fp_seg((const void *)(p)))
#define FP_OFF(p) ((uint16_t)gm_fp_off((const void *)(p)))
#define farmalloc(n) gm_farmalloc((uint32_t)(n))
#define farfree(p) gm_farfree((void *)(p))
#define farcoreleft() gm_farcoreleft()

/* ---- ports, interrupts, BIOS ---- */
#define inportb(p) gm_inportb((uint16_t)(p))
#define outportb(p, v) gm_outportb((uint16_t)(p), (uint8_t)(v))
#define inport(p) gm_inport((uint16_t)(p))
#define outport(p, v) gm_outport((uint16_t)(p), (uint16_t)(v))
#define getvect(n) ((void (*)(...))gm_getvect((int16_t)(n)))
#define setvect(n, h) gm_setvect((int16_t)(n), (gm_isr)(h))
#define enable() gm_int_enable()
#define disable() gm_int_disable()
#define int86(n, i, o) gm_int86((int16_t)(n), (i), (o))
#define int86x(n, i, o, s) gm_int86((int16_t)(n), (i), (o))
#define bioskey(c) gm_bioskey((int16_t)(c))
#define delay(ms) gm_delay((uint32_t)(ms))
#define sound(hz) gm_sound((uint16_t)(hz))
#define nosound() gm_nosound()

/* Borland pseudo-registers (_AX, _BH, ...) + geninterrupt */
#define _AX gm_pseudo.x.ax
#define _BX gm_pseudo.x.bx
#define _CX gm_pseudo.x.cx
#define _DX gm_pseudo.x.dx
#define _AL gm_pseudo.h.al
#define _AH gm_pseudo.h.ah
#define _BL gm_pseudo.h.bl
#define _BH gm_pseudo.h.bh
#define _CL gm_pseudo.h.cl
#define _CH gm_pseudo.h.ch
#define _DL gm_pseudo.h.dl
#define _DH gm_pseudo.h.dh
#define geninterrupt(n) gm_int86((int16_t)(n), &gm_pseudo, &gm_pseudo)

/* ---- name clashes with the modern C runtime ---- */
#define clock gm_clock                 /* engine tick counter vs <time.h> clock() */
#define strncmpi _strnicmp
#define main gm_game_main              /* the real main() lives in the port; it calls this */
static inline void gotoxy(int x, int y) { (void)x; (void)y; }  /* text-mode cursor: no text mode here */

/* ---- misc Borland runtime ---- */
/* Borland's own generator and macros (INCLUDE/STDLIB.H, CRTL/CLIB/RAND.C), so a fixed seed reproduces DOS runs */
#define rand() gm_rand()
#define srand(s) gm_srand((uint16_t)(s))
#define randomize() gm_srand((uint16_t)time(NULL))
#define random(n) ((int16_t)(((int32_t)gm_rand() * (int32_t)(n)) / 32768))

/* Borland packs structs on byte boundaries and game files are raw struct dumps, so do the same.
 * (System headers above were already parsed with their normal alignment.) */
#pragma pack(1)

/* Everything above uses real ints; from here on engine code sees the 16-bit int of the original. */
#define int short

#endif
