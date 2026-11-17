#!/bin/bash
# How the GUI prototypes were built and run for the review (the design's measured-2026-11-17.md has no commands).
# G = result of `nix-build ../../../native-editor/gui/env.nix -o guitools`; run from a local copy of gui/ plus this folder.
G=${G:-$PWD/guitools}; export PATH=$G/bin:$PATH PKG_CONFIG_PATH=$G/lib/pkgconfig:$G/share/pkgconfig
XDG=$(find -L $G/share/wayland-protocols -name xdg-shell.xml | head -1)
wayland-scanner client-header $XDG xdg-shell.h; wayland-scanner private-code $XDG xdg-shell.c
gcc -O2 -o wayland-ed  wayland-ed.c  xdg-shell.c -I. $(pkg-config --cflags --libs wayland-client xkbcommon freetype2)
gcc -O2 -o wayland-ed2 wayland-ed2.c xdg-shell.c -I. $(pkg-config --cflags --libs wayland-client xkbcommon freetype2)
gcc -O2 -o sdl3-ed sdl3-ed.c $(pkg-config --cflags --libs sdl3 sdl3-ttf)
export XDG_RUNTIME_DIR=$PWD/xdg; mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
MESA="LD_LIBRARY_PATH=$G/lib __EGL_VENDOR_LIBRARY_DIRS=$G/share/glvnd/egl_vendor.d LIBGL_DRIVERS_PATH=$G/lib/dri"   # SDL3 and weston's GL renderer need these
weston --backend=headless --renderer=pixman --width=1920 --height=1080 --socket=wl-rev --idle-time=0 >weston.log 2>&1 &
env $MESA weston --backend=headless --renderer=gl --width=1920 --height=1080 --socket=wl-gl --idle-time=0 >weston-gl.log 2>&1 &
sleep 3
FONT=$G/share/fonts/truetype/NerdFonts/JetBrainsMono/JetBrainsMonoNerdFont-Regular.ttf; SRC=${SRC:-wayland-ed.c}
for d in wl-rev wl-gl; do
  echo "== $d: prototype as shipped";             WAYLAND_DISPLAY=$d INJECT=1 ./wayland-ed  $SRC $FONT
  echo "== $d: instrumented, 200 keys at 10 ms";  WAYLAND_DISPLAY=$d INJECT=1 ./wayland-ed2 $SRC $FONT
  echo "== $d: 2000 keys at 1 ms";                WAYLAND_DISPLAY=$d INJECT=1 RATE_US=1000 NKEYS=2000 ./wayland-ed2 $SRC $FONT
  echo "== $d: 2000 keys at 1 ms, frame callbacks"; WAYLAND_DISPLAY=$d FRAMECB=1 INJECT=1 RATE_US=1000 NKEYS=2000 ./wayland-ed2 $SRC $FONT
done
export WAYLAND_DISPLAY=wl-rev LIBDECOR_PLUGIN_DIR=/nonexistent
for i in 1 2 3; do env $MESA INJECT=1 ./sdl3-ed $SRC $FONT; done
SDL_RENDER_DRIVER=software INJECT=1 ./sdl3-ed $SRC $FONT      # without Mesa: "Window framebuffer support not available"
./idle.sh ./wayland-ed $SRC $FONT; env $MESA ./idle.sh ./sdl3-ed $SRC $FONT
# Glyph coverage and shaping: hb-shape --no-positions --no-clusters $FONT --text='⦃'   prints [.notdef] for a missing glyph
