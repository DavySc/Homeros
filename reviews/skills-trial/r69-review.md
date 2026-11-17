# Review of "The change under review" (tests/test_uni_nvim.c, tests/uni_nvim.lua, tests/plants/uni.txt)

All commands run from /home/claude/trial/rev/r69 with `export PATH=$PWD/tools-env/bin:$PATH`.
Everything below tagged **measured** was run; counts are in the last section.

## Confirmed defects

### 1. major: the sweep passes with 1276 wrong table entries that change clusters against Neovim

The header comment's claim (point 1) "a table entry that differs from Neovim's changes some
probe's clusters" is false, and so the test does not check what the admitted axiom needs
("the tables equal Neovim's own values for every c below 2^21"). The argument only compares
property bytes that are present in the table with each other; when a whole class carries a
wrong Indic_Conjunct_Break value (the kind of error gen.py would make), the right byte is not
present, no probe tells the two apart, and the random lines do not reach the context either.
Shown with every Extended_Pictographic entry of tab.h (gcb 12, InCB None) changed to InCB Extend:
the test prints its three success lines and `make test` exits 0, while
U+1F600 U+094D U+200D U+1F600 U+0915 is one 17-byte cluster with that table and two clusters
(14 + 3) in Neovim. **measured**, 2 runs (once planted in prop(), once in tab.h itself).

    $ bash review/repro1.sh
    tab.h entries changed: 1276
    test_uni_nvim: every code point below 2^21 in 21 probes, 26 property bytes told apart
    test_uni_nvim: 1000000 random lines, 62583923 bytes, same clusters and cells
    test_uni_nvim: departs as recorded on 5- and 6-byte forms (Neovim: one character)
    make test exit 0
    ours, real table:   14 2  3 1
    ours, wrong table:  17 2
    Neovim (len cells):   14   2   3   1

Scope (**measured**, review/mut/mut.c and cls.c: every present property byte replaced by each of
the other 207 values, the probe line's clusters and cells compared; then 2,000,000 random lines
per pair to see whether the pair is distinguishable at all): all gcb and cell errors change a
probe; 19 InCB errors do not, and 4 of those change clusters in some line: byte 12 as 60 and
76 as 124 (emoji as InCB Extend), 10 as 26 and 202 as 218 (Prepend as InCB Linker). The last two
are caught by the random lines only (1 run: sweep line printed, then line 74 failed); the first
two by nothing. A single wrong entry of this kind is caught, by the "told apart" check (1 run:
`U+00AE and U+00A9: property bytes 12 and 60, same probes`, line 139); the miss is the whole class.

    $ cd review/mut && sed -n 1,3p mut.c && gcc -std=c17 -O1 -I../../include -o mut mut.c uni_ov.c && ./mut | tail -1
    23 present bytes, 19 undetected (true, wrong) pairs, skip=-1
    $ gcc -std=c17 -O2 -I../../include -o cls cls.c uni_cls.c && ./cls | grep differs
    10 as 26: differs on U+0301(50) U+0600(10) U+0600(10) U+0600(10) U+0300(50) U+0916(32)
    12 as 60: differs on U+00AE(12) U+09CD(18) U+200D(251) U+00A9(12) U+0916(32)
    76 as 124: differs on U+231A(76) U+094D(18) U+200D(251) U+231A(76) U+0916(32) U+231A(76)
    202 as 218: differs on U+070F(202) U+FE0F(50) U+0916(32) U+0301(50)

### 2. minor: four of the test's checks have no plant (CLAUDE.md: "Every check has a plant ... a new check adds one")

