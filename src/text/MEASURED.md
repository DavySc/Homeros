# Text rope (package 1, build A): what was run

`src/text/text.c` is a B-tree of 1 KB byte chunks, ropey's shape (DESIGN.md, Text): all leaves at one depth, every node but the root at least half full, each parent holding its children's byte and `'\n'` counts. An edit or a lookup walks one root-to-leaf path. 244 lines, 227 non-blank.

## Correctness

| Check | What | Result | Tag |
|---|---|---|---|
| `tests/test_text.c` (thread 0) | 200,000 random small edits against an array, all line lookups | pass | measured, 1 run per `make test` |
| `tests/test_text_rope.c` | 20,000 edits on 1 to 6 MB texts (1% of them up to 256 KB), tree height up to 3; after each edit the bytes around it, every 200 edits the whole tree's shape (fill, counts), all bytes and 40 line lookups; malloc failing at each of the 311 allocations of a 300 KB insert, and at each allocation of `text_new` | pass, text unchanged after every failed allocation; 9 s under ASan+UBSan | measured |
| `tests/fuzz_text_rope.c` | libFuzzer, edits up to 8 KB; after each, the shape, all bytes, a short read from the edit, the line count and line lookups (one past the last line too) | 32,414 runs in 60 s, nothing found; llvm-cov over 30 s (42,820 runs): 176 of 179 lines and 91 of 100 branches, all missed ones out-of-range or out-of-memory returns and `text_free(NULL)`, which the unit tests cover (corrected after review: the first version never read short of the end, never asked past the last line and never called `text_lines`) | measured, 1 run of each |
| `tests/plants/text.txt` | 7 planted bugs: join at a quarter, uneven split, no rollback on failed allocation, line start off by one, newline count missing a byte, shared node's count wrong, leaf insert dropping a byte | 7 of 7 caught | measured, `tools/planted.sh` |
| `make conform` and the recorded traces | the thin editor's Vim keys against Neovim, now on the rope | 0 differ | measured |

The test and the fuzzer compile `text.c` a second time under other names (`#define text_new r_new` and so on) so that they can see the nodes; `text.h` stays the only interface the editor uses.

Out of memory: an insert takes the nodes its splits can need (one per level plus a new root) before it changes anything, so a split cannot fail halfway; a multi-chunk insert that cannot get them deletes what it already put in. Deletion never allocates. So `text_insert`'s -1 leaves the bytes unchanged (measured above; the tree may be shaped differently).

## Against ropey 1.6.1

`src/text/bench/run.sh` runs `bench.c` (the rope through `text.h`) and `rope/` (the same steps on ropey 1.6.1, release, LTO): open a file, random 1 to 10 byte inserts and deletes logged for undo, undo them all and check the text is back, read it all out, then random line lookups both ways (`text_line_start`/`line_to_char`, `text_line_of`/`char_to_line`). The input is `design/proto/gen_input.py`'s Lean-like ASCII text, so ropey's char indices are bytes. gcc 13 -O2.

Instructions (callgrind, exact, so one run each), 10 MB; each step is the difference to the run without it:

| Step | C rope | ropey | Tag |
|---|---|---|---|
| open + read out + compare | 37.0 M | 65.2 M | measured |
| 5,000 edits + undo | 11.5 M | 38.3 M | measured |
| 100,000 lookups each way | 137.0 M | 503.2 M | measured |

Wall time on 100 MB, 50,000 edits and 1,000,000 lookups each way, 5 alternating runs per side, median (range), seconds:

| Step | C rope | ropey | Verdict |
|---|---|---|---|
| open (`text_new`, `Rope::from_str`) | 0.077 (0.061 to 0.110) | 0.119 (0.093 to 0.125) | ranges overlap: no measurable difference |
| 50,000 edits | 0.048 (0.041 to 0.055) | 0.093 (0.081 to 0.114) | C faster |
| undo them | 0.028 (0.028 to 0.029) | 0.046 (0.039 to 0.078) | C faster |
| read 100 MB out | 0.075 (0.068 to 0.098) | 0.104 (0.083 to 0.112) | ranges overlap: no measurable difference |
| 2,000,000 lookups | 1.340 (1.257 to 1.393) | 1.614 (1.498 to 1.658) | C faster |
| peak RSS (with the 100 MB file and the 100 MB read-back buffer both hold) | 316 MB, all 5 runs | 325 MB, all 5 runs | C smaller |

