# Review of "The change under review" (tests/test_uni_nvim.c, tests/plants/uni.txt)

All commands run from `/home/claude/trial/rev/r17` with `tools-env/bin` on PATH (nvim v0.12.6).
Scratch harness: `review/h/` (`ours.c` prints uni_glen/uni_width at every offset of every line of a
file, `nv.lua` does the same with Neovim's utfc_ptr2len/utf_ptr2cells over FFI, `cmp.py`/`cmp2.py`
diff them, `gen.py` makes inputs). `review/mut.sh` / `review/mut2.sh` apply a text mutation to a copy
of the repo under `review/m/<name>` and run test_uni_nvim there with the Makefile's sanitizer flags.

## Confirmed defects

### 1. major: the test passes although uni_glen differs from Neovim's utfc_ptr2len() on CR x LF (GB3); the "probes tell every class apart" argument is unsound
The table merges CR, LF and Control into class 1 (gen.py: "CR and LF are ASCII, which never
joins"). Neovim only refuses an ASCII *byte* as follower, so CR followed by an overlong LF is one
cluster in Neovim (utf8proc GB3) and two in uni_glen. No probe has CR before c or LF after c, the
random lines never hold the pair (`grep -c $'\r\xc0\x8a' build/uni_nvim.lines` = 0), and the
probes' self-check only shows that *our* property bytes are distinguishable, not that Neovim has no
class we merged. So the header's "probes that tell each Grapheme_Cluster_Break class ... apart",
"so equal results on every c mean equal classes" and "One difference is known" are false, and the
test (whose job is this comparison) reports "agree with nvim".

    cd review/h && printf '\xc0\x8d\xc0\x8a\n\r\xc0\x8a\n\xc0\x8d\xcc\x80\n\xe0\x80\x8d\xe0\x80\x8a\n\xc0\x8a\xc0\x8d\na\xc0\x8d\xc0\x8a\n' > cr.txt && python3 cmp.py cr.txt
    DIFF at 0 of c0 8d c0 8a ours glen,w 2 2 nvim 4 2
    DIFF at 0 of 0d c0 8a ours glen,w 1 1 nvim 3 1
    DIFF at 0 of e0 80 8d e0 80 8a ours glen,w 3 2 nvim 6 2
    DIFF at 1 of 61 c0 8d c0 8a ours glen,w 2 2 nvim 4 2
    4 of 26 offsets differ

while `make test TESTS="uni uni_nvim"` prints
`test_uni_nvim: 2097152 code points x 32 probes (67108864), 100000 random lines (4809096 offsets x 2) agree with nvim; ...`.
(The root cause is in the base's table/contract, which the author could not change; the defect of the
change is that its check neither finds nor pins it. Apart from this and item 3, my own differential
runs found no other difference, see "What I ran".)

### 2. minor: 4-byte overlong forms are never compared with Neovim, though the header says the random lines hold "overlong" bytes
The exhaustive part uses shortest forms (plus 2-byte overlong ASCII); piece() only ever makes
shortest forms, so other overlongs appear only when random bytes happen to line up. Counted in
build/uni_nvim.lines: 2-byte overlong 363, 3-byte overlong 8, 4-byte overlong 0. A uni_len that
rejects F0 80..8F (Neovim accepts it: 278,516 overlong lines, 0 differences in my harness) passes:

    review/mut.sh ovl4 src/uni/uni.c '	if (k == 0 || k > n - i) return 1;' '	if (k == 0 || k > n - i) return 1;¶	if (c == 0xF0 && s[i + 1] < 0x90) return 1;'
    == ovl4: exit 0: test_uni_nvim: 2097152 code points x 32 probes (67108864), 100000 random lines (4809096 offsets x 2) agree with nvim; 5- and 6-byte leads differ as pinned

(The same mutation for E0 80..9F is caught, by 8 chance occurrences: `mut.sh ovl3 ...` exit 1.)

### 3. minor: the pinned 5-/6-byte difference is described wrongly ("one cluster, one cell") and is wider than what is pinned
Neovim decodes the 5-/6-byte sequence as a code point, so it can be 2 cells (overlong emoji), it joins
a preceding cluster (after Prepend; an overlong Extend after anything) and followers join it. The
difference therefore also shows at offsets that are not an F8..FD lead; the test pins only two
sequences alone on a line, and its message "5- and 6-byte leads differ as pinned" says the rest agrees.

    cd review/h && printf '\xd8\x80\xf8\x88\x80\x80\x80\n\xf8\x80\x9f\x98\x80\n\xf8\x88\x80\x80\x80\xcc\x80\n\xc3\xa9\xf8\x80\x80\x8c\x80\n' > long.txt; python3 cmp.py long.txt
    DIFF at 0 of d8 80 f8 88 80 80 80 ours glen,w 3 1 nvim 7 1
    DIFF at 2 of d8 80 f8 88 80 80 80 ours glen,w 1 4 nvim 5 1
    DIFF at 0 of f8 80 9f 98 80 ours glen,w 1 4 nvim 5 2
    DIFF at 0 of f8 88 80 80 80 cc 80 ours glen,w 1 4 nvim 7 1
    DIFF at 0 of c3 a9 f8 80 80 8c 80 ours glen,w 2 1 nvim 7 1
    DIFF at 2 of c3 a9 f8 80 80 8c 80 ours glen,w 1 4 nvim 5 1

(In 300,000 random lines that allow these leads: 4,507 differing offsets whose byte is not F8..FD,
`python3 gen.py rand 2 300000 > in.txt; python3 cmp2.py in.txt`.)

### 4. minor: the pinned-difference check has no plant in tests/plants/uni.txt (CLAUDE.md: "Every check has a plant ... a new check adds one")
The comment claims "a change on either side fails here"; no plant shows it. The only F8 plant in the
file runs `test TESTS=uni`. The check can fail (so only the plant is missing):

    review/mut.sh f8 src/uni/uni.c 'c < 0xF8 ? 4 : 0;' 'c < 0xFC ? 4 : 0;'
    == f8: exit 1: tests/test_uni_nvim.c:209: CHECK(nvim() == 5 + l && uni_glen(line[l], len[l], 0) == 1) failed

### 5. minor: probe `{ EA, VS16, 1 }` is not needed (top rule: no unnecessary line or cycle)
Neither utf_ptr2cells nor uni_width looks back, so it repeats `{ "", VS16, 1 }`: 2,097,152 extra calls
on each side. Without it the test passes and all 8 uni.c plants aimed at test_uni_nvim are still caught:

    T=tests/test_uni_nvim.c; review/mut2.sh noprobe $T ', { EA, VS16, 1 },' ','
    == noprobe: exit 0: test_uni_nvim: 2097152 code points x 31 probes (65011712), ... agree with nvim ...
    (then each plant on top, review/mut2.sh np1..np8: exit 1 eight times, each with a WRONG line)

Time: 5 alternating runs, with 8.2-11.9 s (median 10.8), without 8.3-11.4 s (median 10.1): no measurable
difference on this loaded machine; the cost is the line and the calls.

### 6. minor: false statement in the header: c < 0x80 "as its overlong 2-byte form, the only way the table entry is reached"
uni_glen reads prop() of a plain ASCII lead when a non-ASCII byte follows (and 3-/4-byte overlongs reach
it too). With prop() returning Control for 'a' only (`review/h/amut.sh`), uni_glen("a" U+0300) goes 3 -> 1:

    bash review/h/amut.sh      # glen,width per offset: unmodified, then mutated
       3   1   2   1   1   4
       1   1   2   1   1   4

(This is the same path by which a real CR reaches the table in defect 1.)

### 7. minor: false test message when the probes' self-check fails
"WRONG probes" lines are counted in `bad`, then reported as differences from Neovim:

    review/mut.sh own tests/test_uni_nvim.c '{ KA VIR, KA, 0 },' ''; cat review/m/own/out.log
    WRONG probes: property bytes 0 and 32 look alike
    WRONG probes: property bytes 194 and 242 look alike
    2 of 74629904 differ from nvim

## Unconfirmed / not counted
- The `has[]` exclusions (`c >= 0x80`, lam/alef) change nothing on today's table (test passes without
  either: mut.sh t1, t3, exit 0); they may be meant as a guard for other tables, so not counted.
