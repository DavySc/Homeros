# Repro files for reviews/pr-92.md

Set-up: CI's apt packages (`.github/workflows/check.yml`).

- `cur2.c`: `reviews/pr-82-repro/cur.c` with an attr per cell, a start column and more
  words (⟹ with faces, FACE_VISUAL, ATTR_REV, ATTR_DIM; after a letter, a ligature and ↔;
  中中, 中->, 👍; all at column 0 and 7; ⟹ and 中 at the row's end). Built and run as cur.c
  (`reviews/pr-82-repro/README.md`, the rule's path changed to this directory). The control
  is the same gcc line with main's `src/wl/wl.c` (2d6cf0d).
- `cur2-head.txt`, `cur2-main.txt`: its output at a4cb40f (4 checks failed) and with main's
  wl.c (32).
- `cost.log`: `reviews/pr-68-repro/cost.c` at -O2, 5 alternating runs a side.