All six new plants fail at the same check, line 74. Never seen to fail by any plant:
`CHECK(sig[k] == v)` (line 129), `CHECK(sig[a] != sig[b])` (line 139, the check the "told apart"
claim rests on), `CHECK(k == m)` (line 78), and the pinned departure (lines 195, 196). The existing
plant aimed at the same byte range ("0xF8 read as a 4-byte lead") also fails at line 74 here, not
at the pin. Lines 139 can fail (defect 1's single-entry run; also probe 7, 9, 11, 15 or 17 blanked),
so a plant exists to be written; none was.

    $ cat review/planted-new.log     # bash build/planted-new.sh: tools/planted.sh over the 6 new plants
    caught        uni_nvim: the table read one code point off   (make test TESTS=uni_nvim): tests/test_uni_nvim.c:74: CHECK(k + 1 < m && out[k] == g && out[k + 1] == w) failed
    caught        uni_nvim: the table ends at U+FFFF   ... tests/test_uni_nvim.c:74: ...
    caught        uni_nvim: GB9c dropped ...             tests/test_uni_nvim.c:74: ...
    caught        uni_nvim: arabic_combine() forgets ... tests/test_uni_nvim.c:74: ...
    caught        uni_nvim: a third flag joins a pair ... tests/test_uni_nvim.c:74: ...
    caught        uni_nvim: VS16 widens only flag letters ... tests/test_uni_nvim.c:74: ...

### 3. minor: a failed Neovim run leaves its input file in build/

nvim() unlinks the temporary file only after `CHECK(pclose(f) == 0)`; when Neovim is missing or
fails, 26 MB stays in build/ per run. **measured**, 1 run.

    $ PATH=/usr/bin:/bin ./build/san/test_uni_nvim 2>&1 | tail -2; ls -la build | grep uni_nvim
    sh: 1: nvim: not found
    tests/test_uni_nvim.c:53: CHECK(pclose(f) == 0) failed
    -rw-------  1 root root 26105856 Nov 17 19:00 uni_nvim_C0bwcL

### 4. minor: the random lines go to Neovim in one 62.6 MB batch, which more than doubles Neovim's peak memory for no time gained

The sweep compares in chunks; the random part does not. Comparing every 100,000 lines
(review/test_uni_nvim_batched.c, one line changed) gives the same checks. **measured**, 5 alternating
runs per side (review/rss.py, RSS sampled every 50 ms): Neovim's peak 305 MB (304.6 to 305.1) against
136 MB (136.1 to 136.2); the test's own 266 MB (266.1 to 268.7) against 198 MB (197.8 to 201.0);
wall time median 21.9 s (20.3 to 23.2) against 21.9 s (20.8 to 28.6): no measurable difference.

    $ python3 review/rss.py ./build/san/test_uni_nvim
    21.9 s wall; peak RSS KB: {'test_uni_nvim': 266120, 'sh': 1968, 'nvim': 304864}; exit 0
    $ python3 review/rss.py ./review/batched     # built from review/test_uni_nvim_batched.c with make test's flags
    21.9 s wall; peak RSS KB: {'batched': 197848, 'sh': 1968, 'nvim': 136232}; exit 0

### 5. minor: false claim in the header comment: "below 0x80 as an overlong pair, which is how the table is reached there"

The table is also read for a plain ASCII byte that starts a cluster before a non-ASCII byte
(uni_glen's `q = prop(p)`), which the sweep never writes. **measured**, 1 run: changing the entry of
U+0001 changes the cluster of the plain bytes 01 CC 81.

    $ cd review/mut && gcc -I../../include -o asc asc.c uni_ov.c && ./asc
    table as is: glen 1
    entry of U+0001 set to 0: glen 3

(A plant that skips the table for a plain ASCII start is still caught, by the overlong pairs: 1 run, line 74.)

## Not confirmed (do not count)

- The plants file says the "contract too" plants are written "so that Frama-C still proves". `make frama`
  with each applied (review/frama-plants.sh, FRAMAC_WP_CACHE=rebuild, 1 run each, machine loaded):
  GB9c 227 / 227; the other three 226 / 227 with the timeout in typed_uni_glen_loop_invariant_3_preserved,
  the same goal that timed out on the unchanged copy in the same batch. That is the known load timeout,
  so the claim is neither confirmed nor refuted for those three.
- tests/uni_nvim.lua stores a cluster's length in one byte: a 401-byte cluster (a + 200 x U+0301) comes
  out as 145 (`nvim --headless --clean -n -l tests/uni_nvim.lua review/long.in | od -An -tu1` gives `145 1`;
  Neovim and uni_glen both say 401). The test's lines stay under about 125 bytes, so no run is affected today.
- Some probes may be unnecessary for the "told apart" check: with compare() stubbed, blanking any one of
  probes 2 to 6, 8, 10, 12 to 14, 16, 18 to 20 still tells all 26 bytes apart (19 runs). They still compare
  a rule against Neovim, so I do not call them needless.
- "The one departure" (5- and 6-byte forms): I found no other. 3 seeds x 20,000 lines of up to 400 bytes from a
  different generator (review/gen2.py: 40 code points of every property byte, long Extend runs, raw high bytes),
  2.9 M clusters, 0 differ from Neovim.

## What I ran

- `make test TESTS="uni uni_nvim"`: 1 run, passes (52 s with the rest of make test); the test binary alone 8 more runs, all pass.
- `make compilers`: 1 run, passes.
- tools/planted.sh restricted to the 6 new plants: 1 run, 6 caught (all at line 74). The older Unicode plants are untouched by the change and were not rerun; `make fuzz` likewise (src/uni unchanged).
- The test under review against 7 planted copies: ExtPict as InCB Extend in prop() (passes), the same in tab.h (passes), Prepend as InCB Linker (line 74, random part), one entry 12 as 60 in tab.h (line 139), 0xF8 as a 4-byte lead (line 74), plain ASCII start skipping the table (line 74), Neovim absent (line 53).
- review/mut/mut.c: 23 x 207 single-entry replacements; review/mut/cls.c: 19 pairs x up to 2,000,000 random lines.
- The sweep with compare() stubbed and one probe blanked: 19 runs.
- `make frama` on 5 copies (4 planted, 1 unchanged): results above.
- Memory and time: 5 alternating runs per side.
- Independent comparison with Neovim: 3 seeds, 2.9 M clusters.

DEFECTS major=1 minor=4
