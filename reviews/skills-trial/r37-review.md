# Review of "The change under review" (uni_u16, uni_u16byte)

Verdict: the two kernels give Neovim 0.12's answers on everything I ran, every new goal proves, and every plant is caught. No major defect confirmed. Three minor defects.

All paths below are relative to /home/claude/trial/rev/r37; `w/` under `review/` is an unchanged copy of HEAD's Makefile, include, ref, src, tests, tools.

## Confirmed defects

### 1. minor: needless cost — every character's continuation bytes are scanned twice

`uni_u16byte` calls `seq(s, j, n)` and then `pair(s, n, j)`, whose first line is the same `seq(s, j, n)`. gcc -O2 does not merge them (clang does only partly). CLAUDE.md top rule: "no redundant machine instruction". Passing `k` to `pair` (with `requires k == nstep(s, j, n)`) removes the second scan and still proves.

Reproduce (the variant is `review/v/uni_var.c`: `pair` takes `k`; bench is `review/bench.c`, 1 MiB line walked 200 times, `uni_u16byte(s, n, SIZE_MAX)`):

```
cd review
gcc -std=c17 -O2 -Iw/include -S -o uni.s w/src/uni/uni.c; grep -n "call	seq\|call	pair" uni.s
  76:  call seq      (inside pair)
  823: call pair  828: call seq      (uni_u16)
  909: call seq   914: call pair     (uni_u16byte: seq, then pair calls seq again)
for c in gcc clang; do for x in orig var; do $c -std=c17 -D_DEFAULT_SOURCE -O2 -Iw/include -Iw/src/uni -o bench_${c}_$x bench.c v/uni_$x.c; done; done
./bench_gcc_orig 0; ./bench_gcc_var 0; ...   (5 alternating runs per side)
```

measured, 5 alternating runs per side, uni_u16byte, ms (median, range), machine under load:

| compiler, line | as committed | seq once |
|---|---|---|
| gcc, ASCII | 682 (574-883) | 385 (306-475) |
| gcc, mixed 1-4 byte | 1480 (1436-1729) | 1113 (969-1180) |
| clang, ASCII | 317 (288-385) | 207 (174-321): ranges overlap, no measurable difference |
| clang, mixed | 1125 (1021-1170) | 941 (892-1030): ranges overlap by 9 ms |

The variant proves (proved): in `review/w2`, `FRAMAC_WP_CACHE=rebuild frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -cpp-extra-args="-Iinclude" -wp-fct seq,pair,uni_u16,uni_u16byte src/uni/uni.c` gives `Proved goals: 110 / 110`.

(`uni_u16` also scans twice, once to n in `pair` and once to i in `seq`; I did not build a proved single-scan variant of that one, so only `uni_u16byte` is claimed.)

### 2. minor: a plant's name is false — "a column inside a surrogate pair rounds down" does not round down

The plant `if (u >= c)` -> `if (u > c)` leaves a column inside a pair rounding up exactly as before; what it breaks is every column that is exactly reached (it returns one character too far). So no plant in the file changes the inside-a-pair rounding that the task names, although the name says one does.

Reproduce (`review/rd.c` prints `uni_u16byte("\xf0\x9f\x98\x80ab", 6, c)`; `review/m/uni_p_round.c` is uni.c with the plant):

```
cd review
gcc -Iw/include -Iw/src/uni -o m/rd rd.c w/src/uni/uni.c && ./m/rd
  c=0 -> 0   c=1 -> 4   c=2 -> 4   c=3 -> 5   c=4 -> 6
gcc -Iw/include -Iw/src/uni -o m/rd rd.c m/uni_p_round.c && ./m/rd
  c=0 -> 0   c=1 -> 4   c=2 -> 5   c=3 -> 6   c=4 -> 6
```

c=1 (inside the pair) is 4 both times.

### 3. minor: the new fuzz property on uni_u16byte has no plant that reaches it

fuzz_uni.c gained two checks: line 59 (`uni_u16` against `utflen`) and line 61 (`uni_u16byte` against `index2bytes`). The one new fuzz plant ("an overlong 4-byte form read as a surrogate pair") changes `pair`, so it always dies at line 59 and line 61 is never seen to fail by any plant. CLAUDE.md: "Every check has a plant ... a new check adds one."

Reproduce (`review/m/uni_p_fuzz.c` is uni.c with that plant):

