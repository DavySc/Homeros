# Review: Org links and footnotes (package 16, build B)

Reviewed: commit 648bb8d on top of PR 108's head aa20bbc and its report cd169e8
(bundle `/mnt/project-files/org-b/links-footnotes.bundle`, branch `org-links`).
origin/main a0c2bcf is an ancestor of it, so it merges onto main as it is.
Reviewer: a separate review thread, 2026-11-22. Repro files: `reviews/org-links-repro/`.

**Verdict: changes requested.** One performance fault, eight Org differences
the checks cannot see (each one a planted bug that no check catches) and
three copies. The colours and conceal agree with Org on every random text
I generated, and with a one-pass walk whatever order the lines are asked in.

## What I ran

| What | Runs | Result |
|---|---|---|
| `make check`, alone, on 648bb8d | 1 (35 min) | passed (EXIT 0); every org-conform mode 0 differ, frama all proved |
| `make check` beside my probes and plants | 1 | failed only at the heavy frame gate (`tests/test_org.c:1235`, limit 40 ms) because of the extra load; `build/san/test_org` then passed 5 of 5 runs on an idle machine |
| `org_conform.py -f` and `-s`, fresh seeds 101 to 104, 4,000 texts each | 8 | **0 of 32,000 differ** (Org gave no answer on 195 to 239 texts with -f, mostly the date skip, and 6 to 16 with -s) |
| random-access probe (`order.c`): faces of 300 random windows and conceal of 300 random lines, each in random order with the other call in between, against one walk from the top | 3 random 12,000-line texts (`gen.py` seeds 1 to 3) + `long.org`, `cross.org` (links of 100 lines, an inline footnote over 4,100 lines, across the 4096-line checkpoints) | 0 differ. Planting a bug in the 64-line look-back (`if (P > c->lk) c->lk = P;`) makes it report 3 differences on `long.org`, so the probe can fail |
| my plants (`review.txt`, run by `run.sh`, the body of `tools/planted.sh`) | 19 | 8 caught, 11 not caught (below) |
| the package's own plants, `tests/plants/org.txt` (2 at a time, then the misses alone) | 116 | 114 caught; 1 not caught, 1 broken (finding 4) |
| frame-gate probe (`perf.c`), sanitizers on, CPU time | see finding 1 | |

## Required

### 1. Link search is quadratic in a paragraph with many unclosed openers (measured)

Each `<TYPE:` without a `>` (`angle()`) and each `[[x][` without a `]]` (`uri()`'s
description loop) scans to the end of the paragraph again. Nothing remembers
where an earlier opener's scan stopped. The walk does this for every line, so a
frame after an edit in such a paragraph pays the whole quadratic cost.

Last 50 lines shown, then coloured (`perf.c`), one paragraph of N copies of the line:

| N lines | `<http:a` | `[[a][x` |
|---|---|---|
| 500 | 7.8 ms | 36.6 ms |
| 1,000 | 29.5 ms | 144 ms |
| 2,000 | 116 ms | 653 ms |
| 4,000 | 732 ms | 2,721 ms |
| 20,000 (3 runs) | 6.8 to 7.5 s | 31.9 to 35.8 s |

(One run per size up to 4,000, measured while `make check` ran; it doubles to
quadruples per doubling of N, so it is quadratic.) The 20 ms gate is passed
at about 300 lines. An inline footnote whose parentheses never close (`[fn:: (`)
does not do this: 11.5 to 17.1 ms at 20,000 lines.

A suggested fix: when a scan fails at a paragraph boundary B, any later opener
before B fails there too. So keep B (and what was looked for) in the Scan and
fail at once until the search passes B. Add a `test_org` gate with this text.

The gate also covers a check nothing tests now. The `inside()` stop in `uri()`
(the URI and description loops) only bounds the search: link_next rejects
such a match anyway, and both resume one byte after its start. My plant that
removes the description's stop changes nothing org-conform can see. I tried
5 directed texts (`texts/skip*.org`); Org agrees with the planted
and the real code. It is a speed mechanism that no gate covers.

### 2. Eight Org differences the generator never makes (planted, reproduced against Org)

Each line below is one of my plants. `make org-conform` (or `test TESTS=org`)
passes with the plant in place. The directed text next to it makes the planted
driver differ from Org, and the unplanted driver agrees with Org on all of them
(`tri.sh` in the run log, `texts/`):

