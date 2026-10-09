# Plan: GameMaker editors on the web

## Starting point

- `port/` already compiles the DOS C code in `code/GM` to WebAssembly behind `port/shim/gmcompat.h`, with an Emscripten/Asyncify build and a browser page (`port/web/`).
- The page shows a canvas and handles zip drop and IDBFS persistence. It currently covers only the player (`gmplay`).
- The editors are the same codebase: `palchos`, `blocedit`, `mapmaker`, `monedit`, `charedit`, `image`, `grator`, `sndedit`, `utility`, `menu`, `gm` (about 15k lines). They share `WINDIO`, `GENC`, `GRAPHC`, `NEWMOUSE.ASM`, `SCRNROUT.ASM`, `GENCLASS` etc. with the player.

**Approach:** compile the original editors to wasm with the same shim layer, not a JS rewrite. A rewrite would redo ~15k lines of UI and drift from the original file formats. The player port already gives the platform layer (framebuffer, palette, keyboard, timer, sound, DOS file names); the new work is mostly the mouse, a few DOS calls, and program switching.

## Phase 0: Scope survey

1. List what each editor needs beyond the player: `int86`/`int 33h`, `findfirst`, `spawn`/`exec`/`system`, text-mode screens, SVGA modes, EMS/XMS or far-memory tricks, `.ASM` modules not yet shimmed, `getenv`/path assumptions.
2. Check `GM.EXE` and `MENU.EXE`: they launch the other editors via `spawn`/`exec` and chain files (`CHAIN.C`).
3. Decide what is out of scope: `INSTALL`, `FLOPINST`, `REGIST`/`REGISTER`, `MKDSK`, SB recording, the SB driver.
4. Output: `port/EDITORS_SURVEY.md` with an editor x dependency table and a risk list.

## Phase 1: Platform layer for editors

- **Mouse:** port the `NEWMOUSE.ASM` / int 33h interface to browser pointer events (position, buttons, hotspot, cursor shape, clipping). Draw the cursor sprite into the framebuffer. Touch fallback (tap = left, long-press or toolbar button = right).
- **Keyboard:** editor key set (F-keys, Alt combos, extended scancodes); suppress browser shortcuts while the canvas has focus.
- **Text/graphics screens:** check `WINDIO`, `SCRNROUT` and the 8x8 font path in the modes the editors use.
- **Files:** `findfirst`/`findnext` over MEMFS; a project tree in IDBFS laid out like `runtime/run/gm` (BBL, MBL, CBL, MON, CHR, MAP, PAL, SND, GAM); shipped libraries and help files read-only.
- **Program switching:** editors define the same global names, so link each as a separate wasm module and instantiate one at a time: `syncfs` before a switch, tear down, load the next, mount the same store. A small JS shell implements `spawn`/`exec` and returns to the menu on exit.
- **Build:** one CMake target per editor over a shared object library; browser builds use `-sEXIT_RUNTIME=0` like the player.

## Phase 2: Editors, one at a time

1. `palchos` (smallest; proves mouse, UI, save path).
2. `image`, `utility` (GIF import/export, `.gam` transfer; needed for the full loop).
3. `blocedit` (largest, 3k lines).
4. `mapmaker`, `charedit`, `monedit`, `grator`.
5. `sndedit` (VOC import from file, playback via existing audio; no hardware recording).
6. `menu`/`gm` launcher wiring.

Each is done when it passes the verification gate below.

## Phase 3: Make-a-game loop in the browser

- A web front page replaces `GM.EXE`'s menu: Projects, tool buttons, and Play (save, run transfer step, hand the `.gam` to the `gmplay` module in the same page).
- Upload/download single assets and whole-project zips (reuse the zip-drop code in `gmplay-web.js`).
- `.HLP` files reachable from the editors' F1.
- Autosave to IDBFS; warn on tab close with unsaved work.

## Phase 4: Polish and release

- Touch / small-screen ergonomics (scale-to-fit, on-screen modifier keys).
- Browser matrix: Chrome, Firefox, Safari (incl. audio, fullscreen, gamepad paths never exercised for the player).
- CI: extend `web` / `web-dist` jobs to build all editors and run the verification scripts.
- Ship a sample project only (game redistribution rights unsettled).

