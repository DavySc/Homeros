# Review of "The change under review" (uni_to16 / uni_from16)

Scratch files and scripts are in `review/w/`. Tools: `tools-env/bin` on PATH (NVIM v0.12.6).
Evidence tag: **measured** = run, with the count.

Behaviour: no difference from Neovim 0.12 found (see "What I ran"). The Frama-C goals of the
four new functions all prove (99 / 99), and the three Frama-C plants fail on the goals they aim at.
The confirmed defects are all minor.

## Confirmed defects

### 1. minor: `uni_from16` runs `step()` twice for every character

`uni_from16` computes `k = step(s, n, j)` and then calls `units(s, n, j)`, whose first line is
again `step(s, n, j)` with the same arguments; neither gcc nor clang at -O2 removes the second
call. Passing the step to `units` (same line count, and it still proves: 98 / 98 goals for
`step,units,uni_to16,uni_from16`, `review/w/fr_alt/`) halves the work. CLAUDE.md top rule: "no
redundant machine instruction".

Reproduce (`review/w/bench.c`: a 4096-byte line of mixed 1- to 4-byte characters, 200,000 calls
with a column past the end; `review/w/alt.c` is the same code with `units(s, n, j, t)`):

    cd review/w && for cc in gcc clang; do $cc -std=c17 -D_DEFAULT_SOURCE -O2 -I../../include -o bench_$cc bench.c ../../src/uni/uni.c alt.c; for r in 1 2 3 4 5; do ./bench_$cc 0; ./bench_$cc 1; done; done

measured, 5 alternating runs per side per compiler:

| | head (as committed) | step passed to units |
|---|---|---|
| gcc -O2 | median 3.226 s (3.127 to 3.423) | median 1.961 s (1.635 to 2.051) |
| clang -O2 | median 1.647 s (1.618 to 1.784) | median 0.844 s (0.803 to 1.008) |

The ranges do not overlap: 1.6 times (gcc) and 2.0 times (clang) slower than needed.

### 2. minor: the new fuzz properties make each `fuzz_uni` execution about 6 times slower

The two added loops are quadratic in the input length and run before the existing cluster and
width properties, so in the fixed `FUZZ_SECONDS` those older properties now get about a sixth of
the executions they had. (The two older fuzz plants are still caught in 60 s, so this is a cost,
not a lost check.)

Reproduce (base and head `tests/fuzz_uni.c`, both linked with head `src/uni/uni.c`, same seeds,
300,000 executions each):

    cd review/w && git -C ../.. show HEAD~1:tests/fuzz_uni.c > fuzz_base.c
    F="clang -std=c17 -D_DEFAULT_SOURCE -I../../include -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all"
    $F -o fz_head ../../tests/fuzz_uni.c ../../src/uni/uni.c; $F -o fz_base fuzz_base.c ../../src/uni/uni.c
    for r in 1 2 3 4 5; do for v in base head; do time ./fz_$v -runs=300000 -seed=$r -max_len=256; done; done

measured, 5 alternating runs per side: base median 39.4 s (33.9 to 39.9); head median 232.3 s
(215.0 to 245.3). In `make fuzz FUZZERS=uni FUZZ_SECONDS=60` head did 124,335 runs.

(Hoisting `m_to16(z, n)` out of the second loop's condition: head median 7.65 s (6.14 to 8.51),
hoisted 6.07 s (4.59 to 7.78) over 60,000 executions, 5 alternating runs: the ranges overlap, no
measurable difference, not claimed.)

### 3. minor: no plant makes either `uni_from16` comparison outside Frama-C fail

CLAUDE.md: "Every check has a plant ... a new check adds one." The change adds four
comparisons outside Frama-C: `uni_to16` and `uni_from16` in `tests/test_uni16_nvim.c`
(lines 86 and 92) and in `tests/fuzz_uni.c` (lines 61 and 62). All three non-Frama-C plants
change `step`/`units`, so each stops at the `uni_to16` comparison, which runs first; the
`uni_from16` comparison against Neovim (line 92) and the one in the fuzzer (line 62) are never
seen to fail by `make planted`. (They can fail: my own mutants reach them. The plants do not
show it.)

Reproduce (the Unicode plants without the Frama-C ones):

    grep -v '~frama~' tests/plants/uni.txt > review/w/noframa.txt
    sed "s#tests/plants/\*.txt#review/w/noframa.txt#" tools/planted.sh > build/planted-uni-noframa.sh && bash build/planted-uni-noframa.sh

    caught  uni: 5- and 6-byte sequences read as invalid bytes (test_uni16_nvim)   (make test TESTS=uni16_nvim): tests/test_uni16_nvim.c:86: CHECK(want == uni_to16(s, n, i)) failed
    caught  uni: U+FFFF counted as two UTF-16 units (test_uni16_nvim)   (make test TESTS=uni16_nvim): tests/test_uni16_nvim.c:86: CHECK(want == uni_to16(s, n, i)) failed
    caught  uni: a sequence that ends the line counts one UTF-16 unit (fuzz_uni)   (make fuzz FUZZERS=uni FUZZ_SECONDS=60): ==16442== ERROR: libFuzzer: deadly signal

