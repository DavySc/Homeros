# Review of "The change under review" (uni_gprev, package 3)

Verdict: the implementation is correct on everything I ran and its three plants are caught for the stated reason. No major defect. Two minor ones.

## Confirmed defects

### 1. minor: the fuzzer's header comment was edited without re-wrapping
`tests/fuzz_uni.c` line 4 is now 138 columns; the longest line of the file at base was 76. The inserted clause was pasted in front of the old text of the line.

```
$ awk 'length($0) > 100 {print FILENAME": line "FNR": "length($0)" columns"}' tests/fuzz_uni.c
tests/fuzz_uni.c: line 4: 138 columns
$ git show HEAD~1:tests/fuzz_uni.c | awk '{ if (length($0) > m) m = length($0) } END { print m }'
76
```

### 2. minor: `0 <=` in the first loop invariant is not needed (top rule: nothing that is not necessary)
`b` is a `size_t`, so `loop invariant 0 <= b <= i - 1` states nothing in its first half. With `loop invariant b <= i - 1` every uni_gprev goal still proves (the other half, and `b < i` in the second invariant, are needed: dropping `b < i` loses 3 goals).

```
$ sed 's|/\*@ loop invariant 0 <= b <= i - 1;|/*@ loop invariant b <= i - 1;|' ../src/uni/uni.c > v/v1.c
$ frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct uni_gprev -cpp-extra-args="-I../include -I../src/uni" v/v1.c
[wp] Proved goals:   22 / 22
(unchanged file: 22 / 22; without `&& b < i`: 19 / 22)
```

## Unconfirmed, or not counted

- **Cost on long non-ASCII lines (measured, not counted as a defect of the change).** One step back costs the whole non-ASCII run before i, so walking left over a line without ASCII is quadratic. `review/perf.c`, gcc -O2, 5 alternating runs: a 30,000-byte CJK line, one step back from the end, median 179 us (range 173 to 238); walking left over the line (10,000 steps) median 1.10 s (range 1.02 to 1.25); the same sizes in ASCII: under 0.1 us and under 1 ms. I do not count it because the fixed contract's `bound` has only the cases zero, ascii and step, so a proof can start nowhere later than the last ASCII byte (starting at i - 1 without the back-walk fails `loop_invariant_established`, 13 / 14), and the code comment states the cost. It is a property of the header's design that thread 0 should know before a caller loops over uni_gprev.
- **The code comment's list "(regional indicator pairs, GB9c conjuncts, lam-alef)".** It says a backward reading of the rules would miss uni_glen's starts in these three cases. Neovim's own backward reading (`utf_head_off`, through the FFI, `review/head.lua`) lands on the forward starts for 3 and 4 regional indicators and for lam-alef, and misses only the conjunct (ka virama ssa: heads 0 0 0 0 0 0 6 6 6 against the one start 0). Over the test's 20,000 lines (`review/head2.lua`) it leaves the forward starts on 645 lines, none of them at a regional indicator or an alef. The comment speaks of "a" backward reading, not of Neovim's, so this is a wording doubt, not a false claim I could show.
- **The Frama-C plant can be reported caught for the wrong reason under load.** `planted.sh` accepts any "Proved goals" line, and the known uni_glen timeout (base code) alone makes `make frama` fail. In my run the plant gave 223 / 225 against 224 / 225 for the clean tree, so here it was caught by a uni_gprev goal. Base behaviour of the tool, not of the change.

## What I ran

- `make compilers`: gcc and clang build, 1 run, clean.
- `make test TESTS="uni uni_nvim"`: 1 run, passes (Neovim 0.12.6).
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 285,597 inputs, no failure.
- `make frama`: 1 run, 224 / 225; the one timeout is `typed_uni_glen_loop_invariant_3_preserved` (base code, the known one). `-wp-fct uni_gprev` alone: 22 / 22.
- The three new plants through `tools/planted.sh` (plant list cut to the uni_gprev lines), 1 run: all caught. Frama-C 223 / 225; test_uni_nvim at `tests/test_uni_nvim.c:196: CHECK(uni_gprev(s, n, j) == b)`; fuzz_uni "deadly signal". The full uni plant list was started once and was stopped by the time limit of my shell before it printed anything, so the nine older plants were not re-run.
- Frama-C variants of uni_gprev, 5 (1 run each): unchanged 22 / 22; without `0 <=` 22 / 22; without `b < i` 19 / 22; without the back-walk 13 / 14; walk from 0 instead 14 / 14.
- `review/ex.c` under ASan and UBSan on exact-size heap copies: every string of 1 to 5 tokens from 18 (ASCII, combining mark, regional indicator, ka, virama, ssa, lam, alef, ZWJ, emoji, stray continuation byte, truncated sequence, Prepend, Hangul L and V, VS16, U+0085, skin tone): 2,000,718 strings, 25,813,199 uni_gprev calls, all equal to the tiling by uni_glen.
- `review/perf.c`: 5 alternating runs per side (numbers above).
- `review/head.lua` (6 fixed cases) and `review/head2.lua` (20,000 lines) against Neovim's `utf_head_off`, 1 run each.

DEFECTS major=0 minor=2
