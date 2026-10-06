/* Implementation of the DOS platform layer on top of raylib. See dosplat.h.
 *
 * Everything is cooperative and single-threaded: the engine's "interrupt handlers" (INT 08h timer,
 * INT 09h keyboard) are invoked from gm_pump(), which the engine's waiting loops reach through
 * bioskey(), delay(), reads of the VGA status port and an explicit call in the main game loop.
 */
#include "dosplat.h"
#include "fb.h"
#include "audio.h"
#include "font8x8.h"
#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t gm_dosmem[GM_DOSMEM_SIZE];
volatile uint32_t gm_heartbeat;           /* bumped on every gm_pump(); used by the hang watchdog in gmplay_main.cpp */
union REGS gm_pseudo;

#define VRAM (gm_dosmem + 0xA0000)
#define TEXT (gm_dosmem + 0xB8000)

/* ---------------------------------------------------------------------------------------------
 * Far heap: first-fit allocator over conventional memory (segments 0x1000..0x9FFF).
 * ------------------------------------------------------------------------------------------- */
#define ARENA_BASE 0x10000u
#define ARENA_END  0xA0000u
#define MAX_BLOCKS 1024

typedef struct { uint32_t off, size; int used; } Block;
static Block blocks[MAX_BLOCKS];
static int nblocks;

static void heap_init(void)
{
    blocks[0] = (Block){ ARENA_BASE, ARENA_END - ARENA_BASE, 0 };
    nblocks = 1;
}

void *gm_farmalloc(uint32_t bytes)
{
    if (!nblocks) heap_init();
    uint32_t need = (bytes + 15u) & ~15u;
    if (need == 0) need = 16;
    for (int i = 0; i < nblocks; i++) {
        if (blocks[i].used || blocks[i].size < need) continue;
        if (blocks[i].size > need && nblocks < MAX_BLOCKS) {   /* split */
            memmove(&blocks[i + 2], &blocks[i + 1], (nblocks - i - 1) * sizeof(Block));
            blocks[i + 1] = (Block){ blocks[i].off + need, blocks[i].size - need, 0 };
            blocks[i].size = need;
            nblocks++;
        }
        blocks[i].used = 1;
        return gm_dosmem + blocks[i].off;
    }
    return NULL;
}

void gm_farfree(void *p)
{
    if (!p || !nblocks) return;
    uint32_t off = (uint32_t)((uint8_t *)p - gm_dosmem);
    for (int i = 0; i < nblocks; i++) {
        if (blocks[i].off != off || !blocks[i].used) continue;
        blocks[i].used = 0;
        if (i + 1 < nblocks && !blocks[i + 1].used) {          /* merge with next */
            blocks[i].size += blocks[i + 1].size;
            memmove(&blocks[i + 1], &blocks[i + 2], (nblocks - i - 2) * sizeof(Block));
            nblocks--;
        }
        if (i > 0 && !blocks[i - 1].used) {                    /* merge with previous */
            blocks[i - 1].size += blocks[i].size;
            memmove(&blocks[i], &blocks[i + 1], (nblocks - i - 1) * sizeof(Block));
            nblocks--;
        }
        return;
    }
}

uint32_t gm_farcoreleft(void)
{
    uint32_t n = 0;
    if (!nblocks) heap_init();
    for (int i = 0; i < nblocks; i++) if (!blocks[i].used) n += blocks[i].size;
    return n;
}

uint16_t gm_fp_seg(const void *p) { return (uint16_t)(((const uint8_t *)p - gm_dosmem) >> 4); }
uint16_t gm_fp_off(const void *p) { return (uint16_t)(((const uint8_t *)p - gm_dosmem) & 15); }

/* ---------------------------------------------------------------------------------------------
 * VGA state: DAC palette (6-bit), video mode, mouse cursor
 * ------------------------------------------------------------------------------------------- */
static uint8_t dac[256][3];
static int dac_wr_idx, dac_wr_phase, dac_rd_idx, dac_rd_phase;
static int video_mode = 3;                         /* 3 = 80x25 text, 0x13 = 320x200x256 */