- `static unsigned char line[100000][256]` holds 25.6 MB for 4.9 MB of lines; not measured as a problem.
- CLAUDE.md/README describe `make test` as recorded traces only; it now needs a live nvim (the task asked for that).
- Header claims are not tagged proved/measured/hypothesis; the base's comments are not either.

## What I ran
- `make test TESTS="uni uni_nvim"`: 1 run, passes (31 s with the rest of the target); test_uni_nvim alone 5 runs, 8.2-11.9 s.
- `make compilers`: passes; test_uni_nvim.c also compiles with clang -Wall -Wextra -Werror.
- Unicode plants (`build/planted-uni.sh`): 1 run, 22 min, 18 of 18 "caught" (log: review/planted.log).
- Mutants through review/mut.sh, mut2.sh: 16 runs (ovl4 not caught; ovl3, pkeep, asciijoin, f8, own caught; t1, t3, noprobe pass; np1..np8 caught).
- Differential runs ours vs nvim (review/h): overlong 278,516 lines (1,607,608 offsets, 0 differ); class pairs 23,805 lines (0);
  ASCII x class 74,466 lines (0); all high byte pairs 49,152 lines (0); class triples 6,612 + 98,829 lines (0);
  random lines without F8..FD leads 3 seeds, 708,932 lines, 21.4 M offsets (0); random with those leads 197,622 + ~296,000 lines
  (only the 5-/6-byte family differs); special sequences incl. overlong CR/LF and 3,000 long clusters, 1.88 M offsets (12 differ, all CR x LF).
- Not run: `make frama` and `make fuzz` on the unmodified tree (the change touches neither src/uni nor the fuzzer; the frama and fuzz plants ran inside planted-uni.sh).
