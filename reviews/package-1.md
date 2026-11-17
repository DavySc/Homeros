# Review of package 1 (text rope): build A (PR 5, head 9735464) against build B (PR 4, head 7ffba50)

Independent review and comparison, as DESIGN.md asks for the rope ("two threads build it independently and the better one wins on the package's own checks") and PROJECT-INSTRUCTIONS.md step 3 ("less code and better measurements wins. Record the comparison"). Everything below was run by the reviewer on one machine (4 cores, gcc 13.3, clang 18.1, valgrind 3.22, the tools of `tools.nix`, ropey 1.6.1 with Rust 1.97), both builds under the same conditions. The sources are in `reviews/r1/`; the raw timings are `reviews/r1/timing.log`. Tags as CLAUDE.md asks.

**Winner: build A.** Both builds pass every check of the package and of the repo. A does it in 24% fewer lines and is faster on the edits made at every keystroke. B is faster on bulk edits and on reads that span several leaves, and B alone proved its newline counter. A stays inside the ropey budget in every scenario, so B's lead on bulk edits does not buy anything the design asks for, and its price is 77 lines.

**Verdict on A: approve after two small changes** (findings 1 and 2). Nothing here needs Davy.

## 1. Checks, same conditions for both

| Check | Build A | Build B | Tag |
|---|---|---|---|
| `make check` (compilers, tests under ASan+UBSan, fuzzers 30 s each, 2,000 fresh conform sequences, undo-diff, lean, frama 77 of 77, specs) | passed | passed | measured, 1 run each |
| `make planted` | 24 of 24 caught (its 7 text plants among them) | 23 of 23 caught (its 6 text plants among them) | measured, 1 run each |
| Thread 0's `fuzz_text`, 30 s inside `make check` | 1,513,402 runs, nothing | 1,376,761 runs, nothing | measured, 1 run |
| The build's own `fuzz_text_rope`, 30 s inside `make check` | 29,188 runs, nothing | 11,417 runs, nothing | measured, 1 run |
| Reviewer's fuzzer `r1/xfuzz.c` (one source for both; edits up to 131 KB, text up to 2 MB, tree shape, all bytes, line lookups, short reads and out-of-range calls checked after every edit), 600 s, `-max_len=1024` | 51,651 runs, nothing | 51,340 runs, nothing | measured, 1 run each |
| Reviewer's deep-tree test `r1/stress.c` (texts of 0 to 48 MB, height 4, edits up to 24 MB, a failing allocation in 1 insert of 50, shape and all bytes every 16 edits and after every large one), ASan+UBSan | 5 seeds of 3,000 edits pass | 5 seeds of 3,000 edits pass | measured, 5 runs each |
| Reviewer's out-of-memory test `r1/oom.c` (every allocation of inserts of 15 sizes at 3 places in 5 texts, and of `text_new`): -1 or NULL, bytes unchanged, shape kept, text still usable | 17,695 failed allocations, all clean | 18,021 failed allocations, all clean | measured, 1 run each |
| Coverage of `src/text/text.c` by the build's own fuzzer in 30 s (llvm-cov) | 175 of 179 lines, 88 of 100 branches | 214 of 214 lines, 121 of 128 branches | measured, 1 run each |

The reviewer's three programs were each seen to fail (measured, 1 run per plant): the fuzzer on A's "join at a quarter" plant in 2 s and "wrong count" plant in under 1 s, on B's "mends only the left side" in 3 s and "merge loses the count" in 1 s; `stress.c` and `oom.c` abort on all of those and on both builds' "no rollback" plants.

Both authors' numbers reproduce exactly where they are exact: A's callgrind table (37.0 M, 11.5 M, 137.0 M; ropey 65.2 M, 38.3 M, 503.2 M) and B's (54.5 M; ropey 113.7 M). A's fuzz plant asks for 120 s but is caught in under 0.3 s on 10 of 10 seeds (measured).

## 2. Code

| | Build A | Build B | Tag |
|---|---|---|---|
| `src/text/text.c`, lines | 244 | 321 | measured |
| of which code (no comments, no blank lines) | 207 | 251 (18 more are ACSL) | measured |
| `text.o`, text section, gcc -O2 | 5,197 bytes | 6,069 bytes | measured |
| Whole PR | +684 lines | +776 lines | measured |
| Delete | one leaf at a time from the root, then the usual B-tree join | drops whole subtrees in the range, then mends the two paths beside the cut | read |
| Read | one descent per leaf | one descent, then a walk | read |
| Proof | none (spike: 113 of 297 goals without contracts, not rerun) | `nls` carries a result contract; 25 of 25 goals proved (reproduced, 1 run) | measured |

