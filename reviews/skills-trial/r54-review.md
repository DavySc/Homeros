# Review of "The change under review" (src/uni/gen.py, src/uni/tab.h, uni.c, plants)

Result: 0 major, 1 minor. The tables are right against Neovim on every code
point, are the smallest the fixed two-level formula allows, the bounds proof
is real, and every Unicode plant is caught.

## Confirmed defects

### 1. minor: gen.py's Mn rule has no comment, though the docstring says every non-Unicode rule has one

`src/uni/gen.py` line 86, `if c not in gc or gc[c] == 'Mn': return 0`, makes a
nonspacing mark one cell even when its East Asian Width is W (U+3099, U+302A,
U+16FE4). That is Neovim's/utf8proc's rule, not Unicode's, and it is needed,
but no comment names it: the comment above `main`'s first loop covers only
unassigned code points. The docstring's claim "Where the rules below are
Neovim's or utf8proc's rather than Unicode's, the comment says so" is false
for this rule, and CLAUDE.md asks that a rule copied from Neovim name its source.

    $ grep -n "Mn" src/uni/gen.py
    86:        if c not in gc or gc[c] == 'Mn':
    $ nvim --clean --headless -l review/mn.lua      # utf_char2cells, utf8proc via FFI
    U+3099 utf_char2cells=1 utf8proc category=6 charwidth=0
    U+302A utf_char2cells=1 utf8proc category=6 charwidth=0
    U+309B utf_char2cells=2 utf8proc category=21 charwidth=2
    U+16FE4 utf_char2cells=1 utf8proc category=6 charwidth=0
    (tab.h: U+3099, U+302A, U+16FE4 cellc=0; U+309B cellc=1)

## Unconfirmed (do not count)

- That the committed tab.h is the output of the committed gen.py on the real
  Unicode 18.0.0 files. I could not get the UCD: unicode.org is refused by the
  network policy (CONNECT 403), GitHub is excluded by the brief, and an attempt
  to rebuild the files from npm/PyPI packages was refused by the permission
  system, so gen.py was never run. Partial evidence only: tab.h's layout matches
  gen.py's emitter (rows in first-use order, 24 entries per uni_t1 line, the
  "32896 bytes" in the header equals 8704 + 189 * 128), and its data equals
  Neovim's on all 2^21 code points. Nothing in `make check` regenerates tab.h,
  so a stale tab.h after a gen.py edit would go unnoticed.
- For the same reason I could not test by mutation whether every clause of
  gen.py is needed (`w != 'A'`, the `c in gc` guards, each NONPRINT range).
  By reading, each has code points that depend on it.

## What I ran (counts)

- `make compilers`: 1 run, gcc and clang pass.
- `make test TESTS="uni uni_nvim"`: 1 run, pass (2,097,152 code points
  exhaustive, 20,000 lines, GraphemeBreakTest 853 of 853; replay 1014 sequences 0 differ).
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 451,321 runs, pass.
- `make frama` on the change: 3 runs, 228 / 228 each (about 3 min each under load 7-8).
- Is the added `//@ assert t == gcb(c) && it == incb(c);` needed: removed it,
  3 runs alternating with the 3 above: 226 / 227 each time
  (uni_glen_loop_invariant_3_preserved times out). Needed: not a defect.
- Is "proved from tab.h's initializers" true: set uni_t1's last entry to 189
  (one past uni_t2), `frama-c -wp -wp-rte -wp-fct prop`: 7 / 8
  (prop_assert_rte_index_bound_2 fails); unchanged: 8 / 8. 1 run each. True.
- Exact 189 rows against a 256-row declaration: `make frama` 228 / 228 in
  3m15 against 3m05-3m19: no measurable proof cost (1 run).
- Table size (review/sizes.py, review/sizes2.py, on the data in tab.h): every
  power of two B from 4 to 4096, with 1- or 2-byte uni_t1, and every divisor
  of 0x110000 from 48 to 400: B = 128 with 189 rows (32,896 bytes) is the
  smallest; next 36,208 (B = 136) and 37,632 (B = 256). The 256-row cap does not bind.
- Data mutation: one entry of uni_t2's last row with cellc flipped:
  test_uni_nvim fails at U+E0152 (1 run).
- Unicode plants (`build/planted-uni.sh`): 13 of 13 caught, including the new
  "a row read past the table's end" (229 / 230), rc=0 (1 run; log review/planted.log).
- `nvim` FFI `utf8proc_unicode_version()`: 18.0.0 (utf8proc 2.12.0, NVIM v0.12.6):
  the docstring's version claim holds.
- `git diff --check`: clean.

DEFECTS major=0 minor=1