```
cd review
for seed in 1 2 3 4 5; do clang -std=c17 -D_DEFAULT_SOURCE -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -Iw/include -Iw/src/uni -o m/fzp w/tests/fuzz_uni.c m/uni_p_fuzz.c && ./m/fzp -seed=$seed -max_total_time=60 -max_len=256 -artifact_prefix=m/ 2>&1 | grep -m1 "in LLVMFuzzerTestOneInput"; done
  #9 ... in LLVMFuzzerTestOneInput .../tests/fuzz_uni.c:59:54     (5 of 5 seeds)
```

The check itself can fail (my mutants "drop `if (c == 0) return 0;`" and "`return n` -> `return j`", which touch only uni_u16byte, were caught by the fuzzer in 20 s), so this is a missing plant, not a dead check: hence minor.

## Unconfirmed, or confirmed but not counted as defects

- The fuzzer's throughput halves (measured, 3 alternating 15 s runs, same uni.c: base harness 142326 / 130633 / 133982 runs, new harness 60746 / 61538 / 67957), because both new properties are quadratic in the line (every offset, every column, each a walk from 0). The two older fuzz plants are still caught at 60 s, so I do not count it; `make check` runs the fuzzer for 30 s by default, which I did not test with those plants.
- Four boundary mutants of `pair` (6-byte `s[j+3] >= 0xA0`, 6-byte without `s[j+2] > 0x80`, 6-byte lead `>= 0xFC`, 4-byte `>= 0x91`) survive 20 s of fuzz_uni under load. test_uni_u16 and Frama-C catch all of them, and the fuzzer does not claim them, so not a defect.
- `make frama` on the change gave `Proved goals: 337 / 338`: the one timeout is `typed_uni_glen_loop_invariant_3_preserved` (base code, the known one, load average 5-9 throughout). I did not get a quiet machine to retry it, so I cannot say whether the header's added logic makes that goal harder. The new functions' 111 goals prove with the cache rebuilt.
- `requires n < 0x80000000` looked arbitrary; without it 7 goals time out, so some bound is needed. Not a defect.
- New comments carry no proved/measured/hypothesis tags; neither do the base's, so not counted.

## What I ran, with counts

- `make compilers`: pass (1 run). `make test TESTS="uni uni_nvim uni_u16"`: pass (1 run; test_uni_u16 alone 2 more times, 2.7 s): "111150 lines of 1-4 bytes exhaustive over 18 byte classes, 20000 random lines, 3196655 answers equal Neovim's".
- `make frama`: 337 / 338, see above (1 run). New functions only, `FRAMAC_WP_CACHE=rebuild`, `-wp-fct seq,pair,uni_u16,uni_u16byte`: 111 / 111 (1 run).
- My own differential against live nvim 0.12.6 (`review/my.lines`, `my.lua`, `drv.c`; ASan+UBSan): 40,000 lines with a different generator (random bytes, a pool of edge bytes, forms of 1-6 bytes with any lead, lengths 0-64 and 800 lines of 300 bytes), every offset 0..n+1 and column 0..2n+1: 2,646,166 answers, 0 differ (1 run).
- The test's claims about its random lines, counted with `review/stats.c` (1 run): 1554 empty lines, 1923 forms cut by the line's end, 44 surrogate forms, 10829 overlong, 3079 U+FFFF, 3117 U+10000, 5-byte forms 3040 at or below U+FFFF and 4398 above, 6-byte 2711 and 4518. The header comment's claims hold.
- 15 mutants of the new code (`review/mut.py`): test_uni_u16 catches 15 of 15; fuzz_uni in 20 s catches 11 of 15 (the four above survive).
- Plants: 13 and 14 with Frama-C on the new functions (1 run each): each leaves one goal unproved, in `pair` (`typed_pair_ensures`) and in `uni_u16byte` (`typed_uni_u16byte_loop_invariant_2_preserved`), so they fail for their own reason and not for the uni_glen timeout. 11, 12, 15, 16, 17, 18 through `tools/planted.sh` on a six-line plant file: all six caught (`review/planted-sel.log`). The full Unicode plant run (`review/planted.log`) had reached plant 5 of 18 (plants 1-4 caught) when I stopped it: its Frama-C plants take about 6 min each under this load. Plants 5-10 (the base's, on code the change does not touch) were therefore not run.
- LSP call sites in Neovim 0.12.6's runtime read (`lua/vim/lsp/*.lua`): they pass `false` for strict indexing, as the test does.
- Bench: 5 alternating runs per side per compiler per line kind (40 runs). Fuzz throughput: 3 alternating 15 s runs per side.
