/* Platform layer that stands in for the DOS/BIOS/hardware services the 1994 engine used.
 *
 * Included by engine code (through shim/gmcompat.h, where `int` is redefined to 16 bits) and by the
 * raylib-side implementation (dosplat.c, plain 32-bit int), so every signature here uses fixed-width
 * types only.
 */
#ifndef GM_DOSPLAT_H
#define GM_DOSPLAT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emulated real-mode address space. MK_FP(seg,off) == gm_dosmem + seg*16 + off.
 * VGA graphics memory lives at A000:0000 (gm_dosmem + 0xA0000). */
#define GM_DOSMEM_SIZE 0x110000u
extern uint8_t gm_dosmem[GM_DOSMEM_SIZE];

/* "far heap": allocations come from conventional memory inside gm_dosmem so segment math works. */
void    *gm_farmalloc(uint32_t bytes);
void     gm_farfree(void *p);
uint32_t gm_farcoreleft(void);
uint32_t gm_farsize(const void *p);          /* size of an allocation made by gm_farmalloc (0 if unknown) */
uint16_t gm_fp_seg(const void *p);
uint16_t gm_fp_off(const void *p);

/* I/O ports (VGA registers, PIC, PIT, keyboard controller, joystick, Sound Blaster, ...). */
uint8_t  gm_inportb(uint16_t port);
void     gm_outportb(uint16_t port, uint8_t value);
uint16_t gm_inport(uint16_t port);
void     gm_outport(uint16_t port, uint16_t value);

/* Interrupt vectors: the engine hooks INT 08h (timer) and INT 09h (keyboard). */
typedef void (*gm_isr)(void);
gm_isr   gm_getvect(int16_t n);
void     gm_setvect(int16_t n, gm_isr handler);
void     gm_int_enable(void);
void     gm_int_disable(void);

/* BIOS keyboard (INT 16h): cmd 0 = wait+read, 1 = peek, 2 = shift flags. Returns scan<<8 | ascii. */
uint16_t gm_bioskey(int16_t cmd);
int16_t  gm_kbhit(void);
int16_t  gm_getch(void);

#pragma pack(push, 1)
/* BIOS/DOS software interrupts (INT 10h video, INT 33h mouse, ...). */
union REGS {
    struct { uint16_t ax, bx, cx, dx, si, di, cflag, flags; } x;
    struct { uint8_t al, ah, bl, bh, cl, ch, dl, dh; } h;
};
struct SREGS { uint16_t es, cs, ss, ds; };
int16_t  gm_int86(int16_t intno, union REGS *in, union REGS *out);
extern union REGS gm_pseudo;                  /* Borland pseudo-registers _AX, _BH, ... */

/* Borland findfirst/findnext. */
struct ffblk {
    intptr_t ff_handle;      /* replaces Borland's reserved DOS DTA bytes */
    char     ff_attrib;
    uint16_t ff_ftime;
    uint16_t ff_fdate;
    int32_t  ff_fsize;
    char     ff_name[260];
};
#pragma pack(pop)
int16_t  gm_findfirst(const char *pattern, struct ffblk *f, int16_t attrib);
int16_t  gm_findnext(struct ffblk *f);

/* Mouse in the INT 33h driver's virtual units (640 x 200, whatever the video mode). */
void     gm_mouse_show(int16_t on);
void     gm_mouse_get(int32_t *vx, int32_t *vy, int32_t *buttons);
void     gm_mouse_set(int32_t vx, int32_t vy);

/* Borland C runtime rand()/srand(): a 32-bit LCG, reproduced so seeded runs match DOS exactly. */
int16_t  gm_rand(void);
void     gm_srand(uint16_t seed);

/* Time and sound. */
void     gm_delay(uint32_t ms);
void     gm_sound(uint16_t hz);
void     gm_nosound(void);

/* Services the main loop must call regularly: polls the window, raises keyboard/timer "interrupts"
 * and presents the VGA frame. Safe to call from anywhere in the engine's waiting loops. */
void     gm_pump(void);

#ifdef __cplusplus
}
#endif
#endif
