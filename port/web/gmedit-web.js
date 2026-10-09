// The page around the WebAssembly design tools. It plays the part of GM.EXE (code/GM/GM.ASM): it runs one program after
// the other, and the program that has just ended says, by its exit code, which one runs next. Every program is its own
// WebAssembly module (gm<name>.js / .wasm, built with -sMODULARIZE -sEXPORT_ES6); they all work on the folder /gm, which is
// kept in IndexedDB, so that what one tool saves is there for the next one and for the next visit.
//
// Everything stays in the browser.
'use strict';

const $ = (id) => document.getElementById(id);

// GM.ASM's table: the exit code of a program is the number of the one to run next; 0 ends GameMaker.
const PROGRAMS = ['', 'gmutility', 'gmpalchos', 'gmblocedit', 'gmmonedit', 'gmmapmaker', 'gmcharedit', 'gmimage', 'gmsndedit',
  'gmgrator', 'gmplay', 'gmmenu'];
const QUIT = 0, PLAYGAME = 10, MENU = 11;
const TITLES = { gmutility: 'Utilities', gmpalchos: 'Palette designer', gmblocedit: 'Block designer', gmmonedit: 'Monster maker',
  gmmapmaker: 'Map maker', gmcharedit: 'Character maker', gmimage: 'Image reader', gmsndedit: 'Sound designer',
  gmgrator: 'Integrator', gmplay: 'Player', gmmenu: 'Main menu' };

// ---------------------------------------------------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------------------------------------------------
const logLines = [];
function log(text, isError) {
  logLines.push(text);
  if (logLines.length > 300) logLines.shift();
  $('log').textContent = logLines.join('\n');
  if (isError) console.warn(text); else console.log(text);
}
function message(text, isError) {
  $('message').textContent = text;
  $('message').className = isError ? 'error' : '';
}
const show = (id, on) => $(id).classList.toggle('hidden', !on);

