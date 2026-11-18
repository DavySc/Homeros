# Review of "The change under review" (uni_width, package 3)

Scratch and repro files: `review/x/` (paths below are relative to the repo root; `export PATH=$PWD/tools-env/bin:$PATH` first).

## Confirmed defects

### 1. major: uni_width disagrees with Neovim 0.12's utf_ptr2cells() on 5- and 6-byte forms, and the new Neovim checks cannot see it

For a lead byte 0xF8..0xFD followed by its 4 or 5 continuation bytes Neovim reads one character (utf_ptr2len 5 or 6) and utf_ptr2cells() returns 1; uni_width returns 4 for the lead (and 4 for each continuation byte), so the line is drawn 20 cells wide where Neovim draws 1. The cause is in the fixed header (want() is 0 from 0xF8, so the contract demands 4), so the author could not fix it in uni.c, but the change does not report the conflict, and its tests assert the opposite: test_uni_nvim.c's comment says part 2 checks uni_width "against utf_ptr2cells (invalid and truncated bytes, what follows)", and fuzz_uni.c enshrines "4 on an invalid byte". The 20,000 generated lines hold no such form, so the check cannot fail on it.

Repro (measured, 1 run each):

```
$ cd review/x && nvim --clean --headless -l f8.lua
f8 88 80 80 80  cells 1 char 200000 len 5 clen 5 strdisplaywidth 1
fc 84 80 80 80 80  cells 1 char 4000000 len 6 clen 6 strdisplaywidth 1
f8 80 80 81 81  cells 1 char 41 len 5 clen 5 strdisplaywidth 1
$ gcc -I../../include f8.c ../../src/uni/uni.c -o f8 && ./f8      # s = f8 88 80 80 80
uni_width 4 uni_glen 1
$ nvim --clean --headless -l r.lua $PWD/r && ./r      # 300,000 random byte lines, width at every byte
1650017 positions 112 differ
Counter({'f8': 32, 'fa': 23, 'fb': 21, 'f9': 16, 'fc': 11, 'fd': 9})    # byte at each differing position
$ python3 -c "..." build/uni_nvim.lines     # the author's 20,000 lines
20001 lines; 5/6-byte forms: 0
```

(The same forms are 5 clusters for uni_glen and 1 for utfc_ptr2len: that is the base's, not counted here.)

### 2. major: two of the four new fuzz properties have no plant that they catch; one of them misses a planted bug in exactly what it claims

tests/fuzz_uni.c adds four aborts. Line 35 (ASCII) and line 36 (invalid byte) each catch a plant. Line 34 (result is 1, 2, 4 or 6) and line 37 ("depends on what follows only through a U+FE0F") are never the line that fires: the third fuzz plant ("look-ahead reads past the line") is caught by ASan inside uni_width at line 31, not by a property. With lines 34 and 37 deleted every fuzz plant is still caught, so nothing shows these two checks can fail (CLAUDE.md: "Every check has a plant ... a new check adds one"). Line 37 is also weak in practice: a bug that widens on U+FE0E instead of U+FE0F survives 3 runs of 60 s, though the 5-byte input `c2 a9 ef b8 8e` trips it.

Repro (measured; each fuzz run 60 s):

```
$ cd review/x      # fz/<plant>.c = src/uni/uni.c with the plant from tests/plants/uni.txt
ascii rc=77  ... LLVMFuzzerTestOneInput tests/fuzz_uni.c:35:30
inval rc=77  ... LLVMFuzzerTestOneInput tests/fuzz_uni.c:36:41
past  rc=1   ERROR: AddressSanitizer: heap-buffer-overflow ... tests/fuzz_uni.c:31:11
$ sed -e '/if (w != 1 && w != 2 && w != 4 && w != 6) abort();/d' -e '/if (w != alone/d' ../../tests/fuzz_uni.c > fz/fuzz_cut.c
cut fuzzer, plant ascii: rc=77
cut fuzzer, plant inval: rc=77
cut fuzzer, plant past: rc=1
$ sed -i 's/== 0xFE0F) return 2/== 0xFE0E) return 2/' fz/fe0e.c     # full fuzzer, 3 runs
FE0E plant rc=0   Done 268056 runs in 61 second(s)
FE0E plant run 2 rc=0   Done 252682 runs in 61 second(s)
FE0E plant run 3 rc=0   Done 230918 runs in 61 second(s)
$ printf '\xc2\xa9\xef\xb8\x8e' > fz/seed && ./fz/fe0e fz/seed
ERROR: libFuzzer: deadly signal ... tests/fuzz_uni.c:37:92
```

## Unconfirmed / not counted

- `make frama` on the unchanged tree gave 226 / 227: the one timeout is `typed_uni_glen_loop_invariant_3_preserved` (base code, the goal the brief names). A retry with `FRAMAC_WP_CACHE=rebuild -wp-fct uni_glen` timed out again at load average 8 to 12; I never had a quiet machine. All 31 uni_width goals prove (`-wp-fct uni_width`, 14 s), so I do not count it.
- The plant comment "every RI is 2 cells in Unicode 18, so no Neovim test sees it" carries no proved/measured tag. It is true (measured from tab.h: 26 regional indicators, 0 one-cell; 110 one-cell Extended_Pictographic), and the base's comments are untagged the same way, so not counted.
- The same mutation (`if (k == 1) return 1;`) is planted three times and the read-past one twice, each a separate full run (a Frama-C plant took about 5 min here). Each aims at a different check, which the rule asks for, so not counted.
- The full `planted-uni.sh` run was stopped by my 30 min limit after 5 base plants (all caught); I ran the nine new plants singly instead (below).

## What I ran

- `make test TESTS="uni uni_nvim"`: 1 run, passes (2,097,152 code points, 20,000 lines, 853 GraphemeBreakTest cases).
- `make compilers`: 1 run, passes.
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 249,110 runs, no failure.
- `make frama`: 1 run, 226 / 227 (see above); `-wp-fct uni_width`: 31 / 31.
- New plants: the 3 Frama-C ones singly with `-wp-fct uni_width` (each 30 / 31, failing in the goal aimed at: ensures_2, call_uni_len_2_requires, ensures_3); the 3 test_uni_nvim ones through planted.sh (all caught, at lines 175, 175, 192); the 3 fuzz ones by hand (all caught, see finding 2). 9 of 9 caught.
- My own Neovim comparison, `review/x/d.lua` + `d.c`: every code point 1..0x1FFFFF in every encoding length (shortest and overlong) with 11 followers (none, U+FE0F, overlong U+FE0F, ASCII, truncated U+FE0F, U+FE0E, U+0E0F, two U+FE0F, ZWJ, lone lead, lone continuation): 23,813,460 cases, 0 differ. So apart from finding 1 the implementation matches utf_ptr2cells(), including the comment's claim about an overlong U+FE0F.
- `review/x/r.lua` + `r.c`: 300,000 random byte lines, 1,650,017 positions, 112 differ, all finding 1.
