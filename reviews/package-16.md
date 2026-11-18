# Review of package 16 (org parser): build A (PR 21, head 498094b) against build B (PR 22, head b96d9ac)

Independent review and comparison, as DESIGN.md asks for the org parser ("two threads build it independently and the better one wins on the package's own checks") and PROJECT-INSTRUCTIONS.md step 3 ("less code and better measurements wins. Record the comparison"). Everything below was run by the reviewer on one machine (4 cores, gcc 13.3, clang 18.1, valgrind 3.22, the tools of `tools.nix`: Emacs 31.1 with Org 9.8.7), both builds from the same base (main ab4bcd4) under the same conditions. The oracle, the drivers and the generators are the reviewer's own and are in `reviews/r16/` (its README says how to rerun); raw timings are `reviews/r16/timing.log`. Tags as CLAUDE.md asks.

**Winner: build B.** Both builds pass `make check` and `make planted`. B is the smaller parser (765 lines against 998), parses 3 to 5 times faster, never crashed, and on 283,000 random and enumerated texts differs from Org only in the three cases its PR already names. A crashes on a 34-byte text and on four large shapes, and differs from Org on babel calls and on lists inside drawers and blocks. A has two things B lacks and package 16 still needs: an incremental reparse (a keystroke costs A 18 to 374 µs where B reparses the whole text, 0.5 to 6 ms) and exact Unicode character classes. Neither outweighs a parser that can take the editor down, and both can be added to B (section 7).

**Verdict on B: approve after finding 1** (Unicode classes in tags, drawer names and footnote labels). Nothing here needs Davy.

## 1. The builds' own checks

