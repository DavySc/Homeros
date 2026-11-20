# Repro files for reviews/pr-82.md

Set-up: CI's apt packages (`.github/workflows/check.yml`).

- `cur.c`: backend.h and font.h only. With a file `probe.mk` holding
  `build/wl/probe-%: reviews/pr-82-repro/%.c $(SRCS) $(OBJS) include/*.h` /
  `	gcc $(WARN) $(CPPFLAGS) $(SAN) -o $@ $< $(SRCS) $(OBJS) $(LIBS)`
  run `make -f Makefile -f probe.mk build/wl/probe-cur`, then
  `reviews/pr-68-repro/sway.sh sh -c 'build/wl/probe-cur $PR68_DIR'` (a headless sway).
  It ends with "N checks failed": a check is one cell whose ink count changes with the
  cursor's place, or one cursor step whose partial redraw differs from a whole one.
  The control is the same gcc line with main's `src/wl/wl.c` (93b3e3b): 26 checks fail.
- `mutants.txt`: the four bugs planted in the new lines, in `tests/plants/` format.
