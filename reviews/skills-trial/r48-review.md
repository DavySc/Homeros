# Review of "The change under review" (src/uni/gen.py, src/uni/tab.h, the comment in src/uni/uni.c)

Verdict: the tables are right (every code point equals Neovim 0.12.6 / utf8proc 2.12.0 / Unicode 18.0.0), gen.py reproduces tab.h byte for byte on input rebuilt from Neovim's own properties, and 128 is the smallest block size. No major defect. Two minor ones.

## Confirmed defects

### 1. minor: a comment in gen.py says something Neovim does not do

`src/uni/gen.py`, in the last loop: "Neovim's width tables leave out unlisted ones and combining marks". Neovim leaves out only nonspacing marks (Mn), which is what the code beside the comment does (`cat[c] == 'Mn'`). Four spacing combining marks (Mc) are 2 cells in Neovim and in tab.h: U+302E, U+302F, U+16FF0, U+16FF1. Someone who trusts the comment and "fixes" the code to match it (Mn, Mc, Me) breaks those four.

Reproduce (`review/dump.lua` writes one line per code point: code, utf8proc category, boundclass, InCB, charwidth, ambiguous, utf_ptr2cells):

    cd review && nvim --clean --headless -l dump.lua
    awk '($2==7||$2==8)&&$7==2' props.txt      # category 7 = Mc, 8 = Me
    302E 7 5 3 2 0 2
    302F 7 5 3 2 0 2
    16FF0 7 5 3 2 0 2
    16FF1 7 5 3 2 0 2
    awk '$2==6&&$7==2' props.txt | wc -l       # category 6 = Mn
    0

### 2. minor: the new proof claim in uni.c has no plant

The change replaces the comment's reason why prop()'s reads stay in bounds ("uni_t2 has 256 rows") by "WP reads uni_t1's values: none is past uni_t2's last row", so the proof now rests on the generated numbers. The claim is true and Frama-C does catch a bad number, but `tests/plants/uni.txt` has no plant for it (CLAUDE.md: "A check you have never seen fail proves nothing"; none of the 12 plants touches tab.h or prop's bounds goal). Borderline: the check (frama) is not new, only what it is relied on for.

Reproduce (`review/wC` is a copy with the last entry of uni_t1 changed from 2 to 189, one past the last row; `review/fr.sh <dir> <function>` runs the Makefile's frama-c line on one function without the cache):

    review/fr.sh wA prop    # unchanged
    [wp] Proved goals:    8 / 8
    review/fr.sh wC prop    # planted
    [wp] [Timeout] typed_prop_assert_rte_index_bound_2 (Qed 873ms) (Z3)  [wp] Proved goals:    7 / 8
    grep -c -i "tab.h\|index_bound\|uni_t1\[" tests/plants/uni.txt
    0

## Not confirmed (do not count)

- gen.py credits the emoji rule to "Neovim's emoji_wide (gen_unicode_tables.lua)" and the widths to "Neovim's width tables". The measurements fit Neovim 0.12 taking both from utf8proc instead (2 cells exactly where utf8proc's charwidth is 2, plus, from U+1F000 on, every non-ambiguous code point of boundclass Extended_Pictographic or Regional_Indicator: 117 of 117; the only 4 such narrow ones are ambiguous). The Neovim source is not on this machine, so which function it is could not be checked; behaviour is the same either way.
- `incb[c] = 0` for unlisted code points may never change anything (no unlisted code point may carry InCB in the UCD). Needs the real UCD files, which could not be downloaded here (unicode.org refused by the proxy, GitHub out of bounds).
- For the same reason gen.py was not run on the real Unicode 18.0.0 files; see the substitute run below.
- `b` is unused in the UnicodeData loop of gen.py (cosmetic).

## What was run

- `make compilers`: 1 run, gcc and clang pass.
- `make test TESTS="uni uni_nvim"`: 1 run, pass (2,097,152 code points against nvim, 20,000 lines, 853 of 853 GraphemeBreakTest cases). nvim reports utf8proc 2.12.0, Unicode 18.0.0.
- Data plant of my own (U+1F321 made narrow in tab.h, in a scratch copy): 1 run, test_uni_nvim fails at "U+1F321: 1 2, nvim 2 2".
- `make fuzz FUZZERS=uni FUZZ_SECONDS=60`: 1 run, 288,959 runs, no finding.
- gen.py on stand-in UCD files built from Neovim's utf8proc properties (`review/synth.py`; Emoji from U+1F000 taken as "2 cells in nvim and charwidth not 2", so that rule is circular; the Mn and unlisted rules are not exercised, since utf8proc already has them applied): 1 run, output identical to the committed tab.h (`cmp`).
- The nonprint list in gen.py against nvim's 6-cell code points: equal sets; the 4-cell ones are U+0000 and U+0080..U+009F, as gen.py says.
- Table size (`review/sizes.py`, `review/anyb.py`): gen.py's numbers hold (128: 189 rows, 32,896 bytes; 64: 264 rows; 256: 37,632). Over every block size 8..1024, not only powers of two, 128 is the smallest (next: 112 with 33,692). All 189 rows are used and distinct.
- `make frama`: 2 runs (second with `FRAMAC_WP_CACHE=rebuild`), both 226 / 227 with the known `uni_glen_loop_invariant_3_preserved` timeout, machine load 7 to 10 on 4 cores. Not a defect: uni_glen alone with a 150 s timeout proves 80 / 80 (1 run), and declaring uni_t2 with 256 rows instead of 189 times out the same way (1 run each, 20 s), so the change's row count is not the cause. prop alone: 8 / 8 (1 run).
- Unicode plants (`build/planted-uni.sh`): 8 of the 12 plants run, all 8 caught: the 6 non-Frama-C ones (test uni, 3 x test uni_nvim, 2 x fuzz_uni) and the first 2 Frama-C ones (225 / 227 and 210 / 214). The other 4 Frama-C plants were not run: the job was stopped at the 30 minute limit (about 8 minutes per Frama-C run under this load). They are base code that the change does not touch.
