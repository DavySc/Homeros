# Repro files for reviews/pr-68.md

Set-up: CI's apt packages (`.github/workflows/check.yml`).

- `bear.c PX`, `ctx.c`: font.h only.
  `gcc -Iinclude -fsanitize=address,undefined -o bear reviews/pr-68-repro/bear.c src/font/font.c src/uni/uni.c $(pkg-config --cflags --libs freetype2 harfbuzz fontconfig)`
- `lig.c`, `pix2.c`: backend.h only. With a file `probe.mk` holding
  `build/wl/probe-%: reviews/pr-68-repro/%.c $(SRCS) $(OBJS) include/*.h` /
  `	gcc $(WARN) $(CPPFLAGS) $(SAN) -o $@ $< $(SRCS) $(OBJS) $(LIBS)`
  run `make -f Makefile -f probe.mk build/wl/probe-lig`, then
  `reviews/pr-68-repro/sway.sh sh -c 'build/wl/probe-lig $PR68_DIR'` (a headless sway).
  The control is the same gcc line with a copy of `src/wl/wl.c` that has the plant's old line.
- `sketch.diff`: the three lines that made `probe-lig` print "same" 14 of 14 (a sketch, not a patch to merge unread).
