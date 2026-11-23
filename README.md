# Zig debugger prototypes (Homeros)

Learning prototypes for a Zig debugger in Homeros. Never merged to main (orphan branch, like the
debugger prototype on `claude/project-thread-l4spyq`). Linux first (Davy, 2026-11-23).

- `oracle/gen.py SEED`: a random Zig program; its own `std.fmt` "{any}" output is the truth.
- `oracle/score.py ZIG ZDB FIRST LAST [self|llvm]`: builds programs, runs a debugger, compares.
- `c/`: prototype P1, a native C debugger that reads the self-hosted backend's DWARF.
  `zdb session ZIG FILE` runs FILE under the incremental compiler and reads commands (`b LINE`,
  `c`, `p NAME`, `bt`, `w` after a save, `d`, `q`): saving patches the running process, and a
  safety panic stops at the user's line; after a fix and `w` the failing call starts again.
- `oracle/ec_stress.py`, `oracle/panic_stress.py`: random edit-and-continue and fix-after-panic trials.

Findings, measurements and lessons: the knowledge base in the project files, `zig-debugger-kb/`.

Build P1: `gcc -std=gnu17 -Wall -Wextra -Wshadow -g -O1 -fsanitize=address,undefined c/*.c -lm -o zdb`
