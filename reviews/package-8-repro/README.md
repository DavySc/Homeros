# Repro files for reviews/package-8.md

Set-up: CI's apt packages; a headless sway (`WLR_BACKENDS=headless WLR_RENDERER=pixman
WLR_LIBINPUT_NO_DEVICES=1 sway -c sway.cfg`) with XDG_RUNTIME_DIR and WAYLAND_DISPLAY exported
(run.sh, jk.sh and keyloss.sh read them from an `env.sh` beside them).

- `pix.c`, `keys.c`, `clip.c`: probes that use only backend.h, so they link against either
  build. Add to src/wl/build.mk:
  `build/wl/probe-%: <dir>/%.c $(SRCS) $(OBJS) include/*.h` /
  `	gcc $(WARN) $(CPPFLAGS) $(SAN) -o $@ $< $(SRCS) $(OBJS) $(LIBS)`
  - `probe-pix SEED FRAMES DIR`: incremental frames against a whole redraw (finding 1).
    `EVERY=1` checks after every frame.
  - `probe-keys`: keys left in the backend's queue (finding 2).
  - `probe-clip`: a reader of our selection that stalls 5 s (finding 3).
- `gone.sh`: the compositor killed under the editor (finding 4).
- `a1-fix.diff`: the 7-line change that made probe-pix pass (a sketch, not a patch to merge unread).
- `b-port.diff`: the 6 lines build B needed to compile against main at ab618e3.
- `run.sh A|B type2000|type200|scroll|page`: the benchmark (build A's src/wl/test/bench.c linked
  to each backend; paths are this session's worktrees). `bench2.log`, `idle1.log`, `idle2.log`: raw results.
- `jk.sh`, `keyloss.sh`: real-editor checks. Both need a keyboard that stays attached
  (`wtype -s 600000 &`): the first key of a keyboard that has just appeared is lost by both builds,
  which is the harness, not the editor.