B's README says its first version had three bugs in the delete path (an inner root left empty, one cut side not mended, a child left with one child); all three are fixed and covered, and nothing above finds another. The path is still the part of B that needed three attempts, and it is the 44 lines A does not have.

## 3. Measurements

Instructions, callgrind, exact, so 1 run each; 10 MB of `design/proto/gen_input.py`'s text; gcc -O2; counted inside the workload only. The scenarios are `r1/scen.c` and, for ropey, `r1/ropescen/`. Lower is better; the better of A and B is in bold.

| Scenario | Build A | Build B | ropey 1.6.1 |
|---|---|---|---|
| Open 10 MB | **29.9 M** | 31.3 M | 51.9 M |
| Type 100,000 bytes at one moving place | **60.2 M** | 75.1 M | 184.7 M |
| Backspace 100,000 bytes | **49.3 M** | 106.0 M | 224.7 M |
| 50,000 random edits of 1 to 10 bytes | **44.9 M** | 69.8 M | 145.6 M |
| A's bench: 5,000 edits and their undo | **11.5 M** | 16.6 M | 38.3 M |
| B's bench: whole workload | **47.5 M** | 54.5 M | 113.7 M |
| 200,000 `text_line_of` | **125.3 M** | 131.2 M | 494.8 M |
| 200,000 `text_line_start` | **148.5 M** | 156.1 M | 515.6 M |
| Read every line, one `text_read` per line | **250.3 M** | 288.7 M | 242.3 M (borrows, does not copy) |
| 20 times: paste 1 MB, delete it | 122.3 M | **75.2 M** | 122.1 M |
| 5 times: delete 5 MB, put it back | 142.7 M | **87.8 M** | 146.6 M |
| 100,000 times: a line start, then read 4,000 bytes | 228.8 M | **181.3 M** | 967.7 M |
| Read all 10 MB in one call | 3.2 M | **1.6 M** | 1.0 M (borrows) |

With clang -O2, A's bench gives A 35.6 M, 11.1 M, 136.5 M and B 33.7 M, 15.5 M, 141.6 M (open, edits, lookups): the same order except open.

Wall time on 100 MB, 5 alternating runs per side, median (range), seconds. "Same" means the ranges overlap.

