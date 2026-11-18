# Review of "The change under review" (uni_gprev), r13

Behaviour, contract and proof hold: no wrong result found. Two minor defects, both confirmed by running.

## Confirmed defects

### 1. minor: `//@ assert bound(s, n, j);` in uni_gprev is not needed (top rule: no line that is not necessary)

The second loop's invariant already states `bound(s, n, j)`, so its establishment goal is the same fact. Without the assert every goal of uni_gprev still proves (3 runs of 3, cache off), and the Frama-C plant is still caught.

    cp -r include src review/exp2/ ; sed -i '/assert bound(s, n, j);/d' review/exp2/src/uni/uni.c
    cd review/exp2 && FRAMAC_WP_CACHE=none frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 \
      -wp-fct uni_gprev -cpp-extra-args=-Iinclude src/uni/uni.c
    [wp] Proved goals:   24 / 24          (3 runs, same result)

    # the frama plant (s[j] < 0xC0) on top of that, review/exp3:
    [wp] [Timeout] typed_uni_gprev_loop_invariant_2_established (Qed 33ms) (Z3)
    [wp] Proved goals:   24 / 25

### 2. minor: the new check at tests/test_uni_nvim.c:132 (GraphemeBreakTest, back) has no plant that it catches (CLAUDE.md: "Every check has a plant ... a new check adds one")

The change adds three checks (test_uni_nvim.c:199, :132, fuzz_uni.c:27). Both `test TESTS=uni_nvim` plants abort at line 199, which runs first, so line 132 was never seen to fail by the plant run. (It can fail: with line 199 deleted, both plants stop at 132; so the check works, the evidence for it is what is missing.)

    grep "^uni: uni_gprev" tests/plants/uni.txt > review/gprev-plants.txt
    sed 's#tests/plants/\*.txt#review/gprev-plants.txt#' tools/planted.sh > build/planted-gprev.sh && bash build/planted-gprev.sh
    caught  uni: uni_gprev steps back to a code point start, as utf_head_off (make test TESTS=uni_nvim): tests/test_uni_nvim.c:199: CHECK(uni_gprev(s, n, b) == i) failed
    caught  uni: uni_gprev returns the cluster before when i - 1 starts one (make test TESTS=uni_nvim): tests/test_uni_nvim.c:199: CHECK(uni_gprev(s, n, b) == i) failed
    caught  uni: uni_gprev replays from byte 1 when byte 0 is not ASCII (fuzz_uni) (make fuzz ...): ERROR: libFuzzer: deadly signal

    # line 199 deleted (review/pA, review/pB), each plant: 
    tests/test_uni_nvim.c:132: CHECK(uni_gprev(s, n, i) == b) failed

## Not counted (unconfirmed, or not a defect on the evidence)

- The assert added to uni_glen (base code, outside the function asked for). It adds the goal `typed_uni_glen_assert`, which timed out in my first `make frama` (228 / 229, a cached timeout from the author's .frama-c) and proved with `FRAMAC_WP_CACHE=rebuild` (229 / 229). Without the assert the base goal `typed_uni_glen_loop_invariant_3_preserved` timed out in 2 runs of 2 (load 10 to 12); with it uni_glen proved in 2 alternating runs of 2 under the same load (third pair stopped). So it appears to help the known flaky goal; I could not get a quiet machine to show it is unneeded. (hypothesis)
- Cost: uni_gprev rescans to the last ASCII byte, so on a line without ASCII one step is O(i) and walking a line back is quadratic. Measured (review/t/edge.c, -O2, loaded machine, 3 walks each): CJK line of 3,000 / 12,000 / 48,000 bytes: 10.6 / 161 / 2396 ms per whole-line walk back, against 0.1 / 0.4 / 1.8 ms with an ASCII byte every 30 bytes. The comment admits it and the fixed contract's `bound` (0, ASCII, steps) invites it; I did not build a cheaper version that proves, so it is not counted.
- When s[i - 1] is ASCII the result is i - 1 without the uni_glen call that is still made; not measured.

## What I ran

- `make compilers`: 1 run, pass (gcc, clang).
- `make test TESTS="uni uni_nvim"`: 1 run, pass (20,000 lines, 853 GraphemeBreakTest cases).
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 299,574 runs, pass.
- `make frama`: 2 runs: 228 / 229 (cached timeout), then 229 / 229 with FRAMAC_WP_CACHE=rebuild.
- Frama-C on uni_gprev alone: 3 runs without the bound assert (24 / 24 each); 2 runs with the Frama-C plant applied by hand (caught both, with and without the assert). The Frama-C plant was not run through planted.sh (6 min per run under load).
- Frama-C on uni_glen alone, alternating with / without the added assert: see above (the alternation was cut short by the machine's load).
- The three non-Frama uni_gprev plants through planted.sh: 1 run, all caught; the two test plants again with line 199 removed: 1 run each.
- review/t/edge.c: uni_gprev against a replay from 0 on 2,000,000 random lines (23,193,151 calls), plain and under ASan+UBSan: 0 differ.