and the fuzz plant's abort is at the `uni_to16` line (plant applied by hand, `review/w/fzm_cut.c`):

    #9 ... in LLVMFuzzerTestOneInput .../tests/fuzz_uni.c:61:73

### 4. minor: `tests/test_uni16_nvim.c` includes `<string.h>` and uses nothing from it

A line that is not needed (top rule).

    cd review/w && sed '/#include <string.h>/d' ../../tests/test_uni16_nvim.c > t_nostring.c
    gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I../../include -I../../tests -c -o /dev/null t_nostring.c && echo "compiles without string.h"
    clang -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I../../include -I../../tests -c -o /dev/null t_nostring.c && echo clang-too

    compiles without string.h
    clang-too

## Not confirmed (do not count)

- `uni_from16` can return an offset past `n` (by up to 5) when the line ends in a cut-off
  sequence, and the result is then not monotonic in `u` (line `FC`: column 1 gives 6, column 2
  gives 1). This is a hazard for a caller that indexes with the result, but it is what Neovim
  0.12.6 returns (checked: `fc` and `f09f98` give identical output from both), the task asks for
  Neovim's result, and the header says so. Not a defect of this change.
- Neither the Neovim test nor the fuzzer runs an empty line (the generator makes at least one
  byte; the fuzzer returns on `n == 0`). My own comparison included the empty line and it agrees;
  Frama-C proves the contract for `n == 0`. I found no mutant that breaks only that case.
- The fuzz property overlaps the other two checks: its plant (`t >= n - j`) is also caught by
  `test_uni16_nvim` (`review/w/mut.sh cut`: fails at line 86) and the proof covers the same code.
  The task allowed fuzz properties "if that helps", so I do not count it as unneeded code.
- `make frama` on the whole file reported 325 / 326 here, the one timeout being
  `typed_uni_glen_loop_invariant_3_preserved` (base code). Run side by side under the same load
  with the cache off, base and head both give 79 / 80 for `uni_glen` with that one goal timing
  out (load average about 6.6 on 4 cores), so it is the known load timeout and not caused by the
  change. I did not get a quiet machine to see it prove.
- The `n < 0x80000000` precondition: removed to see whether it is needed; without it 3 goals of
  `uni_to16` and 4 of `uni_from16` time out, so it is needed.

## What I ran (counts)

- `make compilers`: 1 run, passes (gcc and clang).
- `make test TESTS="uni uni_nvim uni16_nvim"`: 1 run, passes; `test_uni16_nvim` 20,000 lines,
  582,383 calls. `SEED=1..6 build/san/test_uni16_nvim`: 6 runs, all pass (about 585,000 calls each).
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 124,335 executions, no failure.
- My own comparison against `nvim --clean --headless` (`review/w/drv.c`, `o.lua`, `gen.py`):
  the empty line, every string of length 1 to 4 over 19 chosen bytes (137,560 lines), and 60,000
  random lines of 0 to 40 bytes (uniform bytes; the 19-byte alphabet; lead and continuation
  bytes only), each at every byte offset and at every column to 3 past the end plus 2^40:
  2 seeds, 197,561 lines each, output identical both times (driver built with ASan and UBSan).
- Frama-C on `step,units,uni_to16,uni_from16`, cache off: unchanged 99 / 99; each of the three
  Frama-C plants applied alone: 98 / 99, 100 / 101, 98 / 99, the failing goal being
  `uni_to16_loop_invariant_2_preserved`, `uni_from16_ensures`, `uni_from16_ensures`: the plants
  fail for their own reason, not through the `uni_glen` timeout. `make frama` whole: 1 run
  (325 / 326, see above); `uni_glen` alone on base and on head: 1 run each.
- `tools/planted.sh` on the nine non-Frama-C Unicode plants (six old, three new): 9 of 9 caught.
- 13 mutants of `src/uni/uni.c` against `test_uni16_nvim` (`review/w/mut.sh`): 12 caught, 1
  equivalent (`0xFCu` to `0xF8u` for the 6-byte lead: the difference is a multiple of 2^32 in
  `unsigned`). 3 mutants against the fuzzer (two that change only `uni_from16`, and the fuzz
  plant): all caught within 60 s, the `uni_from16` ones at `fuzz_uni.c:62`.
- Timings: defect 1 (20 runs), defect 2 (10 runs of 300,000 executions, 10 of 60,000).

DEFECTS major=0 minor=4