| Step | Build A | Build B | ropey | A against B | Tag |
|---|---|---|---|---|---|
| Open | 0.091 (0.076 to 0.107) | 0.078 (0.077 to 0.090) | 0.111 (0.101 to 0.121) | same | measured |
| 100,000 typed bytes | 0.005 (0.004 to 0.007) | 0.005 (0.004 to 0.007) | 0.018 (0.015 to 0.024) | same | measured |
| 100,000 backspaces | 0.004 (0.004 to 0.006) | 0.011 (0.009 to 0.014) | 0.025 (0.022 to 0.036) | A faster | measured |
| 50,000 random edits (A's bench) | 0.061 (0.050 to 0.064) | 0.071 (0.065 to 0.079) | 0.107 (0.103 to 0.116) | A faster | measured |
| Undo of them (A's bench) | 0.037 (0.035 to 0.047) | 0.049 (0.045 to 0.056) | 0.060 (0.058 to 0.064) | same (ranges touch) | measured |
| 2,000,000 line lookups (A's bench) | 1.647 (1.403 to 1.895) | 1.469 (1.334 to 1.513) | 2.390 (2.312 to 2.407) | same | measured |
| Every line read, 2.3 M reads | 0.381 (0.364 to 0.448) | 0.507 (0.448 to 0.520) | 0.337 (0.326 to 0.422) | A faster (ranges touch) | measured |
| 20 pastes of 1 MB, each deleted | 0.018 (0.014 to 0.023) | 0.009 (0.008 to 0.010) | 0.015 (0.013 to 0.017) | B faster | measured |
| 5 deletes of 50 MB, each put back | 0.277 (0.247 to 0.293) | 0.175 (0.136 to 0.188) | 0.255 (0.232 to 0.264) | B faster | measured |
| 100,000 screens of 4,000 bytes | 0.205 (0.157 to 0.217) | 0.137 (0.123 to 0.158) | 0.257 (0.235 to 0.337) | same (ranges touch), B's median lower | measured |
| Heap after open (mallinfo2) | 107.49 MB | 108.34 MB | not read | A 0.8% smaller | measured, same in 5 runs |
| Peak RSS, A's bench | 316 MB | 317 MB | 325 MB | same | measured, same in 5 runs |

Against the design's targets: open 100 MB takes 0.09 s against 0.3 s; A matches or beats ropey in time on every step except the two bulk ones, where the ranges overlap ropey's, and in memory on all (measured, table above). So the callgrind budget against ropey holds for A, and for B.

## 4. Why A

- **Less code**: 244 lines against 321, 207 against 251 without comments, 14% less machine code.
- **Faster where an editor spends its edits**: typing 20% fewer instructions, backspace 53% fewer, random small edits 36% fewer, lookups 5% fewer, line-by-line reading 13% fewer. In wall time the backspace and random-edit gaps are outside the spread; the others are inside it.
- **B's leads are bulk operations**, 38% fewer instructions for a paste or a large delete and 21% fewer for a multi-leaf read. One 1 MB paste with its delete costs A 0.9 ms and B 0.45 ms; one 50 MB delete with its re-insert costs A 55 ms and B 35 ms (medians above, divided). ropey is where A is on both. These happen once per command, not once per key.
- **Residual risk**: if deleting tens of megabytes at once ever has to fit in a frame, B's range delete is the known remedy (hypothesis that it will not, since ropey, the design's budget, is no faster than A there).

B's proved contract on the newline counter costs nothing to carry over; finding 1 does that.

## 5. Findings on build A

### 1. `nls` has no contract, and its loop bound is written so that the provers cannot finish (requested)

CLAUDE.md: kernels "carry Frama-C contracts that state the result" and proved code is written as comparisons "so the provers finish". Every count in the tree comes from `nls`. With B's contract and loop invariants put on A's `nls` unchanged, Frama-C proves 23 of 26 goals: three time out, because `i + 32 <= n` can wrap (measured, 1 run). With the bound written as `n - i >= 32` it proves 26 of 26 in 4 s; a planted miscount (`i + 1 < n` in the tail loop) then leaves 4 unproved (measured, 1 run each). The proved file is `reviews/r1/text_a_nls_contract.c`. Take the bound and the annotations, and say in MEASURED.md how to run it. Adding the function to `make frama`, with a plant, is thread 0's Makefile.

### 2. The fuzzer's coverage sentence is not what it measures (requested, small)

MEASURED.md says the fuzzer reaches "every line and branch of the tree code, missed only the out-of-range and out-of-memory returns". Measured over 30 s: 4 lines and 12 branches are missed, and 3 of the branches are not error returns: `text_read` never ends inside a leaf or stops short of the text's end (the fuzzer always reads everything into 1 MB), `text_line_start` is never asked past the last line, and `text_lines` is never called. The unit test covers all three. Either add a short read, `r_lines` and a line start past the end to the fuzzer (the reviewer's `xfuzz.c` does it in two lines) or correct the sentence.

### 3. `text_read` goes down from the root once per leaf and adds up newline counts it does not use (no change asked)

That is why B wins the 4,000-byte reads (228.8 M against 181.3 M). A single descent written for A (`rd`, 5 lines more) brings that to 171.4 M and the full read from 3.2 M to 1.6 M, but makes one read per line 7.6% dearer (269.2 M against 250.3 M), and one read per line is what a redraw does (measured, 1 run each). Not better in all scenarios, so A's form stays.

### 4. Not covered by either build

Neither measured Davy's own files; both say so. Neither put ACSL in `include/text.h` (the design's "text.h with ACSL contracts"), which is thread 0's file.

## 6. A finding outside the package

`make planted` hung in this review on thread 0's plant "core: undo loses deleted text (fuzz_keys)". It is not the rope: with that plant `ref/core.c` spins on main too, for 1 of 6 fuzzer seeds, until libFuzzer's default 1,200 s limit for one input (measured, seeds 1 to 6 on main, A and B: seed 1 hangs on all three, the others fail in 0.1 s). A `-timeout` on the fuzz target would turn the hang into a quick failure. Reported to thread 0.

## 7. Re-review of build A, head 056f33b

Findings 1 and 2 are applied. Rerun on that head (measured, 1 run each unless said):

- Frama-C proves 26 of 26 goals for `nls` with the contract in `src/text/text.c`.
- `make compilers`, `make test` for text and text_rope, and 60 s each of `fuzz_text` (2,291,520 runs) and `fuzz_text_rope` (32,295 runs, now with a short read, `text_lines` and a line start past the end): all pass.
- The 7 text plants: 7 of 7 caught, the fuzz plant now inside 30 s.
- `r1/stress.c`, a sixth seed of 3,000 edits: passes.
- Callgrind on A's bench: 36.6 M, 11.5 M, 136.0 M (open, edits, lookups), no more than before (37.0 M, 11.5 M, 137.0 M).

**Approved.** Open for thread 0: `nls` in `make frama` with a plant.