Open is at 0.08 s against DESIGN.md's 0.3 s target for a 100 MB file (measured, rope build only; reading the file is not counted).

Two changes came from callgrind, both measured on the 10 MB run: counting `'\n'` in blocks of 32 bytes, which gcc and clang -O2 turn into vector compares, and `memchr` to find the l-th `'\n'` in a leaf, took the whole run from 854.8 M to 182.3 M instructions (gcc; clang 180.0 M).

Not measured: Davy's own files (the design asks the rope to match ropey "on your files"; these are generated). Whether the difference to ropey comes from bytes against chars (ropey also counts chars and UTF-16 units) or from the tree is not separated: hypothesis that most of it is the char counting.

## Proof spike

`nls`, the newline count every count in the tree comes from, has a result contract (`\result == nl(s, n)`, with `nl` defined by recursion) and loop invariants, from the review: 26 of 26 goals prove in about 4 s (measured, 1 run); a planted miscount in the tail loop leaves 4 unproved (measured by the reviewer, 1 run). Its block loop is bounded by `n - i >= 32`, because with `i + 32 <= n` the sum can wrap and 3 goals time out (measured by the reviewer).

    frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct nls -cpp-extra-args="-Iinclude -D_DEFAULT_SOURCE" src/text/text.c

The rest of the file:

Frama-C WP with `-wp-rte` (z3, cvc5, 10 s per goal) on `text.c` as it is, with no contracts: 113 of 297 runtime-error goals prove, 184 time out (measured, 1 run, about 15 minutes). The unproved ones are almost all memory accesses through a child pointer (`ins` 12, `text_insert` 11, `del` 11, `join` 10, `sum` 9, ...): nothing says a node is valid, so nothing can be proved about reading it. Going further needs a recursive ACSL predicate for a well-formed tree (each kid valid, separated from the others, of height h - 1, its counts right, filled at least half) that every function requires and keeps, which also has to survive `malloc`, `free` and the spare pool. That is the work the design calls much harder than a byte scan, and it was not started: the gate for this package is the differential fuzzing above (hypothesis that a full proof is weeks of work, judged from the earlier CBMC and Kani failures on a smaller structure, not measured).

Reproduce: `frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 10 -cpp-extra-args="-Iinclude -D_DEFAULT_SOURCE" src/text/text.c`.

## Fix: line lookups among empty lines (2026-11-17)

The keys package found `text_line_start` slow on a file of empty lines: it called `memchr` once per `'\n'` in the leaf, so a leaf of 1,016 empty lines cost up to 1,016 calls per lookup, and `fuzz_keys` timed out on `2222222o` (2.2 M lines). It now skips whole 32-byte blocks by their count (`nls`) and steps bytes only inside the last block.

| Every line start of 2.2 MB, gcc -O2, 5 alternating runs, ns per lookup, median (range) | before | after | Verdict |
|---|---|---|---|
| all empty lines | 3,633.8 (3,576.2 to 3,712.0) | 73.7 (64.8 to 88.5) | 49 times faster |
| 50-byte lines | 97.1 (93.7 to 176.8) | 110.2 (87.5 to 115.9) | ranges overlap: no measurable difference |

The PR's review measured more line lengths (`reviews/pr-28.md`, every line start of 2.2 MB, -O2, 5 alternating runs, median ns per lookup): up to 16-byte lines the fix is 2.5 to 60 times faster; 31 to 200-byte lines are inside the run-to-run spread; from about 500 bytes it costs 30 to 50 ns more per lookup than before (1,000-byte lines with gcc: 58.9 before, 107.4 after), because one `memchr` over the leaf did what now takes up to 31 block counts (measured by the reviewer). That is small next to the gain on short lines; whether a scan exists that is no slower on any line length is a hypothesis nobody has tested.

Callgrind, 1 run each (exact): all line starts of 220,000 empty lines, whole program 3,165.3 M to 118.4 M instructions; `bench.c`'s 100,000 lookups each way on 10 MB, 136.0 M to 126.1 M.

`test_text_rope` now times every line lookup among 200,000 empty lines against 100,000 lines of 50 bytes and requires the first under 4 times the second. Under ASan: before, the check failed 5 of 5 runs; after, it passed 5 of 5 with the two within 4% of each other (measured). Its plant puts the per-line `memchr` back and is caught.