/* Attribute-controller palette of the text modes: attribute 0..15 -> DAC index */
static const uint8_t atc_text[16] = { 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F };

static void default_palette(void)
{
    memset(dac, 0, sizeof dac);
    for (int i = 0; i < 64; i++) {                 /* EGA colours: bits (R,G,B) with 0x2A / 0x15 weights */
        dac[i][0] = (uint8_t)(((i >> 2) & 1) * 0x2A + ((i >> 5) & 1) * 0x15);
        dac[i][1] = (uint8_t)(((i >> 1) & 1) * 0x2A + ((i >> 4) & 1) * 0x15);
        dac[i][2] = (uint8_t)((i & 1) * 0x2A + ((i >> 3) & 1) * 0x15);
    }
    if (video_mode == 0x13) {                      /* mode 13h: 16 EGA colours, 16 greys, rest black */
        uint8_t ega[16][3];
        for (int i = 0; i < 16; i++) memcpy(ega[i], dac[atc_text[i]], 3);
        static const uint8_t grey[16] = { 0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63 };
        memset(dac, 0, sizeof dac);
        for (int i = 0; i < 16; i++) { memcpy(dac[i], ega[i], 3); dac[16 + i][0] = dac[16 + i][1] = dac[16 + i][2] = grey[i]; }
    }
}

static int mouse_shown;
static FbRect game_rect = { 0, 0, 960, 600 };      /* where the VGA image landed in the window */

/* ---------------------------------------------------------------------------------------------
 * Window / presentation
 * ------------------------------------------------------------------------------------------- */
static int window_open;
static double last_present;

static void ensure_window(void)
{
    if (window_open) return;
    SetTraceLogLevel(LOG_WARNING);
    fb_open("GameMaker", 3);
    window_open = 1;
    last_present = GetTime();
}

#define RGBA(r6, g6, b6) ((uint32_t)(((r6) << 2) | ((r6) >> 4)) | ((uint32_t)(((g6) << 2) | ((g6) >> 4)) << 8) | \
                          ((uint32_t)(((b6) << 2) | ((b6) >> 4)) << 16) | 0xFF000000u)

static uint32_t text_rgba[640 * 400];

static void render_text(int mouse_cell_x, int mouse_cell_y)
{
    for (int cy = 0; cy < 25; cy++)
        for (int cx = 0; cx < 80; cx++) {
            uint8_t ch = TEXT[(cy * 80 + cx) * 2], attr = TEXT[(cy * 80 + cx) * 2 + 1];
            if (cx == mouse_cell_x && cy == mouse_cell_y) attr = (uint8_t)((attr >> 4) | (attr << 4));   /* software cursor */
            const uint8_t *fg = dac[atc_text[attr & 15]], *bg = dac[atc_text[(attr >> 4) & 7]];
            uint32_t fgc = RGBA(fg[0], fg[1], fg[2]), bgc = RGBA(bg[0], bg[1], bg[2]);
            for (int row = 0; row < 8; row++) {
                uint8_t bits = font8x8[ch * 8 + row];
                for (int col = 0; col < 8; col++) {
                    uint32_t c = (bits & (0x80 >> col)) ? fgc : bgc;
                    int x = cx * 8 + col, y = cy * 16 + row * 2;
                    text_rgba[y * 640 + x] = c;               /* 8x8 glyph stretched to 8x16 */
                    text_rgba[(y + 1) * 640 + x] = c;
                }
            }
        }
}

