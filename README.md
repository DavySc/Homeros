# Zig debugger prototypes (Homeros)

Learning prototypes for a Zig debugger in Homeros. Never merged to main (orphan branch, like the
debugger prototype on `claude/project-thread-l4spyq`). Linux first (Davy, 2026-11-23).

- `oracle/gen.py SEED`: a random Zig program; its own `std.fmt` "{any}" output is the truth.
- `oracle/score.py ZIG ZDB FIRST LAST [self|llvm]`: builds programs, runs a debugger, compares.
- `c/`: prototype P1, a native C debugger that reads the self-hosted backend's DWARF.

Build P1: `gcc -std=gnu17 -Wall -Wextra -g -O1 -fsanitize=address,undefined c/*.c -lm -o zdb`
