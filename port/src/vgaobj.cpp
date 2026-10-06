/* Replacement for DRV/VGAOBJ.ASM: the standard VGA `VideoMode` object (VGAScrn), whose function
 * table the FLI/GIF display code uses (Vid->RepCols, Vid->Rep1Col, ...).
 *
 * Mode 13h only: 320x200, 256 colours, linear memory at A000:0000.
 */
#include "gen.h"
#include "pal.h"
#include "palette.h"
#include "Coord2d.hpp"
#include "viddrive.hpp"

extern "C" void BoxFill(int x, int y, int x1, int y1, unsigned char col);

static void SetModeL256(VideoMode *v)
  {
  union REGS r;
  r.x.ax = v->BIOSMode;
  int86(0x10, &r, &r);
  }

static void ClearL256(Pixel col, VideoMode *v)
  {
  memset(v->Address, col, 0xFFFE);               // the asm clears 0x8FFF words
  }

static Pixel *line_ptr(VideoMode *v, int x, int y) { return v->Address + (uint16_t)(y * v->LineSep) + x; }

static void PointL256(Coord2d Pos, Pixel col, VideoMode *v)
  {
  *line_ptr(v, Pos.x, Pos.y) = col;
  }

static Pixel GetColL256(Coord2d Pos, VideoMode *v)
  {
  return *line_ptr(v, Pos.x, Pos.y);
  }

// Clip a horizontal run against the right edge/bottom, as the asm did. Returns the length to use (<= 0: nothing).
static int clip_run(Coord2d Pos, int len, VideoMode *v)
  {
  if (Pos.y >= v->Size.y) return 0;
  if (Pos.x >= v->Size.x) return 0;
  if (v->Size.x - Pos.x < len) len = v->Size.x - Pos.x;
  return len;
  }

static void Rep1ColL256(Coord2d Pos, uint Num, Pixel col, VideoMode *v)
  {
  int len = clip_run(Pos, Num, v);
  if (len > 0) memset(line_ptr(v, Pos.x, Pos.y), col, len);
  }

static void RepColsL256(Coord2d Pos, uint Num, Pixel *cols, VideoMode *v)
  {
  int len = clip_run(Pos, Num, v);
  if (len > 0) memcpy(line_ptr(v, Pos.x, Pos.y), cols, len);
  }

static int GetColsL256(Coord2d Pos, uint Num, Pixel *cols, VideoMode *v)
  {
  int len = clip_run(Pos, Num, v);
  if (len > 0) memcpy(cols, line_ptr(v, Pos.x, Pos.y), len);
  return 0;
  }

// Entries the driver left as `Dummy` (a bare retf)
static void DummyLine(Coord2d, Coord2d, Pixel, VideoMode *) {}
static void DummyBCurve(Coord2d, Coord2d, Coord2d, unsigned char, VideoMode *) {}
static void DummyViewPos(unsigned long, VideoMode *) {}

static void BoxFillL256(Coord2d s, Coord2d e, Pixel col, VideoMode *v) { BoxFill(s.x, s.y, e.x, e.y, col); }
static void DummyBlock(Coord2d, int, VideoMode *) {}

static void SetAllPalL(RGBdata *pal, VideoMode *) { SetAllPal(pal); }
static void GetAllPalL(RGBdata *pal, VideoMode *) { GetAllPal(pal); }
static void SetAllPalToL(RGBdata *pal, VideoMode *) { SetAllPalTo(pal); }
static void SetPalL(int num, RGBdata c, VideoMode *) { Palette((unsigned char)num, c.red, c.green, c.blue); }

VideoMode VGAScrn;

static struct VgaScrnInit
  {
  VgaScrnInit()
    {
    VGAScrn.Size.Set(320, 200);
    VGAScrn.LineSep = 320;
    VGAScrn.ColDepth = 8;
    VGAScrn.Address = (Pixel *)MK_FP(0xA000, 0);
    VGAScrn.ViewPageOff = 0;
    VGAScrn.BIOSMode = 0x13;
    VGAScrn.SetMode = SetModeL256;
    VGAScrn.Clear = ClearL256;
    VGAScrn.Point = PointL256;
    VGAScrn.GetCol = GetColL256;
    VGAScrn.Rep1Col = Rep1ColL256;
    VGAScrn.Rep1ColRev = 0;
    VGAScrn.RepCols = RepColsL256;
    VGAScrn.RepColsRev = 0;
    VGAScrn.GetCols = GetColsL256;
    VGAScrn.GetColsRev = 0;
    VGAScrn.Line = DummyLine;
    VGAScrn.Box = DummyLine;
    VGAScrn.BoxFill = BoxFillL256;
    VGAScrn.DrawBlock = DummyBlock;
    VGAScrn.BCurve = DummyBCurve;
    VGAScrn.Spline = DummyBCurve;
    VGAScrn.SetViewPos = DummyViewPos;
    VGAScrn.SetWritePage = DummyViewPos;
    VGAScrn.SetAllPal = SetAllPalL;
    VGAScrn.GetAllPal = GetAllPalL;
    VGAScrn.SetAllPalTo = SetAllPalToL;
    VGAScrn.SetPal = SetPalL;
    }
  } vga_scrn_init;
