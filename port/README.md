# GameMaker on raylib (port)

A port of the 1994 DOS game player (`code/GM/PLAYGAME.C`) to modern systems, built with CMake and
drawn through [raylib](https://www.raylib.com/). The original engine source is compiled **in place**
(no copy of it lives here); everything DOS-specific is replaced underneath it.

**Status.** `gmplay` plays GameMaker games interactively (game picker, title, menus, gameplay with the keyboard)
and replays the shipped demo recordings **identically to the DOS original**: 8 recordings, every tick and every
video frame match (see [baseline/README.md](baseline/README.md)). Sound works too: FM music (CMF files on an emulated
OPL2), digital effects (VOC) and the PC speaker. Not done yet: joystick, and the
editors (`gm`, `blocedit`, `mapmaker`, ...), which are separate DOS programs that are not part of this port.

## Build and run

```
port\build.bat                          # fetches raylib on first run; needs Visual Studio (cmake, ninja, cl)
port\run_port.ps1 -Game bcuda -Replay   # replays cd\gameware\bcuda\demo.rec in a scratch copy
cd runtime\run\GM && ..\..\..\port\build\gmplay.exe     # interactive; needs GM.CFG in the working directory
```

`gmplay` takes the same arguments as `playgame`. Windows is the only platform tried so far; the platform layer is
plain C over raylib except `dosfind.c` (directory listing, Win32) and the crash/hang diagnostics in `gmplay_main.cpp`.

## How it works

| Piece | Where | What it does |
|---|---|---|
| Borland shim | `shim/gmcompat.h` | Force-included before every engine file: 16-bit `int` and enums, byte-packed structs, `far`/`near`/`interrupt` keywords removed, `MK_FP`/`FP_SEG` into an emulated 1 MB address space, `inportb`/`outportb`/`setvect`/`bioskey` routed to the platform layer, Borland's own `rand()`/`random()` |
| Platform layer | `src/dosplat.c`, `dosfind.c` | Emulated memory (VGA at `A000:0`, text at `B800:0`), far heap, VGA DAC palette, PIT timer and keyboard "interrupts" delivered cooperatively from `gm_pump()`, BIOS keyboard/video/mouse, `findfirst` |
| Video | `src/fb.c`, `font8x8.h` | Indexed 320x200 and 80x25 text screens converted to RGBA and letterboxed in a raylib window. The 8x8 font was extracted from a Windows `.FON` with `tools/fon2h.c` |
| Assembly rewrites | `src/gfx_asm.cpp`, `text_asm.cpp`, `input_asm.cpp`, `vgaobj.cpp` | C++ versions of `SVGAA`, `BLOCA`, `MEMBLOCA`, `PALA`, `SCRNROUT`, `MICROCNL`, `JSTICKA`, `OLDMOUSE` and the VGA driver object |
| Hardware stand-ins | `src/svga_port.cpp` | A single "software scrolled" VGA card |
| Sound | `src/audio.cpp`, `sound_port.cpp` | Mixer (OPL2 synthesizer, PC speaker, one VOC voice) feeding a raylib audio stream, and the engine-side FM driver; see "Sound" below |
| Entry and diagnostics | `src/gmplay_main.cpp` | `main()`, a crash reporter, and (with `GM_WATCHDOG=1`) a hang watchdog that print symbolised stacks |

The engine is cooperative on one thread: the original's timer and keyboard interrupt handlers run from
`gm_pump()`, which is called from the engine's waiting loops (`bioskey`, `delay`, the VGA status port) and once per
pass of the main game loop.

### Changes to the original sources

Small, behaviour-preserving for the DOS build, and mostly guarded by `GM_PORT`/`FRAMEDUMP`:

- `long int` -> `long` and bare `unsigned` -> `unsigned int` in structs (the shim makes `int` 16 bits); enums that
  live in on-disk structs use `GM_ENUM16` (empty for DOS, `: short` in the port)
- Borland-only constructs MSVC rejects: `operator &>`, literal far pointers such as `0xA0000000`, a reference bound to a
  literal, `<fileclss.hpp>` so the `DRV` header wins as it did under Borland's include rules
- `PLAYGAME.C`: one `gm_pump()` call in the main loop, the entry wrapper, the `FRAMEDUMP` oracle hook, and bug fixes:
  `argc>0` -> `argc>1` before reading `argv[1]`, and well-defined values for two reads of uninitialised memory (documented
  in baseline/README.md)
- `WINDIO.C`: the file list's border string written with `\x` escapes (the CP437 bytes are easy to lose in editors)
- `DRV/FILECLSS.CPP` and `Image/fli/FLI.CPP` (animations such as the fades around the Instructions/Storyline/Credits
  screens): `FileRead::Peek` no longer calls `fread` on the NULL `FILE*` left after a small file was read whole (Borland's
  `fread` quietly returned 0, the modern runtime aborts), and the FLI player now stops at the end of a truncated file
  instead of reusing the last frame header, and skips unknown chunk types instead of calling `exit(1)`

## Sound

Three sources are mixed into one 16-bit mono stream at 49,716 Hz (the YM3812's native rate) and played through raylib.

- **FM music (CMF).** The original `SBMUSIC.C` is compiled unchanged: it parses the file and its `PlayIt()` runs once per
  timer tick (146 Hz, the rate the songs are timed for). The Creative FM driver it called (`sbfd_*`, in a binary library
  with no source) is rewritten in `sound_port.cpp`: it maps MIDI-style note events onto the OPL2's nine voices (six plus five
  percussion voices in rhythm mode, which 68 of the 213 shipped songs use), loads the 16-byte instrument records into the
  chip's operator registers, scales loudness by velocity, and steals the oldest voice when all are busy. The chip itself is
  [ymfm](https://github.com/aaronsgiles/ymfm) (Aaron Giles, BSD-3-Clause), fetched at the commit pinned in `CMakeLists.txt`.
- **Digital effects (VOC).** `audio.cpp` decodes Creative Voice Files (8-bit PCM, 4-bit ADPCM, silence, repeat and the extended
  rate blocks) and resamples them; one sample plays at a time, as with the original driver. 2-bit and 2.6-bit ADPCM (one file
  in the shipped games) are kept as silence.
- **PC speaker.** The engine's per-tick `sound(hz)` calls become a square wave.

Register writes are applied at the moment they happen (the mixer first renders up to "now" on the wall clock), so music keeps
its tick timing rather than being quantised to audio buffers. Everything runs on the engine's thread, with
`gm_audio_pump()` feeding the device from `gm_pump()`; a long operation that does not pump (a slow scene load) can cause a
short silent gap. Sound is not part of the DOS replay comparison: replays freeze the timer interrupt that drives music.

How it was checked, without listening: `tools/audiotest.cpp` (speaker pitch, a raw OPL2 note, a synthetic VOC; exits non-zero on
failure), `tools/cmf2wav.cpp` (renders a CMF through the real music code on a manual clock; a hand-made three-note song came out
at 261, 440 and 880 Hz with exact one-second note lengths; 52 shipped songs, one per distinct file size, render without errors),
`tools/voc2wav.cpp` (302 shipped VOC files, one per distinct size, decode to the length their headers imply), and `tools/wavstat.c` (level and dominant
frequency timeline of a WAV, for example one captured from a real session with `GM_WAV`).
The mixer levels and the choice of operator for single-operator drums are by ear and by the OPL documentation, not compared with a
real card.

## Test hooks

Environment variables for driving `gmplay` without a person (details in baseline/README.md): `GM_TYPE` (scripted
keystrokes), `GM_SHOT` (save the exact emulated screen, also as a numbered series), `FDUMP`/`FTRACE` (replay records),
`GM_WATCHDOG`, and `GM_EXITTRACE=1` (print the call stack of whoever calls `exit()`; a silent non-zero exit code
is otherwise hard to explain). For sound: `GM_WAV=<file.wav>` also writes everything sent to the audio device (it works with no device
present), and `GM_SOUNDLOG=1` prints the FM driver's note events and each VOC file played.

## Porting notes and gotchas

Things learned the hard way, so they do not have to be rediscovered.

### Working in this repo

- **Do not edit CP437 files with editors that assume UTF-8.** The sources contain box-drawing characters as raw
  bytes (>= 0x80). The Edit/Write tools of the assistant used here re-encode them as U+FFFD, which is how the file
  picker once got a row of junk (`WINDIO.C`). Use `\x` escapes, or `sed` with `LC_ALL=C`. After editing, compare
  `git show HEAD:file | tr -d '\000-\177' | wc -c` with the working copy. The original `PLAYGAME.EXE` still contains
  the intact strings if a literal ever needs recovering.
- **Line endings are CRLF.** MSYS `sed -i` strips the `\r` on read, so a plain `sed -i` makes `git diff` show whole-file
  changes; re-add with `sed -i 's/\r$//; s/$/\r/' file`. Patterns containing `\r$` never match.
- **Visual Studio's cmake/ninja/cl are not on PATH**; use `build.bat`. Do not pipe it through `Select-Object -First N`,
  which stops the build early. There is no Python on the development machine.
- **`<windows.h>` and raylib cannot share a translation unit** (name clashes), hence `src/dosfind.c`.
- **PowerShell:** `"$g: text"` parses `$g:` as a scope (write `"${g}: text"`); in `@(0, $x/2, $n - 1)` the comma binds
  tighter than `-`.

### Where DOS and the port differ

- **Include resolution.** Borland searched the working directory and `-I` paths for `"quoted"` includes, not the
  including file's own directory. `code/DRV` and `code/Image/fli` hold different `fileclss.hpp` versions, so under MSVC
  the wrong one was picked until `FLI.*` switched to `<fileclss.hpp>`.
- **Sizes.** Anything that is read from a game file as a raw struct needs Borland's layout: 16-bit `int`, 16-bit enums,
  byte packing, and bare `unsigned` meaning 16 bits. Symptom of getting it wrong: sizes such as `sizeof(frames)` coming out
  as 25 instead of 17, and animations that never advance. The shim handles `int`; the rest are source edits.
- **Undefined behaviour that happened to work in DOS.** `map[100][x]` read the start of the data segment (the linker map,
  `code/GM/PLAYGAME.MAP`, shows `_map` at `1DE2:0000`, 20,000 bytes, directly followed by `DGROUP` at `22C4:0000`); the
  never-initialised sentinel block read leftover heap; `strncmpi(argv[1], ...)` read `argv[1]` when it was NULL.
  Anything like this changes behaviour with memory layout, so do not try to copy the garbage (a first attempt that copied
  the bytes broke as soon as the DOS executable's layout changed). Define the behaviour instead.
- **Wall-clock time leaks into game logic.** Monsters compare absolute clock values, and DOS loading time advanced the
  clock between scenes. Replays are only repeatable with the deterministic clock described in baseline/README.md.

### Damaged game data

- Some game files in this repository are cut short. `cd/gameware/bcuda/cfadein.fli` and `cfadeout.fli` are 16,384 bytes but
  their headers say 104,206, so most of each fade is missing. Only 18 files in `cd/` and `runtime/` are exactly 16,384
  bytes (the others are maps, block files and `bwon.gif`; I did not check whether those are complete). The engine reads
  such files without validating them; in DOS the same code would have used whatever stale bytes were in its buffers, which I
  did not try. When a game misbehaves on one screen, check that screen's file sizes against their headers first.

### Diagnosing crashes and hangs

- `gmplay_main.cpp` prints a symbolised stack for crashes (needs the `.pdb` next to the exe) and, with `GM_WATCHDOG=1`,
  for a main thread that stops reaching the platform layer for 5 seconds. A hang there means a loop in the engine that
  spins without calling `gm_pump()`; add the call, as was done in the main game loop.
- Exit code `0xC0000409` is the C runtime's invalid-parameter path (for example `fopen(NULL)`), not necessarily a stack
  overrun; the handler in `gmplay_main.cpp` prints where it came from.
- Several windows started at once can fail to initialise OpenGL; run replays about four at a time.

### Finding where the port and DOS diverge

1. Run `baseline/compare.ps1`; it reports the first differing tick.
2. Diff the two `FTRACE` files around that tick (the `timer=` field is wall-clock and is ignored). The state fields
   narrow it down (sequence/frame, position, hit points, the monster checksum, sub-block position, gravity).
3. If more detail is needed, temporarily add `fprintf` lines to the `FRAMEDUMP` code in `PLAYGAME.C`: a full monster
   dump at one tick, one monster slot every tick, or the intermediate values inside `movechars()`; run both builds with
   the same environment variables and diff. This is how the `map` row, the sentinel block and the clock problems were
   found. Remove the extra output afterwards; only the per-tick trace is kept.
4. Before blaming the port, run the DOS side twice (ideally once under load): if DOS differs from itself, the cause is
   non-determinism in the original, not the port.

## Next

Joystick/gamepad, volume and mute controls, a cleaner window/fullscreen experience, other platforms, and then the
editors.
