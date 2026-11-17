# Review of "The change under review" (uni_width), r35

Two confirmed defects: 1 major, 1 minor. Everything else I ran passed.

## Confirmed defects

### 1. major: uni_width disagrees with Neovim 0.12's utf_ptr2cells() on 5- and 6-byte sequences, and the Neovim test cannot see it

Neovim reads a lead byte F8..FD with all its continuation bytes as one character.
uni_width (through `uni_len`/`dec`, i.e. the header's `wellformed`/`cpx`) reads the lead as an invalid byte. Two wrong results follow:

- the sequence itself: Neovim 1 cell, uni_width 4;
- a one-cell emoji followed by U+FE0F written overlong in 5 or 6 bytes: Neovim 2 cells, uni_width 1.

The change shows the author considered this class: it adds the 4-byte overlong U+FE0F to the test's `odd[]` with the comment
"which Neovim reads as U+FE0F". The 5- and 6-byte forms are not there, and the random lines draw bytes F8..FF only as single stray bytes,
so the claim "widths there against utf_ptr2cells" is unchecked for this input and `make test TESTS=uni_nvim` passes.

Caveat: the header contract (which the task fixes) demands exactly what the code returns, so the code cannot be made right without thread 0
changing `include/uni.h`; base `uni_glen` has the same divergence (Neovim's cluster is 5 or 6 bytes, ours 1), which is not counted here.
What the change owed was the test input and a report that the contract and Neovim disagree.

Reproduce (from the repo root, tools on PATH): `sh review/repro1.sh`

```
f888808080 nvim utf_ptr2cells 1 strdisplaywidth 1	uni_width 4
fc8480808080 nvim utf_ptr2cells 1 strdisplaywidth 1	uni_width 4
e29da4f8808fb88f nvim utf_ptr2cells 2 strdisplaywidth 2	uni_width 1
e29da4fc80808fb88f nvim utf_ptr2cells 2 strdisplaywidth 2	uni_width 1
c2a9f8808fb88f nvim utf_ptr2cells 2 strdisplaywidth 2	uni_width 1
```

Extent (measured, 1 run each):
- `review/gen.lua` + `review/diff`: 38,420 strings (every lead C0..FF with 0 to 5 random continuation bytes, alone and after four prefixes): 206 widths differ, all with an F8..FD lead.
- `review/gen2.lua` + `review/diff2`: 300,000 random byte lines, 1,404,960 non-ASCII positions at Neovim's own cluster starts: 572 differ, all 572 with an F8..FD lead at the position or right after its code point; 0 others.

### 2. minor: the second assertion added to uni_glen is not needed, and its comment credits it with the fix the first one makes

The change adds two assertions to base `uni_glen`. The comment ("a step for the provers: without it CVC5 timed out on the invariant ... measured: 4 of 4 runs")
sits on `//@ assert glue != 0 <==> glue(l, t);`. Measured here, the uncommented first assertion (`t == gcb(c) && it == incb(c)`) alone proves every goal,
and faster; the commented one alone still timed out once. So 3 lines and about 30 s of every `make frama` are not needed, and the comment's claim is about the wrong line.
The comment's own measurement is 4 runs, under the 5 the rules ask for.

Reproduce: four copies of uni.c in `review/sc/{both,first,second,none}`, then in each
`frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-cache none -wp-fct uni_glen -cpp-extra-args="-Iinclude" src/uni/uni.c`, alternating (log: `review/asserts.log`; machine load 2.5 to 9.8).

| variant | runs | all goals proved | wall time, median (range) |
|---|---|---|---|
| first assertion only | 5 | 5 of 5 | 123 s (120 to 159) |
| second (commented) only | 5 | 4 of 5 (1 timeout, loop_invariant_3_preserved) | 157 s (154 to 233) |
| both (the change) | 3 | 3 of 3 | 171 s (135 to 181) |
| none (base) | 3 | 1 of 3 (2 timeouts, same goal) | 139 to 155 s |

The base timeout is real, so adding a proof step is justified; only the second one is not.

## Unconfirmed

None that I would stand behind. Looked at and found sound: the plant note "no Unicode 18 data needs it" (26 regional indicators in tab.h, 0 with cellc 0, `review/ri`);
the ASCII early return (33 ASCII code points have a non-zero cells code, so it is needed).

## What I ran

- `make compilers`: 1 run, clean.
- `make test TESTS="uni uni_nvim"`: 1 run, pass (2,097,152 code points, 20,000 lines, 853 GraphemeBreakTest cases).
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 479,266 runs, no finding.
- `make frama`: 1 run, 225 / 225 proved (load about 10).
- Unicode plants: all 22 caught, 1 run each. The first run was stopped at the 30 min background limit after 5 plants; the rest ran in two further batches (`review/planted-part1.log`, `planted-new.log`, `planted-rest.log`). For the 8 new plants I read the failure: the two Frama-C ones fail on `uni_width_ensures_2` and `_ensures_3` (not on a load timeout elsewhere), the three test ones on test_uni_nvim.c:177, the fuzz ones on abort (2) and an ASan heap-buffer-overflow in uni_len (1).
- Neovim comparisons: the two differential runs under defect 1, plus `review/probe.lua` and `review/disp.lua` (12 and 6 strings).
- Frama-C assertion variants: 16 runs (table above; one of the three "none" runs was run by hand before the loop and is not in asserts.log).