static void present(void)
{
    int32_t mx, my, mb;
    gm_mouse_get(&mx, &my, &mb);
    if (video_mode == 0x13) {
        memcpy(fb_pix, VRAM, FB_W * FB_H);
        for (int i = 0; i < 256; i++) fb_set_vga_pal(i, dac[i][0], dac[i][1], dac[i][2]);
        if (mouse_shown) {                                 /* small arrow cursor drawn over the image */
            int px = (int)(mx / 2), py = (int)my;
            for (int r = 0; r < 10; r++)
                for (int c = 0; c <= r && c < 7; c++) {
                    int x = px + c, y = py + r;
                    if (x < FB_W && y < FB_H) fb_pix[y * FB_W + x] = (c == 0 || c == r || r == 9) ? 0 : 15;
                }
        }
        fb_present();
        /* fb_present drew 320x200; remember the letterbox for mouse mapping */
        float sw = (float)GetRenderWidth(), sh = (float)GetRenderHeight();
        float s = sw / FB_W < sh / FB_H ? sw / FB_W : sh / FB_H;
        game_rect = (FbRect){ (sw - FB_W * s) / 2, (sh - FB_H * s) / 2, FB_W * s, FB_H * s };
    } else {
        render_text(mouse_shown ? (int)(mx / 8) : -1, mouse_shown ? (int)(my / 8) : -1);
        fb_draw_rgba(text_rgba, 640, 400, &game_rect);
    }
    last_present = GetTime();

    /* Test hook: GM_SHOT=<file.png> saves the exact emulated screen after GM_SHOT_AFTER seconds (default 3) and exits. */
    static const char *shot;
    static double shot_after = -1;
    if (shot_after < 0) {
        shot = getenv("GM_SHOT");
        shot_after = getenv("GM_SHOT_AFTER") ? atof(getenv("GM_SHOT_AFTER")) : 3.0;
    }
    /* If the name contains %d, a numbered screenshot is also saved every GM_SHOT_EVERY seconds (default 4). */
    static double next_seq = -1;
    if (shot && strchr(shot, '%')) {
        double every = getenv("GM_SHOT_EVERY") ? atof(getenv("GM_SHOT_EVERY")) : 4.0;
        if (next_seq < 0) next_seq = every;
        if (GetTime() >= next_seq) {
            static int seq;
            char name[512];
            snprintf(name, sizeof name, shot, seq++);
            if (video_mode == 0x13) fb_save_png(name);
            else { Image img = { text_rgba, 640, 400, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }; ExportImage(img, name); }
            next_seq += every;
        }
        if (GetTime() >= shot_after) { fb_close(); exit(0); }
    } else if (shot && GetTime() >= shot_after) {
        if (video_mode == 0x13) fb_save_png(shot);
        else { Image img = { text_rgba, 640, 400, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }; ExportImage(img, shot); }
        fb_close();
        exit(0);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Interrupt vectors, PIT timer, PIC, keyboard
 * ------------------------------------------------------------------------------------------- */
static void default_isr(void) {}
static gm_isr vectors[256];
static int in_isr;

gm_isr gm_getvect(int16_t n) { return vectors[n & 255] ? vectors[n & 255] : default_isr; }
void gm_setvect(int16_t n, gm_isr h) { vectors[n & 255] = h; }
void gm_int_enable(void) {}
void gm_int_disable(void) {}

static uint32_t pit_divisor = 0x10000;      /* 0 == 65536: 18.2 Hz */
static int pit_lo_hi;
static double next_tick;

static uint8_t kbd_data;                    /* port 0x60 */
static uint8_t pic_mask;                    /* port 0x21: bit 1 masks the keyboard IRQ */
static uint8_t kbd_held[64];                /* scan codes raised while IRQ 1 was masked */
static int kbd_held_n;

typedef struct { int rkey; uint8_t scan; char ascii, shifted; } KeyMap;
static const KeyMap keymap[] = {
    {KEY_ESCAPE, 0x01, 27, 27}, {KEY_ONE, 0x02, '1', '!'}, {KEY_TWO, 0x03, '2', '@'},
    {KEY_THREE, 0x04, '3', '#'}, {KEY_FOUR, 0x05, '4', '$'}, {KEY_FIVE, 0x06, '5', '%'},
    {KEY_SIX, 0x07, '6', '^'}, {KEY_SEVEN, 0x08, '7', '&'}, {KEY_EIGHT, 0x09, '8', '*'},
    {KEY_NINE, 0x0A, '9', '('}, {KEY_ZERO, 0x0B, '0', ')'}, {KEY_MINUS, 0x0C, '-', '_'},
    {KEY_EQUAL, 0x0D, '=', '+'}, {KEY_BACKSPACE, 0x0E, 8, 8}, {KEY_TAB, 0x0F, 9, 9},
    {KEY_Q, 0x10, 'q', 'Q'}, {KEY_W, 0x11, 'w', 'W'}, {KEY_E, 0x12, 'e', 'E'}, {KEY_R, 0x13, 'r', 'R'},
    {KEY_T, 0x14, 't', 'T'}, {KEY_Y, 0x15, 'y', 'Y'}, {KEY_U, 0x16, 'u', 'U'}, {KEY_I, 0x17, 'i', 'I'},
    {KEY_O, 0x18, 'o', 'O'}, {KEY_P, 0x19, 'p', 'P'}, {KEY_LEFT_BRACKET, 0x1A, '[', '{'},
    {KEY_RIGHT_BRACKET, 0x1B, ']', '}'}, {KEY_ENTER, 0x1C, 13, 13}, {KEY_LEFT_CONTROL, 0x1D, 0, 0},
    {KEY_A, 0x1E, 'a', 'A'}, {KEY_S, 0x1F, 's', 'S'}, {KEY_D, 0x20, 'd', 'D'}, {KEY_F, 0x21, 'f', 'F'},
    {KEY_G, 0x22, 'g', 'G'}, {KEY_H, 0x23, 'h', 'H'}, {KEY_J, 0x24, 'j', 'J'}, {KEY_K, 0x25, 'k', 'K'},
    {KEY_L, 0x26, 'l', 'L'}, {KEY_SEMICOLON, 0x27, ';', ':'}, {KEY_APOSTROPHE, 0x28, '\'', '"'},
    {KEY_GRAVE, 0x29, '`', '~'}, {KEY_LEFT_SHIFT, 0x2A, 0, 0}, {KEY_BACKSLASH, 0x2B, '\\', '|'},
    {KEY_Z, 0x2C, 'z', 'Z'}, {KEY_X, 0x2D, 'x', 'X'}, {KEY_C, 0x2E, 'c', 'C'}, {KEY_V, 0x2F, 'v', 'V'},
    {KEY_B, 0x30, 'b', 'B'}, {KEY_N, 0x31, 'n', 'N'}, {KEY_M, 0x32, 'm', 'M'}, {KEY_COMMA, 0x33, ',', '<'},
    {KEY_PERIOD, 0x34, '.', '>'}, {KEY_SLASH, 0x35, '/', '?'}, {KEY_RIGHT_SHIFT, 0x36, 0, 0},
    {KEY_LEFT_ALT, 0x38, 0, 0}, {KEY_SPACE, 0x39, ' ', ' '},
    {KEY_F1, 0x3B, 0, 0}, {KEY_F2, 0x3C, 0, 0}, {KEY_F3, 0x3D, 0, 0}, {KEY_F4, 0x3E, 0, 0},
    {KEY_F5, 0x3F, 0, 0}, {KEY_F6, 0x40, 0, 0}, {KEY_F7, 0x41, 0, 0}, {KEY_F8, 0x42, 0, 0},
    {KEY_F9, 0x43, 0, 0}, {KEY_F10, 0x44, 0, 0},
    /* cursor block: the engine treats these like the numeric keypad (no E0 prefix) */
    {KEY_UP, 0x48, 0, 0}, {KEY_LEFT, 0x4B, 0, 0}, {KEY_RIGHT, 0x4D, 0, 0}, {KEY_DOWN, 0x50, 0, 0},
    {KEY_HOME, 0x47, 0, 0}, {KEY_PAGE_UP, 0x49, 0, 0}, {KEY_END, 0x4F, 0, 0}, {KEY_PAGE_DOWN, 0x51, 0, 0},
    {KEY_INSERT, 0x52, 0, 0}, {KEY_DELETE, 0x53, 0, 0},
};
#define NKEYS ((int)(sizeof keymap / sizeof keymap[0]))
static uint8_t key_down[NKEYS];
static uint8_t synth_down[NKEYS];           /* keys held by the GM_TYPE test hook, merged with the real keyboard */

/* BIOS keyboard buffer, filled only while no program handler owns INT 9 */
static uint16_t bios_buf[32];
static int bios_n;

static int shift_held(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }

static void raise_kbd(uint8_t code)
{
    kbd_data = code;
    in_isr++;
    vectors[9]();
    in_isr--;
}

static void key_event(int idx, int down)
{
    const KeyMap *k = &keymap[idx];
    uint8_t code = k->scan | (down ? 0 : 0x80);
    if (vectors[9]) {
        if (pic_mask & 2) { if (kbd_held_n < 64) kbd_held[kbd_held_n++] = code; }   /* IRQ 1 masked: latch it */
        else raise_kbd(code);
    } else if (down) {
        char a = shift_held() ? k->shifted : k->ascii;
        if (bios_n < 32) bios_buf[bios_n++] = (uint16_t)((k->scan << 8) | (uint8_t)a);
    }
}

static void poll_keys(void)
{
    for (int i = 0; i < NKEYS; i++) {
        int d = IsKeyDown(keymap[i].rkey) || synth_down[i];
        if (d != key_down[i]) { key_down[i] = (uint8_t)d; key_event(i, d); }
    }
}

/* Test hook: GM_TYPE="text" types into the engine after GM_TYPE_AT seconds (default 2), one key every
 * 80 ms. In the text: \n = Enter, \b = Backspace, \e = Escape, \l \r \u \d = arrow keys, \\ = backslash,
 * and "\hNN" holds the key typed next for NN * 80 ms (so a game can be steered). Used by automated UI tests. */
static int find_key_for_char(char c, int *shift)
{
    for (int i = 0; i < NKEYS; i++) {
        if (keymap[i].ascii == c && c) { *shift = 0; return i; }
        if (keymap[i].shifted == c && c && keymap[i].shifted != keymap[i].ascii) { *shift = 1; return i; }
    }
    return -1;
}

static void type_keys(void)
{
    static const char *buf;
    static int pos = -1, held = -1, hold_left;
    static double next;
    if (pos < 0) {
        buf = getenv("GM_TYPE");
        pos = 0;
        next = GetTime() + (getenv("GM_TYPE_AT") ? atof(getenv("GM_TYPE_AT")) : 2.0);
        if (!buf) return;
    }
    if (!buf || GetTime() < next) return;
    next = GetTime() + 0.08;
    if (held >= 0) {                                   /* release the key typed on the previous step */
        if (hold_left-- > 0) return;
        synth_down[held] = 0; held = -1; return;
    }
    if (!buf[pos]) return;
    char c = buf[pos++];
    int idx = -1, shift = 0, hold = 0;
    if (c == '\\') {
        char e = buf[pos++];
        if (e == 'w') { next = GetTime() + ((buf[pos] - '0') * 10 + (buf[pos + 1] - '0')) * 0.08; pos += 2; return; }   /* \wNN: pause */
        if (e == 'h') { hold = (buf[pos] - '0') * 10 + (buf[pos + 1] - '0'); pos += 2; c = buf[pos] == '\\' ? (pos++, buf[pos++]) : buf[pos++]; e = c; }
        switch (e) {
        case 'n': idx = 27; break;                      /* Enter */
        default:
            for (int i = 0; i < NKEYS; i++) {
                int want = e == 'b' ? KEY_BACKSPACE : e == 'e' ? KEY_ESCAPE : e == 'l' ? KEY_LEFT : e == 'r' ? KEY_RIGHT :
                           e == 'u' ? KEY_UP : e == 'd' ? KEY_DOWN : e == '\\' ? KEY_BACKSLASH : -1;
                if (keymap[i].rkey == want) { idx = i; break; }
            }
        }
        for (int i = 0; e == 'n' && i < NKEYS; i++) if (keymap[i].rkey == KEY_ENTER) idx = i;
    } else idx = find_key_for_char(c, &shift);
    if (idx < 0) return;
    held = idx; hold_left = hold;
    synth_down[idx] = 1;
}

static void unmask_kbd(void)
{
    while (kbd_held_n && !(pic_mask & 2) && vectors[9]) {
        uint8_t code = kbd_held[0];
        memmove(kbd_held, kbd_held + 1, --kbd_held_n);
        raise_kbd(code);
    }
}

static void fire_timer(void)
{
    double now = GetTime();
    double period = (pit_divisor ? pit_divisor : 0x10000) / 1193182.0;
    if (next_tick == 0) next_tick = now + period;
    int budget = 8;                                  /* don't spiral after a stall */
    while (now >= next_tick && budget--) {
        next_tick += period;
        if (vectors[8] && !(pic_mask & 1)) { in_isr++; vectors[8](); in_isr--; }
    }
    if (now >= next_tick) next_tick = now + period;
}

void gm_pump(void)
{
    gm_heartbeat++;
    if (in_isr) return;
    ensure_window();
    if (WindowShouldClose()) { fb_close(); exit(0); }
    if (GetTime() - last_present >= 1.0 / 60.0) present();
    poll_keys();
    type_keys();
    gm_audio_pump();
    fire_timer();
}

/* ---------------------------------------------------------------------------------------------
 * Mouse (virtual 640x200 driver coordinates, see input_asm.cpp)
 * ------------------------------------------------------------------------------------------- */
void gm_mouse_show(int16_t on) { mouse_shown = on; }

void gm_mouse_get(int32_t *vx, int32_t *vy, int32_t *buttons)
{
    if (!window_open) { *vx = *vy = *buttons = 0; return; }
    Vector2 p = GetMousePosition();
    float s = (float)GetRenderWidth() / (float)GetScreenWidth();   /* DPI scale: render px per logical px */
    float nx = (p.x * s - game_rect.x) / game_rect.w, ny = (p.y * s - game_rect.y) / game_rect.h;
    nx = nx < 0 ? 0 : (nx > 1 ? 1 : nx);
    ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
    *vx = (int32_t)(nx * 639.0f + 0.5f);
    *vy = (int32_t)(ny * 199.0f + 0.5f);
    *buttons = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1 : 0) | (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 2 : 0) |
               (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ? 4 : 0);
}

void gm_mouse_set(int32_t vx, int32_t vy)
{
    if (!window_open) return;
    float s = (float)GetRenderWidth() / (float)GetScreenWidth();
    SetMousePosition((int)((game_rect.x + vx / 639.0f * game_rect.w) / s), (int)((game_rect.y + vy / 199.0f * game_rect.h) / s));
}

/* ---------------------------------------------------------------------------------------------
 * Ports
 * ------------------------------------------------------------------------------------------- */
uint8_t gm_inportb(uint16_t port)
{
    switch (port) {
    case 0x3DA: {                       /* VGA input status: bit3 vertical retrace, bit0 display disabled */
        static uint32_t n;
        gm_pump();
        n++;
        double ph = fmod(GetTime() * 70.0, 1.0);
        return (uint8_t)((ph > 0.93 ? 8 : 0) | (n & 1));
    }
    case 0x3C9:
        if (dac_rd_idx < 256) {
            uint8_t v = dac[dac_rd_idx][dac_rd_phase];
            if (++dac_rd_phase == 3) { dac_rd_phase = 0; dac_rd_idx++; }
            return v;
        }
        return 0;
    case 0x21: return pic_mask;
    case 0x60: return kbd_data;
    case 0x61: return 0;
    case 0x201: return 0xFF;            /* no joystick: all axes/buttons idle */
    default:   return 0;
    }
}

uint16_t gm_inport(uint16_t port) { return gm_inportb(port); }

void gm_outportb(uint16_t port, uint8_t v)
{
    switch (port) {
    case 0x3C7: dac_rd_idx = v; dac_rd_phase = 0; break;
    case 0x3C8: dac_wr_idx = v; dac_wr_phase = 0; break;
    case 0x3C9:
        dac[dac_wr_idx & 255][dac_wr_phase] = v & 63;
        if (++dac_wr_phase == 3) { dac_wr_phase = 0; dac_wr_idx++; }
        break;
    case 0x21: pic_mask = v; unmask_kbd(); break;
    case 0x43: pit_lo_hi = 0; break;
    case 0x40:                           /* PIT channel 0 reload value, low byte then high byte */
        if (!pit_lo_hi) { pit_divisor = (pit_divisor & 0xFF00u) | v; pit_lo_hi = 1; }
        else { pit_divisor = (pit_divisor & 0x00FFu) | ((uint32_t)v << 8); pit_lo_hi = 0; next_tick = 0; }
        break;
    default: break;                      /* EOI, speaker, CRTC, sequencer, ... are ignored */
    }
}

void gm_outport(uint16_t port, uint16_t v)
{
    gm_outportb(port, (uint8_t)v);
    gm_outportb((uint16_t)(port + 1), (uint8_t)(v >> 8));
}

/* ---------------------------------------------------------------------------------------------
 * BIOS
 * ------------------------------------------------------------------------------------------- */
uint16_t gm_bioskey(int16_t cmd)
{
    gm_pump();
    if (cmd == 2) return (uint16_t)(shift_held() ? 3 : 0);
    if (cmd == 1) return bios_n ? bios_buf[0] : 0;
    while (!bios_n) { gm_pump(); WaitTime(0.001); }          /* cmd 0: block */
    uint16_t k = bios_buf[0];
    memmove(bios_buf, bios_buf + 1, (--bios_n) * sizeof bios_buf[0]);
    return k;
}

int16_t gm_kbhit(void) { return gm_bioskey(1) != 0; }
int16_t gm_getch(void) { return (int16_t)(gm_bioskey(0) & 0xFF); }

int16_t gm_int86(int16_t intno, union REGS *in, union REGS *out)
{
    ensure_window();
    if (out != in) *out = *in;
    switch (intno) {
    case 0x10:
        if (in->h.ah == 0x00) {                               /* set video mode */
            int mode = in->h.al & 0x7F;
            video_mode = (mode == 0x13) ? 0x13 : 3;
            if (!(in->h.al & 0x80)) {                         /* clear screen unless bit 7 */
                memset(VRAM, 0, 64000);
                for (int i = 0; i < 80 * 25; i++) { TEXT[i * 2] = ' '; TEXT[i * 2 + 1] = 7; }
            }
            default_palette();
            return 0;
        }
        break;
    case 0x16: return 0;                                      /* keyboard typematic rate etc. */
    case 0x33: out->x.ax = 0; return 0;                       /* no mouse driver (replaced natively) */
    default: break;
    }
    static uint8_t warned[256];
    if (!warned[intno & 255]) { warned[intno & 255] = 1; fprintf(stderr, "gm_int86: unhandled INT %02Xh AX=%04X\n", intno & 255, in->x.ax); }
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Time, sound
 * ------------------------------------------------------------------------------------------- */
void gm_delay(uint32_t ms)
{
    ensure_window();
    double end = GetTime() + ms / 1000.0;
    do { gm_pump(); WaitTime(0.001); } while (GetTime() < end);
}

void gm_sound(uint16_t hz) { gm_speaker(hz); }       /* PC speaker */
void gm_nosound(void) { gm_speaker(0); }


/* ---------------------------------------------------------------------------------------------
 * Borland C runtime random numbers (CRTL/CLIB/RAND.C): seed = 0x015A4E35 * seed + 1, 15 bits from bit 16.
 * ------------------------------------------------------------------------------------------- */
static int32_t rand_seed = 1;

void gm_srand(uint16_t seed) { rand_seed = seed; }

int16_t gm_rand(void)
{
    rand_seed = (int32_t)((uint32_t)rand_seed * 0x015A4E35u + 1u);       /* 32-bit wraparound */
    return (int16_t)((rand_seed >> 16) & 0x7FFF);
}

uint32_t gm_farsize(const void *p)
{
    uint32_t off = (uint32_t)((const uint8_t *)p - gm_dosmem);
    for (int i = 0; i < nblocks; i++)
        if (blocks[i].off == off && blocks[i].used) return blocks[i].size;
    return 0;
}
