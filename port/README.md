# GameMaker on raylib (port)

A port of the 1994 DOS game player (`code/GM/PLAYGAME.C`) to modern systems, built with CMake and
drawn through [raylib](https://www.raylib.com/). The original engine source is compiled **in place**
(no copy of it lives here); everything DOS-specific is replaced underneath it.

**Status.** `gmplay` plays GameMaker games (menus, gameplay with keyboard or gamepad, FM music and sound effects) on
**Windows** and **Linux** (tried on Debian under WSL) and is written to build on **macOS**, which nobody has been able to
try yet. On Windows and Linux it replays the shipped demo recordings and generated ones **identically to the DOS original**: every tick and
every video frame match (see [baseline/README.md](baseline/README.md)). Not done: the editors (`gm`, `blocedit`,
`mapmaker`, ...), which are separate DOS programs that are not part of this port, and a web build.

## Build and run

Windows (Visual Studio provides cmake, ninja and cl):

```
port\build.bat                          # fetches raylib on first run
port\build\gmplay.exe houses            # play a game by name; see "Playing" below
```

Linux and macOS (cmake, a C and C++ compiler, git; on Debian/Ubuntu also
`sudo apt install libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`):

```
sh port/build.sh
port/build/gmplay houses
```

`gmplay` with no game shows the original menu. `gmplay demo.rec game.gam` and the other `playgame` arguments still
work. [.github/workflows/port.yml](../.github/workflows/port.yml) builds all three platforms and runs the self-tests (and, on
Linux, the replay check); it has not been run yet, and is the way to find out whether macOS builds.

Whole-project checks: `port\smoke.ps1` starts every playable game with scripted input and reports crashes, hangs and
frozen screens; `port\datacheck.ps1` looks for damaged game data; `port\baseline\compare.ps1` (Windows, needs DOSBox)
and `port/baseline/compare.sh` (needs a DOS run to compare with) check the port against DOS, and
`port/baseline/check_states.sh` does a cheaper check against the DOS tables kept in the repository, on any platform;
`platformtest` and `audiotest` (built with the port) check the platform layer and the sound. Licenses of what the port
builds on: [THIRD_PARTY.md](THIRD_PARTY.md).

## Playing

```
gmplay [options] [game]
```

`game` is a `.gam` file, a game folder, or a name: names are looked up in `$GM_GAMES` (a list of folders that hold game
folders), `cd/gameware` next to the checkout, `games/` next to the executable, and the current folder.
`gmplay --list` shows what it finds. The game starts directly; its scores, saved games and recordings go in the game's
own folder, as they did in DOS. Nothing needs configuring: the port has a built-in sound card and display, and without a
`config.dat` it uses those (the game's own *Configure* screen still works).

| Option | |
|---|---|
| `--fullscreen` / `--windowed` | borderless full screen or a window (remembered) |
| `--scale=int43\|fit43\|intsq\|fitsq` | whole-number or fitted picture, with 4:3 monitor proportions (as in 1994: VGA pixels are taller than wide) or square pixels (remembered) |
| `--volume=N`, `--mute`, `--unmute` | master volume 0-100 (remembered) |
| `--no-gamepad` | ignore gamepads |

| Key | |
|---|---|
| F11 or Alt+Enter | full screen / window |
| F12 | cycle the four picture modes |
| Alt+Up, Alt+Down | volume |
| Alt+M | mute |

These keys are the port's own; while Alt is held the game does not see Up, Down, Enter or M. The settings are kept in
`%APPDATA%\gmplay\gmplay.ini`, `~/Library/Application Support/gmplay/gmplay.ini` or `~/.config/gmplay/gmplay.ini`.

**Gamepad** (the first one connected): in a game the D-pad or left stick is the joystick and A/B are its two buttons,
so every game's own joystick set-up works with no calibration; in the menus the same controls are the arrow keys, A is
Enter and B is Esc; Start is Esc everywhere. Esc belongs to the game, so closing the window is how to leave the program
from a menu. Nothing here was tried with a physical gamepad.

## How it works

| Piece | Where | What it does |
|---|---|---|
| Borland shim | `shim/gmcompat.h` | Force-included before every engine file: 16-bit `int` and enums, byte-packed structs, `far`/`near`/`interrupt` keywords removed, `MK_FP`/`FP_SEG` into an emulated 1 MB address space, `inportb`/`outportb`/`setvect`/`bioskey` routed to the platform layer, Borland's own `rand()`/`random()` |
| Platform layer | `src/dosplat.c`, `dosfind.c`, `dospath.c`, `settings.c` | Emulated memory (VGA at `A000:0`, text at `B800:0`), far heap, VGA DAC palette, PIT timer and keyboard "interrupts" delivered cooperatively from `gm_pump()`, BIOS keyboard/video/mouse, `findfirst`; DOS-style file names on case-sensitive systems (`dospath.c`); the settings file |
| Video | `src/fb.c`, `font8x8.h` | Indexed 320x200 and 80x25 text screens converted to RGBA and shown in a raylib window in one of four scaling modes, with full-screen and a status toast. The 8x8 font is [Unscii](https://github.com/viznut/unscii) (public domain), generated into `font8x8.h` by `tools/mkfont.ps1` |
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
- `PLAYGAME.C` and `JSTICKC.C`, port only (`GM_PORT`): `gmplay game.gam` plays that game and leaves, without the file
  picker; with no `config.dat` the built-in sound card and display are used instead of asking; the gamepad joystick is
  switched on, and ignored while a recording plays; `blks[0]` has 256 entries because a block number is a byte
- `gm_long`/`gm_ulong` instead of `long` in file structs and 32-bit arithmetic (`gen.h`, `PLAYGAME.C`, `HISCOREL.C`,
  `SBMUSIC.C`, `FLI.H`, `FLI.CPP`); `const` on `Coord2d` operator parameters (`COORD2D.HPP`, `FASTCORD.HPP`); `(int)(a*b)`
  in `ChangeScroll`; see "Linux and macOS" and "Where DOS and the port differ" below

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

**Level check without listening.** `wavstat file.wav -s` prints peak and RMS in dBFS and the number of clipped samples.
Rendering every distinct music file offline (`cmf2wav`, 45 s each) and every distinct sound effect (`voc2wav`) in
`cd/`: 53 CMF files peak between -19.5 and -0.2 dBFS (RMS -41 to -16, median -27), 308 VOC files peak at most -3.1 dBFS,
and **nothing clips**. So the mix has headroom, if anything the music is quiet next to the effects; whether it *sounds*
right (balance, drum voices) is still a matter for ears.

**Known gap.** The VOC decoder handles 8-bit PCM and Creative's 4-bit ADPCM. One file in the repository,
`cd/gameware/rings5/tune.voc`, is 2-bit ADPCM (VOC codec 3) and plays as silence; of ~525 sound effects it is the only one
that uses a codec other than those two.

## Test hooks

Environment variables for driving `gmplay` without a person (details in baseline/README.md): `GM_TYPE` (scripted
keystrokes), `GM_SHOT` (save the exact emulated screen, also as a numbered series), `FDUMP`/`FTRACE` (replay records),
`GM_WATCHDOG`, and `GM_EXITTRACE=1` (print the call stack of whoever calls `exit()`; a silent non-zero exit code
is otherwise hard to explain). For sound: `GM_WAV=<file.wav>` also writes everything sent to the audio device (it works with no device
present), and `GM_SOUNDLOG=1` prints the FM driver's note events and each VOC file played.

For tests on machines without a display or sound card: `GM_HEADLESS=1` runs with no window, no keyboard/mouse/gamepad
and no audio device (replays, `GM_TYPE`, `GM_SHOT`, `FDUMP` and `GM_WAV` all still work). `GM_NO_SETTINGS=1` neither
reads nor writes the settings file, and `GM_CONFIG_DIR=<dir>` moves it; every test script sets the first so that a
person's saved window or volume cannot change a test. `GM_HOST_KEYS="f11@3,f12@4,volup@5,voldn@6,mute@7"` runs the
port's own key actions at those seconds, since the real Alt chords cannot be scripted.

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
- **16-bit arithmetic overflow.** `#define int short` makes variables 16-bit, but C still does the arithmetic in 32 bits, so
  `a*b` no longer wraps. `ChangeScroll` multiplies a screen distance by 40; when the character is far off screen (it is
  for a while after some scene changes) the product exceeded 32,767 and DOS scrolled at a wrapped, different speed. Found
  by a generated recording of rings5 (tick 849); fixed with `(int)(a*b)` casts at the four places. Expect more of these:
  any product or sum of `int`s that can exceed 16 bits needs the same cast.
- **Array indices that DOS got away with.** `blkmap`/`nextbl` are bytes (up to 255) but `blks[0]` has 150 entries, and
  peach and zark have junk `nextbl` values (255, 249) in unused blocks. DOS read whatever followed the array; a 64-bit
  heap faults. The port allocates 256 entries (`GM_PORT` only, because 256 blocks do not fit one far allocation).
- **Wall-clock time leaks into game logic.** Monsters compare absolute clock values, and DOS loading time advanced the
  clock between scenes. Replays are only repeatable with the deterministic clock described in baseline/README.md.

### Linux and macOS

What it took to build the 1994 sources with GCC (Clang is handled by probing flags, untested):

- **`long` is 64 bits** on Linux and macOS (32 on Windows and in Borland), and `long` cannot be redefined because it is part
  of `unsigned long`. The sources use `gm_long`/`gm_ulong` (32-bit; defined in the shim for the port and in `gen.h` for the
  DOS build) wherever a `long` is stored in a file (high scores, saved games, the FLI headers) or must wrap at 32 bits.
- **File names.** The engine builds DOS names (`SAMPLE\SAMPLE.GAM`, any case). The shim sends `fopen` and `remove` through
  `gm_fopen`/`gm_remove` (`src/dospath.c`): `\` becomes `/`, a drive letter is dropped, and each component is matched
  case-insensitively against the directory. `findfirst` is rewritten with `opendir` and DOS wildcards, upper-cases the
  names like Windows' short names, and hides names that are not 8.3 (the engine keeps 14-byte slots for them).
  `ParseFileName` accepts `/` and the file picker turns a typed `/` into `\`.
- **Headers.** The sources include `"gen.h"` for `GEN.H`, `"Palette.h"` for `PALETTE.H` and so on, and the directories are
  `Image/FLI` in git but `fli` in the includes. `cmake/IncludeShadow.cmake` makes a directory holding a link for every
  spelling used. Because the shadow comes before `port/src`, `"gif.h"` is the engine's `GIF.H`, not the port's own `gif.h`.
- **Ctrl-Z.** DOS text files end with a 0x1A byte, which MSVC ignores and GCC rejects, so on these platforms CMake compiles
  from copies of the three source directories with those bytes removed (and reconfigures when an original changes).
- **Language differences.** `xor` is a function name in the sources (`-fno-operator-names`); `Coord2d`'s operators took
  non-const references and relied on MSVC binding temporaries to them (now `const &`, valid in Borland too); glibc declares
  `uint`, `ushort` and `ulong`, which collide with the engine's typedefs, so the shim renames them while it reads the system
  headers; `set_new_handler` was global in Borland.
- **Other.** Crash, hang and exit diagnostics use `backtrace()` and signals instead of `dbghelp`; `-rdynamic` makes the names
  readable. WSL: window creation can hang inside GLFW when a second window is opened under WSLg, which is why the tests
  run with `GM_HEADLESS=1`.

Result on Debian (GCC 14, WSL): all 16 recordings compared with `compare.sh` are identical to DOS (ticks and pixels),
`check_states.sh` matches the DOS tables for the 12 that have one in the repository, and `platformtest` and `audiotest`
pass.

### Damaged game data

- `datacheck.ps1` validates the data files (GIF and VOC structure, FLI and CMF lengths, and the sizes of the fixed-size
  map/block/character/monster/palette files). Of 3,248 files, **18 are cut off at exactly 16,384 bytes, all of them in
  BCUDA**: 12 `.map`, 3 `.bbl`, `bwon.gif`, and `cfadein.fli`/`cfadeout.fli` (whose headers say 104,206 and 132,330 bytes).
  BCUDA therefore plays with missing map rows and blocks and shortened fades, in DOS as well as here (its demo replays
  identically in both). Everything else it flags is harmless: two leftover `pipes*.gif` files that no game references, `.cbl`
  files with 2 extra bytes, and a stray byte after one VOC.
- The engine reads these files without validating them. When a game misbehaves on one screen, run `datacheck.ps1` and
  compare that screen's file sizes against their headers first.
- Only the 17 games in `cd/gameware` use the 3.0 file format that `gmplay` loads. The 14 games in `cd/sharware` are in an
  older format and each ships with its own old engine executable; the 3.0 player (DOS and port alike) rejects them with
  "Incorrect Data File Version". They still run in DOSBox with their own executables.

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
   found. Remove the extra output afterwards; only the per-tick trace is kept. One extra line is kept behind an
   environment variable: `FSCROLL=1` (set it for both `run_baseline.ps1` and `compare.ps1`) adds an indented line with the
   scroll state after each tick, which is what located the 16-bit overflow in `ChangeScroll`.
4. Before blaming the port, run the DOS side twice (ideally once under load): if DOS differs from itself, the cause is
   non-determinism in the original, not the port.

## Next

More differential tests: `baseline/gen_recordings.ps1` plays a game with seeded random keys and saves the engine's own
recording, which DOS and the port then replay (`run_baseline.ps1`/`compare.ps1` with `-Recs baseline\recs -Tag -gen`).
It found the scroll overflow above on its first useful game. A game's recording is only saved if the session ends cleanly
through the high-score screens, which depends on wall-clock timing, so the script is run again (new `-Seed`) for games
that did not produce one.

Still open:

- macOS has never been built; the GitHub workflow is there to find out, and probably needs a few fixes.
- The gamepad code has never run with a gamepad connected (there was none), and the full-screen, scaling, volume and mute
  actions were run through `GM_HOST_KEYS`, not with real key presses. Nobody has listened to the sound either (levels and
  drum voices are unchanged from the earlier analysis).
- A web build (emscripten) needs the engine's blocking loops turned inside out; not attempted.
- The editors (`gm`, `blocedit`, `charedit`, `mapmaker`, ...); they still run in DOSBox.
- 14 shareware games in `cd/sharware` use an older format that this player rejects, as the DOS one does.
