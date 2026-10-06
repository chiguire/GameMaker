# Verifying the port against DOS

The port is checked against the original engine, not against opinions about how it should behave.
`code/GM/PLAYGAME.C` can be built with `-DFRAMEDUMP`; that build replays a game's shipped `demo.rec`
with no menus and records what happened. The same hook is compiled into the port (`gmplay`), so both
sides write identical records and `compare.ps1` diffs them.

## Result

All eight sample games that ship a demo recording replay **identically** in DOS and in the port
(8 recordings, 17,000+ game ticks; run `compare.ps1` to reproduce):

| game | ticks (DOS vs port) | frames with differing game state | frames with differing pixels |
|---|---|---|---|
| bcuda | identical (1625) | 0 of 81 | 0 of 81 |
| houses | identical (1049) | 0 of 52 | 0 of 52 |
| nebula | identical (3021) | 0 of 150 | 0 of 150 |
| peach | identical (3021) | 0 of 150 | 0 of 150 |
| pipemare | identical (3021) | 0 of 150 | 0 of 150 |
| tutor | identical (3021) | 0 of 150 | 0 of 150 |
| volume | identical (1858) | 0 of 92 | 0 of 92 |
| zark | identical (1546) | 0 of 77 | 0 of 77 |

"Ticks identical" means a per-tick trace (sequence, animation frame, position, scene, hit points,
lives, score, a checksum of every monster, sub-block position, gravity) matches line for line.
"Pixels" are the 64,000 bytes of video memory every 20 ticks, compared exactly.

nebula, peach, pipemare and tutor stop at the 150-frame cap (3,000 ticks), not at the end of their recording.

## How a run works

`playgame <game>\demo.rec <game>\<game>.gam` (instrumented build only) plays the recording and appends
to the files named by environment variables:

| variable | content |
|---|---|
| `FDUMP` | one record per 20 ticks: `"FRM1"`, tick, timer, score, scene, character x/y, scroll offset, 768-byte palette, 64,000 video bytes (read with `tools/fdump.c`) |
| `FTRACE` | one text line per tick (see above) |

## Making replays repeatable

DOS is not deterministic on its own (wall-clock time, uninitialised memory), so the instrumented builds
(`FRAMEDUMP`, both DOS and port) pin down everything that would otherwise leak into a replay:

- **Clock.** While a scene's main loop runs, the timer interrupt stops advancing time and the loop advances it
  by exactly one game tick per pass. Each scene starts from the clock value the previous one ended with, not from
  however long loading took.
- **Random numbers.** The port uses Borland's own `rand()` (a 32-bit LCG, from `tools/BORLANDC/CRTL/CLIB/RAND.C`)
  and `random()` macro. Both builds seed with 1 at start and re-seed with `1 + scene` at the start of each scene, so title
  and fade code that consumes random numbers a timing-dependent number of times cannot shift the sequence.
- **Video.** The oracle is forced onto the software-scroll video path, the only one the port implements.
- **Two original bugs made well defined.** Both are undefined behaviour in the 1994 code whose effect depended on
  leftover memory:
  1. A monster on the last map row makes the engine read `map[100][x]`, one row past the array. In DOS that is the
     start of the data segment (the Borland banner text and some pointers), whose bytes look like monster indices, so
     monsters collided with phantom monsters; which bytes it saw changed whenever the executable's layout changed.
     The instrumented builds and the port give `map` a real extra row that means "no monster".
  2. The "no block here" sentinel entry at the end of each block array (`blks[0][150]` ...) is never initialised, so
     its `solid` flags were leftover heap bytes (one phantom wall in `tutor`). It is zeroed.

  Neither change applies to a normal DOS build.

## Files

| Path | Contents |
|---|---|
| `<game>/states.txt` | the DOS frame table: one line per recorded frame (tick, score, scene, character x/y, scroll offset) |
| `<game>/frameNNN.png` | first, middle and last recorded DOS frame |
| `build_oracle.ps1` | builds the instrumented DOS `PLAYGAME.EXE` from the repo source, in DOSBox with the bundled Borland toolchain (in a scratch copy; tracked `.OBJ`/`.EXE` files are not touched) |
| `run_baseline.ps1` | replays demos in DOSBox and writes the files above; full dumps and traces stay in `%TEMP%\gm_oracle` |
| `compare.ps1` | replays the same demos in `gmplay` and compares ticks, game state and pixels against the DOS run |
| `../tools/fdump.c` | `fdump list|png|diff` for the frame dumps |

Reproduce: `port\build.bat`, `baseline\build_oracle.ps1`, `baseline\run_baseline.ps1 -Games bcuda,nebula,...`,
`baseline\compare.ps1 -Games bcuda,nebula,...` (four games at a time is fine; running all eight windows at once
made one of them fail to start).

## Interactive checks

`gmplay` has test hooks for driving it without a person: `GM_TYPE` scripts keystrokes (`\n` Enter, `\b` Backspace,
`\l \r \u \d` arrows, `\hNN<key>` holds a key NN x 80 ms, `\wNN` waits), and `GM_SHOT=<file.png>` writes the exact
emulated screen and exits (`%d` in the name plus `GM_SHOT_EVERY` takes a numbered series). Together they were used
to walk the real menus (game picker, title, main menu), start a game and steer the character with held arrow keys.