// ---------------------------------------------------------------------------------------------------------------------
// zip files (stored or deflated), read with the browser's own DecompressionStream
// ---------------------------------------------------------------------------------------------------------------------
async function inflateRaw(bytes) {
  if (typeof DecompressionStream === 'undefined') throw new Error('This browser cannot read zip files (no DecompressionStream)');
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function unzip(buffer) {
  const v = new DataView(buffer), u8 = new Uint8Array(buffer), dec = new TextDecoder('utf-8');
  let eocd = -1;
  for (let i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) {
    if (v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('This is not a zip file');
  const count = v.getUint16(eocd + 10, true);
  let p = v.getUint32(eocd + 16, true);
  const files = [];
  for (let n = 0; n < count; n++) {
    if (v.getUint32(p, true) !== 0x02014b50) throw new Error('Damaged zip file');
    const method = v.getUint16(p + 10, true);
    const csize = v.getUint32(p + 20, true);
    const nlen = v.getUint16(p + 28, true), elen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true);
    const local = v.getUint32(p + 42, true);
    const name = dec.decode(u8.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + elen + clen;
    if (name.endsWith('/')) continue;
    const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
    const raw = u8.subarray(start, start + csize);
    if (method !== 0 && method !== 8) throw new Error('Unsupported zip compression in ' + name);
    files.push({ path: name, data: method === 0 ? raw : await inflateRaw(raw) });
  }
  return files;
}

// ---------------------------------------------------------------------------------------------------------------------
// the shared folder /gm (IndexedDB) as seen by the program that is running
// ---------------------------------------------------------------------------------------------------------------------
const ROOT = '/gm';
let current = null;          // { name, mod, canvas, persistTimer }
let seedFiles = null;        // the shipped files (gmdata.zip), read once

function mkdirs(FS, path) { try { FS.mkdirTree(path); } catch (e) { /* exists */ } }
function dirname(p) { const i = p.lastIndexOf('/'); return i < 0 ? '' : p.slice(0, i); }

function syncfs(mod, fromStore) {
  return new Promise((resolve) => mod.FS.syncfs(fromStore, (err) => { if (err) log('IndexedDB: ' + err, true); resolve(); }));
}

async function loadSeed() {
  if (seedFiles) return seedFiles;
  const r = await fetch('gmdata.zip');
  if (!r.ok) throw new Error('Could not download gmdata.zip (' + r.status + ')');
  seedFiles = await unzip(await r.arrayBuffer());
  return seedFiles;
}

// The first visit: the shipped help files and the sample game go into the store. Later visits find them there, together
// with whatever the person made.
async function mountStore(mod) {
  const FS = mod.FS;
  mkdirs(FS, ROOT);
  let persistent = true;
  try {
    FS.mount(FS.filesystems.IDBFS, {}, ROOT);
    await syncfs(mod, true);
  } catch (e) {
    persistent = false;
    log('Saving is not available in this browser (' + e.message + '); your work will be lost when the page closes.', true);
  }
  if (!FS.analyzePath(ROOT + '/.seeded').exists) {
    for (const f of await loadSeed()) {
      mkdirs(FS, ROOT + '/' + dirname(f.path));
      FS.writeFile(ROOT + '/' + f.path, f.data);
    }
    FS.writeFile(ROOT + '/.seeded', '1');
    if (persistent) await syncfs(mod, false);
  }
  FS.chdir(ROOT);
  return persistent;
}

function persistNow() {
  if (current && current.persistent) current.mod.FS.syncfs(false, (err) => { if (err) log('IndexedDB: ' + err, true); });
}

// ---------------------------------------------------------------------------------------------------------------------
// running one program
// ---------------------------------------------------------------------------------------------------------------------
function freshCanvas() {
  const stage = $('stage');
  stage.textContent = '';
  const c = document.createElement('canvas');
  c.id = 'canvas';
  c.tabIndex = 0;
  c.addEventListener('contextmenu', (e) => e.preventDefault());
  stage.appendChild(c);
  return c;
}

// resolves with the exit code, or rejects if the module could not start
async function runProgram(name, args) {
  const canvas = freshCanvas();
  let notify;
  const ended = new Promise((resolve) => { notify = resolve; });
  window.gmProgramExited = (code) => notify(code);
  const factory = (await import('./' + name + '.js')).default;
  const mod = await factory({
    noInitialRun: true,
    canvas,
    print: (t) => log(t, false),
    printErr: (t) => log(t, true),
    onAbort: (what) => { log('The program stopped: ' + what, true); notify(-1); },
  });
  const persistent = await mountStore(mod);
  current = { name, mod, canvas, persistent, persistTimer: setInterval(persistNow, 4000) };
  $('program-name').textContent = TITLES[name] || name;
  canvas.focus();
  mod.callMain(args);
  const code = await ended;
  clearInterval(current.persistTimer);
  if (persistent) await syncfs(mod, false);
  current = null;
  canvas.remove();
  return code;
}

// GM.ASM's main loop. `last` is the program that ran before (the menu opens on its entry).
async function gameMaker() {
  show('cover', false);
  show('bar', true);
  let last = PLAYGAME;
  let next = await runChecked(MENU, ['cOoL', String.fromCharCode(65 + last)]);
  while (next > QUIT && next <= MENU) {
    const ran = next;
    const args = ran === MENU ? ['cOoL', String.fromCharCode(65 + last)] : [];
    if (ran === PLAYGAME) {
      next = await runPlayer();
    } else {
      next = await runChecked(ran, args);
    }
    if (ran !== MENU) last = ran;
  }
  finished(next);
}

async function runChecked(prog, args) {
  try {
    const code = await runProgram(PROGRAMS[prog], args);
    return code < 0 ? QUIT - 1 : code;
  } catch (e) {
    log('Could not run ' + PROGRAMS[prog] + ': ' + (e.message || e), true);
    return QUIT - 1;
  }
}

// The player is its own page for now (index.html); it plays the games that are dropped on it or bundled with it.
function runPlayer() {
  return new Promise((resolve) => {
    $('stage').textContent = '';
    show('bar', false);
    show('cover', true);
    $('cover').querySelector('h1').textContent = 'Play';
    message('The player is a separate page.');
    $('start').disabled = false;
    $('start').textContent = 'Back to the menu';
    $('start').onclick = () => { show('cover', false); show('bar', true); resolve(MENU); };
    const note = document.createElement('p');
    note.className = 'note';
    note.id = 'play-note';
    note.innerHTML = 'Open <a href="index.html" target="_blank" rel="noopener">the player</a> in another tab to play a game.';
    $('cover').querySelector('.card').appendChild(note);
  });
}

function finished(code) {
  $('stage').textContent = '';
  show('bar', false);
  show('cover', true);
  $('cover').querySelector('h1').textContent = 'GameMaker closed';
  $('start').textContent = 'Start again';
  $('start').disabled = false;
  $('start').onclick = () => location.reload();
  message(code < 0 ? 'A program could not start. See "Messages".' : 'Your work is saved in this browser.', code < 0);
  if (document.fullscreenElement) document.exitFullscreen();
}

// ---------------------------------------------------------------------------------------------------------------------
// keyboard: what a browser would act on while a program runs
// ---------------------------------------------------------------------------------------------------------------------
const KEEP_FOR_PROGRAM = new Set(['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space', 'Tab', 'Backspace', 'Enter',
  'PageUp', 'PageDown', 'Home', 'End', 'Insert', 'Delete', 'Escape', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10']);
window.addEventListener('keydown', (e) => {
  if (!current) return;
  if (e.target && /^(INPUT|TEXTAREA|SELECT|BUTTON)$/.test(e.target.tagName) && e.code !== 'Tab') return;
  if (KEEP_FOR_PROGRAM.has(e.code) || (e.altKey && /^(Arrow|Enter|Key)/.test(e.code))) e.preventDefault();
}, true);
// the program that has ended no longer listens (its window is closed), but a late event must not reach it
window.addEventListener('pagehide', persistNow);
document.addEventListener('visibilitychange', () => { if (document.hidden) persistNow(); });

// the bar: shown when the pointer is near the top edge
let hideTimer = 0;
function showBar() {
  $('bar').classList.remove('faded');
  clearTimeout(hideTimer);
  hideTimer = setTimeout(() => $('bar').classList.add('faded'), 3500);
}
window.addEventListener('mousemove', (e) => { if (current && e.clientY < 60) showBar(); });

$('btn-full').onclick = (e) => {
  e.target.blur();
  if (document.fullscreenElement) document.exitFullscreen(); else document.documentElement.requestFullscreen().catch((err) => log('Full screen refused: ' + err.message, true));
};
document.addEventListener('fullscreenchange', () => {
  const on = !!document.fullscreenElement;
  if (navigator.keyboard && navigator.keyboard.lock) {
    if (on) navigator.keyboard.lock(['Escape']).catch(() => {}); else navigator.keyboard.unlock();   // Esc goes to the program
  }
  $('btn-full').textContent = on ? 'Leave full screen' : 'Full screen';
  if (current) current.canvas.focus();
});
// mouse capture: the browser hides its cursor (pointer lock) and the program's own cursor is the only one; the program gets
// the movement (dosplat.c adds it up). The browser gives the mouse back on Esc, which is also this page's way out.
function toggleCapture() {
  if (!current) return;
  if (document.pointerLockElement) document.exitPointerLock();
  else {
    // raw movement first: with the ordinary lock some systems (remote desktops, virtual machines) stop reporting movement
    // when the real pointer reaches the edge of the host screen, and the program's cursor would never reach its own edge
    const c = current.canvas;
    const plain = () => { const r2 = c.requestPointerLock(); if (r2 && r2.catch) r2.catch((err) => log('Mouse capture refused: ' + err.message, true)); };
    let r;
    try { r = c.requestPointerLock({ unadjustedMovement: true }); } catch (err) { plain(); return; }
    if (r && r.catch) r.catch(plain);
  }
}
$('btn-capture').onclick = (e) => { e.target.blur(); toggleCapture(); };
document.addEventListener('pointerlockchange', () => {
  $('btn-capture').textContent = document.pointerLockElement ? 'Release mouse' : 'Capture mouse';
  if (current) current.canvas.focus();
});
window.addEventListener('keydown', (e) => { if (e.altKey && e.code === 'KeyG') { e.preventDefault(); toggleCapture(); } }, true);
$('log-toggle').onclick = () => $('log').classList.toggle('hidden');

// ---------------------------------------------------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------------------------------------------------
window.addEventListener('error', (e) => log('Error: ' + e.message, true));
(async function init() {
  if (!window.WebAssembly) { message('This browser does not support WebAssembly.', true); return; }
  try {
    await loadSeed();
    message('Ready.');
    $('start').disabled = false;
  } catch (e) {
    message(e.message || String(e), true);
    return;
  }
  $('start').onclick = () => { $('start').disabled = true; gameMaker(); };
  if (new URLSearchParams(location.search).has('autostart')) $('start').click();
})();
