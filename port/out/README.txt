GameMaker Player for the web
============================

This folder is a complete static web site. Put it on any web server (no special headers or server software needed),
or try it locally:

    cd <this folder>
    python3 -m http.server 8000        then open  http://localhost:8000/

(Opening index.html straight from the disk does not work: browsers do not let a page load .wasm files from file://.)

Games packed into this build: pipemare
Players can also drop a game folder or a .zip onto the page; those files are read in the browser and not uploaded.

Contents
  index.html, gmplay-web.js   the page
  gmplay.js, gmplay.wasm      the player (the 1994 engine compiled to WebAssembly)
  games.json, games/*.zip     the packed games (games.json lists them for the page)
  LICENSE, THIRD_PARTY.md, licenses/   licences of the engine and of what it builds on

Before you publish: the games in games/ are not covered by the licences above. Include only games you have the right to
distribute. Rebuild with a different list:  bash port/build_web.sh --games none   (or --games name1,name2 or --games all)

Browsers: current Chrome, Edge, Firefox and Safari (WebAssembly, DecompressionStream for zip files). Esc is kept for the
game in full screen on Chrome and Edge only. Touch screens are not supported.
