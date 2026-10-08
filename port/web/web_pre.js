// Browser target: runs inside the Emscripten module, before the engine starts (see port/CMakeLists.txt).
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  // Settings (window mode, volume, ...) are kept here; the page backs this folder with IndexedDB (gmplay-web.js).
  ENV['GM_CONFIG_DIR'] = '/persist/settings';
});