| Plant | Directed text that shows it |
|---|---|
| `angle()`: a line of blanks then `>` no longer ends the search | `ang.org` `<id:a\n >x>`, `ang2.org` |
| `tc()` without `3` (no `w3m:` links) | `w3m.org` `w3m:xy z` |
| `bibtex` missing from `ltypes` | `bib.org` `bibtex:ab x`. 17 of the 24 types are never generated: `LINKS` has http, https, id, file, mailto, doi and shell |
| `fin()` without DEL | `del.org` |
| `valid()`: verbatim that ends after the line ignored (`l > VC` dropped) | `vb3.org` |
| `frag()`: a `$` fragment may end after `.` | `dol1.org` |
| `unhide()`: a hidden span around verbatim keeps its tail (the split line dropped) | `un1.org` `[[a =b= c][d]]`, `un2.org` (with `-s`) |
| `uri()`: a backslash pair before `]` is no choice (`b = 0`) | `bs1.org` `[[a\\\\]b]]`, `bs3.org` |

These are bugs the checks would let through later. Add each text's shape to
`LINKS`/`FNS` (all 24 types, a line of blanks then `>`, DEL, `$x.$`, verbatim
over a reference line, a target holding verbatim, backslash pairs before `]`),
and add these as plants.

### 3. Three copies (Davy's one-solution and no-duplication rules)

- **org-verbatim-re's closer is written twice.** `vclose()`/`vend()` (new) and
  `emph_close()` (PR 108) both look for the first `c >= m + 2` with
  `S[c] == marker && !sp(c - 1) && post(c + 1)`, up to the second newline. The
  first has no cache; the second caches the closer per marker for conceal. The
  opener test is also written twice: `verb_re()`'s `sp(q) || one(q, "-('\"{")`
  and `emphasis()`'s negation of it (`post()` was made shared, `pre` was not).
  One closer search, with `emph_close()`'s cache as its caller's choice, and one
  `pre()` would do.
- **`by_first` and `by_start`** compare the same thing, because a Conceal starts
  with its `a` (C17 6.7.2.1p15). `by_first.diff` deletes `by_start` (6 lines
  less, 2 more). With it, `make test` passed, and `make org-conform ORG_N=1000`
  gave 0 differ in all four modes.
- **`tools/org_faces.el` and `tools/org_show.el`** carry the same 16 lines (the two
  `advice-add` forms and their comments; `diff` finds them identical). Put
  them in one file that both load.

### 4. Two of the package's own plants no longer work (run)

- `org: show: script braces stop at the line's end` is **not caught** on 648bb8d (also when run alone).
  The same plant at aa20bbc is caught: 3 of 2000 differ. The longer `MATH` list (`+ LINKS + FNS`)
  changes what seed 1 generates, and the texts that showed it are gone. Point it at a seed that
  catches it (as done for `SEED=3` elsewhere), or better, give it a directed text.
- `org: show: a headline ending a block read without the line above` is **broken**: its
  replacement `c = scan_at(f, &f->cs, CL)` still calls the old three-argument `scan_at`, so
  the copy does not compile ("FAILED OTHERWISE", Makefile:102). It needs a replacement that
  compiles, for example `c = scan_at(f, CL)`.

## Questions and notes

- `forbidden()`: my plant that drops `#+begin_example` is not caught. In 9
  directed texts (`ex*.org`: references and definitions in example, src,
  export and comment blocks) I found no text where it shows. In
  example, src and export blocks org-block is the top face over the
  footnote's. Only the comment entry showed in my texts (`ex6.org`, which
  agrees with Org). Is there a text where the other three matter? If there is,
  generate it. If there is none, they are code no one can see. Keeping Org's list
  as written is defensible, but say so in the comment.
- `links_to(&c, c.at)` before a checkpoint is saved: a plant that deletes it
  is caught neither by `test TESTS=org` nor by my random-access probe (it
  only makes a later search start earlier). It is speed only. The existing
  gate plant covers the look-back but not this line.
- The heavy gate has little room. With my `perf.c` on an idle machine the
  heavy text took 34.8 to 50.9 ms, median 35.2 (5 runs), against the limit of 40
  (the author measured median 27.8). One `make check` beside other work failed on it.
- The ASCII fast path in `in()`/`namec()` earns its place. With it the heavy
  text took median 34.3 ms (32.5 to 35.9), without it 41.6 (36.0 to 62.8), 5
  alternating runs each. On plain lines there is no measurable difference
  (13.8 against 12.8, inside the spread).
- Nits: the commit message says "parens.h gone", but no commit ever had a
  `src/org/parens.h`. `verb_re()`'s `q < N &&` repeats the loop's own test.
  `include/org.h` line 81 ("not coloured yet") is thread 0's to fix, as already
  known. In `org_faces.el` the header still lists emphasis among the objects that
  are off, which is still true.

## Re-review plan

Run `perf.c` on the two quadratic texts (5 alternating runs). Run the 8
directed texts and the 11 uncaught plants of `review.txt`, the org plants and
`make check`. Run fresh seeds 105 to 108 with `-f` and `-s`.