| Check | Build A | Build B | Tag |
|---|---|---|---|
| `make check` (compilers, tests under ASan+UBSan, fuzzers 30 s each, 2,000 fresh Neovim sequences, undo-diff, lean, frama, specs, win, and the build's own comparison with Emacs) | passed, 17 min 42 s | passed, 18 min 52 s | measured, 1 run each |
| its comparison with Emacs inside that run | `org-oracle`: 400 files, 849,505 elements, 0 differ | `org-conform`: 2,000 texts, 0 differ | measured, 1 run each |
| its fuzzer inside that run, 30 s | 201,802 runs, nothing | 664,732 runs, nothing | measured, 1 run each |
| `make planted` | 132 of 132 caught (its 10 org plants among them) | 125 of 125 caught (its 3 org plants among them) | measured, 1 run each |
| The reviewer's 12 planted bugs, the same 12 rules of Org broken in each build (`r16/xplants-*.txt`), against the build's recorded tests, then its fuzzer (30 s), then its comparison with Emacs (seed 1) | 9 caught; not caught: `#+HEADER` no longer affiliated, priority `[#65]` accepted, `@` refused in tags | 12 caught | measured, 1 run each |

B has fewer plants of its own, but its checks are the stronger ones: its 32 recorded texts and 2,000 short random texts noticed every planted rule, where A's 24 recorded texts and 400 long files missed three.

## 2. Conformance with Org, same texts for both

One oracle for both (`r16/canon.el`: every element's type, `:begin`, `:post-affiliated`, `:contents-begin`, `:contents-end`, `:end`, and each headline's level, TODO keyword, priority, COMMENT, title and tags), one driver per build printing the same lines. A text counts once, at its first differing line. All measured, 1 run per row.

| Texts | Count | Elements | A differs | B differs |
|---|---|---|---|---|
| Real files: every `*.org` of org-mode c0ed324 and nvim-orgmode 08c3ea6, ORG-NEWS, the synthetic agenda | 63 | 26,538 | 0 | 0 |
| A's generator, seeds 7 and 23 | 60,000 | 1,305,008 | 2 | 0 |
| A's generator mutating the real files, seed 7 | 2,000 | 1,026,233 | 1 | 1 |
| B's generator, seeds 7 and 23 (Org itself fails on 6) | 60,000 | 815,470 | 185 | 0 |
| The reviewer's generator `r16/gen_r.py`, seeds 7 and 23 | 60,000 | 4,597,372 | 1,502 | 12 |
| Every 3-line text over 33 line kinds and every 4-line text over 16 (`r16/enumgen.py`) | 101,473 | 485,662 | 97 | 0 |
| One non-ASCII character in each of 13 places, 12,988 code points, and 1,890 cased letters in a block name (`r16/sweep.py`) | 170,737 | 417,552 | 1,890 | 10,576 |

What the differences are (each reduced in `r16/repro/`):

| Difference | Who | Texts | Reduced case |
|---|---|---|---|
| A babel call's brackets are matched across lines, to the end of the text if need be; A ends the call at its line | A | 1,678 random, 97 enumerated | `call-open.org` |
| A list inside a drawer or block inside an item takes the outer list's structure (one line: `parse` hands `e.ss, e.se` to every container, Org only to items and lists; with that changed both texts agree, measured) | A | 2 random, both from A's own generator; also the only disagreement kind the side-by-side fuzzer found | `list-struct-1.org`, `list-struct-2.org` |
| Characters above ASCII that are not word or alphanumeric to Emacs count as such: tags, drawer names, footnote labels | B | 5,010, 1,838 and 1,838 of 12,988 code points; none of the random texts | `tag-arrow.org`, `drawer-arrow.org`, `fn-arrow.org` |
| A block name inside a list is a regexp to Org (`#+begin_x*` is closed by `#+end_x`); B compares it literally (named in its PR). A copies Org here, and that is where it crashes (section 3) | B | 3 random | `list-regexp-name.org` |
| Case folding above ASCII: `#+begin_É` is closed by `#+end_é` | both | 9 random, all 1,890 cased letters | `block-case.org` |
| An in-buffer `#+TODO:` line changes the keywords; both compile in Davy's | both | 1 random | `todo-line.org` |

So outside the sweep A differs alone on 1,777 texts and B alone on 3. The sweep is the one place A is ahead: its character classes are generated from Emacs's own syntax table (`class.el`, 911 ranges) and are exact.

A's 65,535 cap: a repeat in A's matcher is stored in 16 bits, so `[ \t]*` stops after 65,535 characters. With 100,000 tabs before a bullet A sees one paragraph where Org and B see a list (measured, `patho.py` tabs-indent).

## 3. Crashes and worst cases

| Test | Build A | Build B | Tag |
|---|---|---|---|
| `- a\n  #+begin_a**b\n  #+end_ab\n- c\n` (34 bytes, `r16/repro/min2.org`) | segmentation fault, -O2 and ASan: the name is compiled as a regexp, `**` makes a loop over an empty match, the matcher recurses without end | parses, equal to Org | measured |
| Both builds side by side in one libFuzzer process on text built from about 150 org fragments (`r16/xfuzz.c`), 639 s | 10 crashes, all that stack overflow | none; survives all 10 texts | measured, 1,240,957 runs |
| The same on raw bytes, 305 s | none | none | measured, 218,305 runs |
| Disagreements between the builds in those runs | 1 kind on fragments (the list structure above; Emacs agrees with B on the 3 texts kept), 1 kind on raw bytes (a drawer name holding invalid UTF-8, B's class difference) | | measured |
| A drawer of N properties, closed or not | stack overflow from N = 26,145 | 300,000: 27 ms | measured (bisected) |
| N blank lines after a footnote definition | stack overflow from N = 32,688 | 300,000: 3.8 ms | measured (bisected) |
| A table.el rule of N columns | stack overflow from N = 65,352 | 300,000: 0.4 ms | measured (bisected) |
| ASan+UBSan over all 283,562 files of section 2 and the 25 worst-case shapes | clean but for the four shapes above | clean | measured, 1 run |
| Every allocation failing in turn (`r16/oom.c`; synthetic agenda, 10 real files, 11,111 random texts) | 54,829 failures, each clean; the list right again after the next edit | 48,371 failures, each clean | measured, 1 run |
| 2,999 headlines each one level deeper (4.5 MB) | 6.6 ms | 3.4 s: each headline scans to its end, so the cost grows with depth | measured, 1 run |
| 3,000 list items each one column deeper (4.5 MB) | 952 ms | 86 ms | measured, 1 run |
| 20,000 unclosed blocks, drawers, LaTeX environments (80 to 280 KB) | 5.6 s, 5.0 s, 15.6 s | 2.7 s, 2.6 s, 6.2 s | measured, 1 run; both rescan to the limit for each, as Org does |

The stack limits are for Linux's 8 MB stack; Windows gives 1 MB (hypothesis: an eighth of those sizes there). The other 17 shapes of `r16/patho.py` run in under 160 ms in both builds, B the faster on all but one (a single line of 3 MB: 5.8 ms against 1.9 ms).

A's crash is in what makes A exact: Org's regexps run by a backtracking matcher, with patterns built from the text. The matcher recurses once per repeat of a group, has no guard against a repeat that matches nothing, and compiles text as a pattern without checking it. B matches by hand and keeps its open containers on the heap.

## 4. Code

| | Build A | Build B | Tag |
|---|---|---|---|
| `src/org/org.c`, lines | 998 | 765 | measured |
| of which code (no comments, no blank lines) | 884 | 689 | measured |
| `org.h` | 80 | 50 | measured |
| generated | `class.h`: 911 ranges from `class.el` (19 lines) | none | measured |
| `org.o` gcc -O2: text, bss | 37,343 and 134,656 bytes (the pattern cache) | 23,249 and 48 bytes | measured |
| Whole PR | +1,764 lines | +1,743 lines | measured |
| Interface | `Org` over a `Text`; `org_edit(o, t, pos, ndel, nins)` reparses the headline chunks an edit touches; headline, timestamp, planning and clock readers | `org_parse(s, n, &count)` over a flat buffer, a new array each call; `org_head` | read |
| How it matches | Org's regexps verbatim, run by a 265-line backtracking matcher | each regexp written out as a C function that quotes it | read |

A does more in its 998 lines: about 130 are the incremental reparse and about 105 the timestamp, planning and clock readers, neither of which B has. Parser against parser the two are the same size (A about 700 lines with its matcher, B 765).

## 5. Speed

Instructions of one full parse (callgrind, exact, 1 run; the cost of loading the file subtracted), gcc -O2:

| Text | Build A | Build B |
|---|---|---|
| synthetic agenda, 81 KB, 4,218 elements | 21.3 M | **4.1 M** |
| ORG-NEWS, 406 KB, 5,164 elements | 30.8 M | **9.4 M** |
| org-manual, 902 KB, 10,683 elements | 121.1 M | **28.3 M** |
| synthetic agenda 12 times, 974 KB, 50,605 elements | 248.4 M | **48.3 M** |

Wall time on the idle machine, 5 alternating runs per side, median (range). A full parse is the median of 21 in a run; an edit run is 2,000 one-byte inserts, each followed by bringing the parse up to date: `org_edit` for A, reading the text out of the rope and `org_parse` for B. "B, subtree only" is what a caller would pay to reparse just the innermost headline subtree holding the edit; B does not have the code that puts the result back into the list, so it is a lower bound, not a result.

| | Build A | Build B | B, subtree only | Tag |
|---|---|---|---|---|
| Full parse, synthetic agenda, ms | 2.02 (2.00 to 2.93) | **0.484 (0.468 to 0.501)** | | measured |
| Full parse, ORG-NEWS, ms | 3.05 (2.96 to 3.40) | **1.50 (1.46 to 2.07)** | | measured |
| Full parse, org-manual, ms | 12.4 (11.8 to 16.9) | **4.32 (4.27 to 5.69)** | | measured |
| Full parse, agenda 12 times, ms | 25.0 (23.8 to 25.2) | **6.16 (6.02 to 7.28)** | | measured |
| Typing, synthetic agenda: median, µs | **18.4 (17.8 to 26.9)** | 484 (479 to 491) | 7.2 (7.2 to 7.3) | measured |
| p99 | **41.8 (35.8 to 64.9)** | 724 (648 to 828) | 22.5 (20.4 to 28.3) | measured |
| Typing, ORG-NEWS: median, µs | **22.7 (21.7 to 32.3)** | 1,510 (1,490 to 1,520) | 9.8 (9.3 to 10.2) | measured |
| p99 | **59.7 (52.4 to 92.1)** | 2,140 (2,120 to 2,240) | 27.8 (24.6 to 29.7) | measured |
| Typing, org-manual: median, µs | **69.0 (66.7 to 89.2)** | 4,210 (4,200 to 4,240) | 23.0 (22.8 to 24.5) | measured |
| p99 | **381 (372 to 568)** | 6,190 (5,960 to 6,250) | 70.5 (64.2 to 79.3) | measured |
| Typing, agenda 12 times: median, µs | **278 (268 to 291)** | 5,990 (5,930 to 6,130) | 64.7 (63.6 to 64.9) | measured |
| p99 | **454 (318 to 631)** | 8,840 (8,600 to 10,100) | 142 (139 to 184) | measured |
| Worst single edit of the 10,000 at random places, org-manual, µs | **1,420** | 14,600 | 4,880 | measured |

Against the design's 2 ms budget for a reparse: A is inside it on every text (worst single edit 3.9 ms once in 80,000, on the 974 KB text). B with a whole reparse is inside it on the 81 KB agenda the design takes as Davy's size (p99 0.72 ms), at it on 406 KB and over it on 1 MB. A's incremental reparse was also checked against fresh parses on real-sized files: 4,500 random edits to three files, the list equal to `org_new`'s after each (`r16/edits_a.c`, measured, 1 run, ASan+UBSan).

## 6. Why B

- **Passes the checks.** Both pass their own. On the reviewer's, A fails the one that cannot be argued with: it crashes, on a text a person can type. B does not crash on anything tried.
- **Less code.** 765 lines against 998, with no generated table and no pattern cache.
- **Better measurements.** Conformance: 3 texts of 283,000 against 1,777. Full parse: 3.3 to 5.1 times fewer instructions. A's leads are the edit cost and the Unicode classes; both are things A added that B has not built yet, and B's design leaves room for both (its fuzzer already checks that a subtree parses alone to the same nodes).
- A's faults are not all shallow. The babel call cannot be copied exactly inside A's chunks (the call may end past its headline, which is what the chunks assume never happens), and the crash is in the matcher that carries the whole build.

## 7. Findings on build B (PR 22)

1. **Fix before merge: character classes above ASCII.** `namec()` (`src/org/org.c:70`) and the tag scan in `org_head` (`:757`) take every non-space byte from 0x80 up as word and alphanumeric. Org does not: `* h :a→b:` has no tags, `:a→b:` opens no drawer, `[fn:a→b]` defines no footnote. B is wrong for 5,010 (tags), 1,838 (drawer names) and 1,838 (footnote labels) of 12,988 code points; A for none (measured, `r16/sweep.py`). The PR names this as a known difference; A shows the exact answer costs 19 lines of elisp that print Emacs's ranges and a binary search. The gate is zero disagreements on random org text, and a generator that writes such tags (B's does not) finds these at once.
2. **Record, fix when the incremental reparse is built: a headline's end is found by scanning forward from every headline** (`:481`), so the parse costs text times depth: 3.4 s for 2,999 nested levels where A's single pass over the list takes 6.6 ms (measured). No real file is that deep; at depth 4 to 6 it is a few per cent of a parse.
3. **Record in the PR: no incremental reparse, flat buffer, new array per parse.** The design's parser "reparses only the heading subtree an edit touches". B's whole reparse is over 2 ms from roughly 400 KB (table above). Package 16 owes this before the phase 5 gate; A's `org_edit` and `fixup` (130 lines, checked by A's fuzzer and by `r16/edits_a.c`) are a worked answer, and a babel call with open brackets has to fall back to a whole reparse.
4. **Known differences, confirmed and counted:** case folding above ASCII (all 1,890 cased letters tried), a block name used as a regexp inside a list (3 of 182,000 random texts), in-buffer `#+TODO:`. All three are in the PR. None is in the 63 real files.
5. Small: `tests/org_drv.c` reads at most 16 MB of a file and says nothing when it cuts.

What was looked for and not found in B: any crash or sanitizer report (283,562 files, 1.46 million fuzz runs, 48,371 failed allocations), any difference from Org on ASCII text outside finding 4, any of the reviewer's 12 planted bugs passing its checks.

## 8. What goes with A

Worth keeping from PR 21 when it closes: `class.el` (for finding 1), `org_edit`/`fixup` (finding 3), the timestamp, planning and clock readers with their oracle columns (the agenda needs them), and its recorded cases "planning repeater is case-sensitive" and "tab counts 8 in list indentation".