## Verification (same oracle idea as the player)

1. Record key/mouse event streams that drive each editor through its main features; replay in an instrumented DOS build under DOSBox and in the port.
2. Compare frame hashes at fixed points.
3. Compare saved files byte for byte (load shipped assets, edit, save, diff vs DOS output). File-format fidelity matters most.
4. Final loop: build a small game in the web editors, play it in the web player, replay against the DOS player.
5. Expect mismatches to be 16-bit arithmetic, UB or timing in the original, as with the player.

## Risks

- Large-model 16-bit code: far allocations, big buffers (`MEMBLOCA`, `GENBUFA`), possibly EMS/XMS.
- Mouse polling in tight loops: Asyncify yield points need tuning or the page stutters.
- Instantiating a fresh wasm per tool costs load/parse time: cache compiled modules.
- IDBFS can be cleared by the browser: zip export and visible save status.
- Untested for web: real audio, gamepad, non-Chrome browsers.

## First deliverable

Phases 0 and 1 plus `palchos` in the browser, saving a `.pal` that DOS reads identically.

## Status (2026-10-09)

Phase 0 done (`EDITORS_SURVEY.md`). Phase 1 started, native Windows build only (`cmake/Editors.cmake`, targets `gmmenu`,
`gmutility`, `gmpalchos`; `port\build.bat --target gmpalchos`).

Done and checked:
- `gmmenu` shows the original menu and its exit codes chain as in DOS (Play 10, Utilities 1, Design -> Palette 2,
  Character 6, Quit 0).
- `gmutility` shows its menu.
- `gmpalchos` (the first graphical editor): text menus, file prompt, New / Choose / Save palette, the 320x200 colour
  editor with the software cursor following the real mouse, click on the +/- buttons changes the colour. A palette
  loaded and saved again is byte-identical to the shipped one.
- The player is unaffected: `check_states.sh bcuda houses volume zark` identical to the DOS tables.

What it took (all in the port or as `GM_PORT` guards in `code/GM`):
- `MOUSE` (old-mouse mode of GENC/WINDIO) is a playgame-only setting; the editors build with `GM_EDITOR`.
- `NEWMOUSE.ASM` replaced by `src/newmouse_port.cpp`: tick counter on INT 1Ch, mouse polled from `gm_pump()` through
  a hook, row transfers. MouseClass::Change() is bypassed (its mickey loops cannot work with the values a modern
  pointer gives); the cursor is moved with `Cur.Move`, and when the program moves the cursor the OS pointer follows.
- `horizmenu()` read its x list by walking the stack after `y`; now `va_list` (32-bit ints on the host).
- Borland-isms: `long int` -> `gm_long`/`gm_ulong`, `"..."GMVER"..."` (user-defined literal in C++11), inline `asm`,
  a hard-coded `d:\drv\gen.h`, int-to-enum, `extern int CurMode` vs `unsigned int`, `_new_handler`, a 32-bit
  `OldTimer` that cannot hold a pointer, `WindAddr = 0xA0000000`.
- A PC starts in text mode with the BIOS palette; the platform now sets it up before the first palette access.
- `CRIPPLEWARE` (GMGEN.H) is off under `GM_PORT`: the source tree is the demo configuration, which refuses to save
  anything but maps; the shipped runtime executables are the full version.

Not done yet in phase 1:
- `blocedit mapmaker monedit charedit image grator sndedit` targets (need `CLRBLOCA.ASM` for mapmaker, the inline asm
  in `SOUNDC.C` for charedit and sndedit, GIFC for image, GRAMAP for grator, `long`/enum fixes as they show up).
- The editors as one launcher (native: spawn by exit code; web: module switching), then the wasm build.
- Keyboard-only paths through every menu (a keyboard move can be undone by the pointer if it is not synchronised;
  `setmoupos` is honoured, but check each editor), Ctrl/Alt key state, real-mouse feel at 640x480 and larger.
- Web: `NewTimer(...)` and the other `(...)` interrupt handlers need exact function types in wasm; OS pointer warping
  does not exist in a browser (the cursor must then follow the pointer without warping).
