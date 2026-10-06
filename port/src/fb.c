#include "fb.h"
#include <raylib.h>

uint8_t fb_pix[FB_W * FB_H];
FbColor fb_pal[256];

static Texture2D tex;
static int tex_w, tex_h;
static uint32_t rgba[FB_W * FB_H];

static void make_texture(int w, int h)
{
    static uint32_t blank[640 * 400];   // zero-filled; the real pixels arrive with UpdateTexture
    if (tex_w) UnloadTexture(tex);
    Image img = { blank, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    tex_w = w;
    tex_h = h;
}

void fb_open(const char *title, int scale)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(FB_W * scale, FB_H * scale, title);
    SetWindowMinSize(FB_W, FB_H);
    SetTargetFPS(60);
    make_texture(FB_W, FB_H);
}

void fb_close(void)
{
    UnloadTexture(tex);
    CloseWindow();
}

void fb_set_vga_pal(int idx, int r6, int g6, int b6)
{
    /* expand 6-bit DAC to 8 bits: v<<2 | v>>4 */
    fb_pal[idx].r = (uint8_t)((r6 << 2) | (r6 >> 4));
    fb_pal[idx].g = (uint8_t)((g6 << 2) | (g6 >> 4));
    fb_pal[idx].b = (uint8_t)((b6 << 2) | (b6 >> 4));
}

static void convert(void)
{
    for (int i = 0; i < FB_W * FB_H; i++) {
        FbColor c = fb_pal[fb_pix[i]];
        rgba[i] = c.r | (c.g << 8) | (c.b << 16) | 0xFF000000u;
    }
}

int fb_save_png(const char *path)
{
    convert();
    Image img = { rgba, FB_W, FB_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    return ExportImage(img, path) ? 0 : -1;
}

void fb_draw_rgba(const uint32_t *px, int w, int h, FbRect *out_rect)
{
    if (w != tex_w || h != tex_h) make_texture(w, h);
    UpdateTexture(tex, px);
    BeginDrawing();
    ClearBackground(BLACK);
    /* letterbox: largest rectangle with the image's aspect ratio, centred in the window */
    float sw = (float)GetRenderWidth(), sh = (float)GetRenderHeight();
    float s = sw / w < sh / h ? sw / w : sh / h;
    float dw = w * s, dh = h * s, dx = (sw - dw) / 2, dy = (sh - dh) / 2;
    DrawTexturePro(tex, (Rectangle){0, 0, (float)w, (float)h}, (Rectangle){dx, dy, dw, dh},
                   (Vector2){0, 0}, 0, WHITE);
    EndDrawing();
    if (out_rect) { out_rect->x = dx; out_rect->y = dy; out_rect->w = dw; out_rect->h = dh; }
}

void fb_present(void)
{
    convert();
    fb_draw_rgba(rgba, FB_W, FB_H, 0);
}
