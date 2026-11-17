# Review 2: native editor design, changes of 2026-11-17

Reviewed 2026-11-17 on Fable 5.2: the design doc at rev 29 (C instead of Rust, own-drawn window, undo tree, org, plan and work packages, pstack, open questions) and the new evidence in `native-editor/` (`specs/UndoTree.tla`, `proofs/Undo.lean`, `ref/undo/`, `org/`, `gui/`).
Everything below was run unless a line says "read, not run". Reproduction files are in `native-editor-review/r2/`.
Tools: the two `env.nix` files (TLC 2.19, Lean 4.35.0, Neovim 0.12.6, Emacs 31.1, Frama-C 33.0, SDL3, weston 16), gcc 13.3, rustc 1.97, 4 cores, 15 GB, no GPU.

## Verdict

The new measurements reproduce, most of them to the digit. The undo tree is the strongest part: its rules match Neovim on 5,000 random sequences. Four conclusions are weaker than the doc says:

1. The Wayland prototype misses the doc's own keystroke target by a factor of eight on one compositor and wastes 94% of its frames on another. Which one you get depends on the compositor, not the editor. The design needs frame callbacks and a reworded target.
2. FreeType alone cannot draw Lean. 15 of 89 common Lean symbols are missing from JetBrains Mono, so font fallback is required code that the design does not have. foot, cited as the precedent, links fontconfig and HarfBuzz.
3. The org corpus is too thin to be the parser's acceptance test: orgize 0.10 passes "property drawers 35 of 35" and still drops the drawer under a normal habit (`.+1d/3d`).
4. "Keep no Emacs running because the daemon is slower" measured a garbage-collector setting. With the same setting the daemon is faster than batch. One batch job per babel block also loses babel state, and the "byte-identical export" gate fails for Emacs against itself.

Nothing I ran argues against C.

## What reproduced

| Claim in the doc | My run |
|---|---|
| `specs/check.sh`: 22 configs as expected, 14 of them broken variants | 22 of 22 as expected, 14 expected failures, 7 min 01 s with other jobs running |
| `Undo.lean` proves `undo_group` and `jump`, standard axioms only | passes in 0.5 s; both depend on propext, Classical.choice, Quot.sound |
| Neovim undo recording (`ref/undo/tree.lua`) | identical output |
| Raw Wayland: 1 thread, 0 wake-ups in 20 s, 11.7 MB, first frame 5.0 to 5.6 ms | 1 thread, 0 context switches in 20 s, 10.7 MB, 5.3 to 5.8 ms (3 runs) |
| Raw Wayland full redraw 0.66 to 0.89 ms | 1.03 to 1.21 ms here (6 runs); same order, not the same number |
| SDL3: 11 threads, 7 wake-ups, 137 MB, 74 to 82 ms, redraw 3.05 to 3.31 ms | 11 threads, 7 wake-ups, 128 MB, 83 to 86 ms, 3.42 to 3.56 ms (3 runs) |
| SDL3 has no software path on Wayland | confirmed: without Mesa on the library path, `SDL_RENDER_DRIVER=software` gives "Window framebuffer support not available" |
| 121 frames for 200 keys | 120 or 121 (but see finding 1) |
| Org corpus: 35 files, 32,336 lines | rebuilt from upstream (`r2/org/corpus.sh`): 35 files, 32,336 lines; my `el.json` equals the stored one |
| orgize 0.9 and 0.10-alpha counts, 0.027 s and 0.036 s | my outputs equal the stored `oz.json` and `oz10.json`; 0.030 s and 0.039 s |
| orgize 0.9 loses the drawer after a repeater; 0.10 loses named tables | both reduced again (`r2/org/reductions/`); see finding 3 for two more |
| Emacs: start 0.22 s, batch export 1.02 to 1.13 s, daemon 54 MB, client export 1.31 to 1.95 s, agenda 0.49 to 0.57 s | 0.25 s; 1.26 to 1.27 s; 53 MB; 1.64 to 1.91 s; 0.51 to 0.59 s |
| pstack: 0.15.43, MIT, 58 skills, 2 agents, 6,692 lines; port 0.9.138 of 2026-11-13; agent-formal-verify GPL-3.0 | all confirmed from clones of the three repositories; the quoted sentences are in the skills |

Not rerun: the tree-sitter-org column, anything on Windows, NVIDIA or a real SSMS.

## Findings

Ordered by how much they change the design.

### 1. The keystroke target and "no wasted cycle" cannot both hold as written, and the prototype's coalescing belongs to weston

The target is "keystroke to frame under 2 ms, p99", backed by "the raw Wayland prototype redraws in 0.66 to 0.89 ms". I added timestamps to the prototype (`r2/gui/wayland-ed2.c`): the time from a key to the commit that contains it.

| Compositor | Input | Frames drawn | Key to commit p50 / p99 |
|---|---|---|---|
| weston, pixman renderer (the doc's setup) | 200 keys at 10 ms | 121 | 9.4 / 16.6 ms |
| weston, pixman | 2,000 keys at 1 ms | 124 | 9.4 / 16.9 ms |
| weston, GL renderer | 200 keys at 10 ms | 200 | 1.0 / 1.9 ms |
| weston, GL | 2,000 keys at 1 ms | 1,950 | 0.5 / 3.2 ms |
| weston, GL, with frame callbacks added | 2,000 keys at 1 ms | 127 | 9.4 / 18.0 ms |

The pixman renderer keeps a buffer until a newer one replaces it on screen, so the client waits for the next repaint: no waste, but p99 is 16.6 ms, not 2. The GL renderer copies the buffer at commit and gives it back at once, so the client draws every key: p99 under 2 ms, but 1,950 full-window frames in two seconds on a 60 Hz output, about 1,830 of them never shown. The doc calls the coalescing "the desired behaviour" of the client. It is the compositor's. Hyprland renders with the GPU, so the second row pair is the likely one on your machine (hypothesis, not run).

TLC, `r2/tla/Present.tla` (9 configs, all as expected, under a second each): the prototype's rule passes NoWaste only with a compositor that holds buffers and fails it with one that releases at commit or when the window is hidden. Drawing only after the compositor's frame callback passes NoWaste and Fresh with both kinds, and draws nothing while hidden. No rule passes both NoWaste and Prompt (each key committed before the next one arrives). That conflict is in the goals, not in the code.

Suggested change: draw on frame callbacks, and reword the target to "frame ready within 2 ms of the callback; key to screen within one refresh". `Render.tla` says nothing about any of this.

Related: "first frame 5 ms" is the time until the commit request is queued in the client. The compositor's first repaint came 18.0 and 22.6 ms after start. The SDL3 number has the same caveat, so the comparison stands; the "under 10 ms" target needs the same rewording.

Reproduce: `r2/gui/run.sh`; `tlc -deadlock -config Present_X.cfg Present.tla`.

### 2. FreeType alone cannot draw Lean; the window backend is not a copy of foot

I asked HarfBuzz which glyphs JetBrains Mono (the Nerd Font build from `gui/env.nix`) has for 89 symbols common in Lean and Mathlib. 15 are missing: `⁻ ᵢ ⬝ ⦃ ⦄ 𝓝 𝒰 ⁿ ᶜ ⥤ 𝟙 ⊚ ᵀ`, plus a CJK character and an emoji. `⦃ ⦄` and `ᵢ` are everyday Mathlib. The prototype caches ASCII 32 to 126 from one font file and prints `?` for the rest.

So the editor needs font fallback (fontconfig, or its own font list and per-glyph lookup), which appears nowhere in the design, the "still lacks" list or the 1,500 to 2,500 line estimate. The Fonts paragraph says that without ligatures "each character maps to one glyph and shaping can be skipped". That fails the design's own grapheme scenario: `q` + U+0303 has no precomposed form, and HarfBuzz returns two glyphs with the mark positioned (`[q+600|tildecomb+0]`); placing that mark is shaping work, as is a ZWJ emoji drawn as one glyph. The ligature question is real (HarfBuzz turns `->` into `hyphen_greater.liga`), but it is not the only reason to shape.

foot 1.28 is cited as the precedent that makes this "a copy, not a new approach". `ldd foot` shows pixman, fontconfig, fcft, HarfBuzz and utf8proc, and its version string says `+graphemes`. foot copies CPU drawing into shared memory. It does not copy "FreeType only".

Also missing from the prototype and from the "still lacks" list (read, not run): it ignores the size in the configure event (`top_conf` discards it), and a tiling compositor decides the size; no output scale, so it is blurry on a HiDPI screen; no pointer at all.

The SDL3 comparison itself is fair for what it measures. Its memory and threads are llvmpipe's, as the doc says.

Reproduce: `hb-shape --no-positions --no-clusters FONT --text='⦃'` prints `[.notdef]`; `ldd $(nix-build '<nixpkgs>' -A foot --no-out-link)/bin/foot`.

### 3. The org corpus cannot serve as the parser's acceptance test

The comparison reproduces, but reducing further shows what it misses:

| Two-line case | Org | orgize 0.9 | orgize 0.10-alpha |
|---|---|---|---|
| `SCHEDULED: <2026-11-22 Sun .+1d>` then a drawer | drawer | lost | drawer |
| same with `+1d`, `++1w`, `09:00 +1w -2d` | drawer | lost | drawer |
| same with `.+1d/3d` (a habit with a range; Org's manual writes `.+2d/4d`) | drawer | lost | lost |
| table under `#+NAME:`, `#+CAPTION:`, `#+TBLNAME:` or `#+ATTR_HTML:` | table | table | lost |

orgize 0.9 fails on every repeater, not only habits. orgize 0.10 scores 35 of 35 on property drawers and still loses the drawer under `.+1d/3d`, because the corpus has that form only inside an example block of the manual. A new C parser that makes the same mistake would pass the phase 5 gate "zero disagreements on the corpus".

`synthetic-agenda.org` is not in your style in ways that matter: its weekday names are wrong (2026-11-18 is a Wednesday, the file says Mon), clocks end before they start, and it has one repeater form. The comparison counts elements per file, so two parsers can agree while disagreeing on where things are (the doc says positions come later).

Suggested gate: your real `~/org`, compared by position, plus random org text fed to both parsers. The "own C parser" decision stands, since orgize is Rust.

Reproduce: `r2/org/reductions/*.org` with `org/el-sum.el` and the two orgize harnesses.

### 4. Emacs as backend: the daemon result is a GC setting, sessions are lost, the export gate cannot pass

- The doc keeps no Emacs running because a daemon exported in 1.31 to 1.95 s against 1.02 to 1.13 s for batch. Emacs collects garbage less often in batch mode (`gc-cons-percentage` is 1.0 there and 0.1 in the daemon). With `gc-cons-threshold` at 100 MB the daemon exports the same file in 0.74 to 0.81 s (3 runs); batch with the same setting takes 1.16 s. So a daemon is about a third faster and saves the 0.25 s start per babel block. Batch may still be the simpler choice, but "slower" is not the reason.
- One `emacs --batch` per babel block loses state. Two elisp blocks, `(setq rev-x 41)` then `(1+ rev-x)`: 41 then 42 in one Emacs; `void-variable rev-x` for the second in its own batch job. The same holds for `:session` blocks in Python, shell and SQL. kittymax's config sets no `:session`; whether your org files do is a question for you.
- "Results written back" is not designed (read, not run): batch Emacs changes the file on disk while the editor may hold unsaved edits to it, which is the outside-writer case of the Save model.
- The phase 5 gate "exports byte-identical to Emacs's own" fails for Emacs against itself: two HTML exports of the same file differ in 1,626 lines (random anchor ids such as `org4615d85`). It is also empty, since Emacs produces both sides.
- All timings are `emacs -Q`. Your exports need kittymax's settings (xelatex, the docx backend, htmlize, ox-gfm, dbbliss); that start-up cost is not measured.
- kittymax installs toc-org, ox-gfm and org-download, which the feature table does not mention.

Reproduce: `r2/org/emacs.sh`.

### 5. Undo tree: the rules match Neovim; one rule is not covered by the model, and "keep everything" has no bound

Good news first. I wrote the model's rules in 25 lines of Python and ran random sequences of edit, `u`, Ctrl-R, `g-`, `g+` and `:undo N` in Neovim and in the rules: 2,000 sequences of 12 and 5,000 of 30, zero differences, and Neovim never showed two texts under one state number. The test can fail: with Ctrl-R going to the oldest child 1,358 of 5,000 differ, to the newest child 722.

- The rule "a jump records each child it walks into as last-visited" is not needed by any property in `UndoTree.tla`. With that rule removed TLC still passes Exact and NoLoss (404,013 states). Only the Neovim comparison catches it (278 of 5,000 differ). So the doc's tag "TLC: UndoTree (tree rules)" overstates; the evidence for that rule is the differential test, which is now in `r2/undo/undo_diff.py` and should become the package 2 check.
- A jump through the root instead of the common ancestor also passes (256,801 states). The common ancestor is a speed choice, which is fine; the "direct" variant that fails is not a mechanism anyone would build.
- NoLoss says every text that ever existed stays in the tree. Neovim does not do that: `undolevels` defaults to 1000, and with it at 3, six edits leave 4 states and `:undo 1` gives E830. The design gives no limit, so the tree and its saved file grow without bound. Either adopt Neovim's cap (then NoLoss needs the bound in it) or say that unbounded is intended.
- `jump` in Lean is a two-line corollary of `undo_group` and is correct. Its hypotheses (the stored groups really lead from the ancestor to both texts) are what the implementation must keep true; that part is TLC to 4 nodes plus tests.
- `:earlier` and `:later` with a time or a file-write count (`:earlier 10m`, `:earlier 1f`) need a timestamp and a save mark per node. Only the count form was recorded.

Reproduce: `python3 undo_diff.py 5000 30 2` (add `MUT=firstchild`, `newest` or `nogotolast`); `tlc -config UndoTree2_nogotolast.cfg UndoTree2.tla`; `nvim --clean --headless -c 'luafile undolevels.lua'`.

### 6. C: the direction holds, two numbers need care, and the safety gate is feasible for kernels only if the code avoids bit tricks

- "Rebuild 0.18 s vs 12.7 s" compares a two-file C prototype with a Rust program that links 32 crates. Like for like, the Rust prototype (no dependencies, the doc's release profile) rebuilds in 3.9 to 4.0 s and the C one in 0.15 to 0.16 s. C still wins by 25 times; 12.7 s is not the number.
- "Configuration is a C source file; a rebuild takes a fraction of a second" is tagged measured. It is measured for 150 lines. For the editor it is a hypothesis.
- What replaces Rust's safety is "Frama-C proofs whose contracts state the result". I tried that on the UTF-8 kernel (`r2/fc/`). With the function as written (shifts and masks) 24 of 28 goals prove and 4 time out. With the same logic written as comparisons, 32 of 32 prove in 5.5 s. A planted `return 1;` fails the new contract, as it should. So the kernel part of the phase 0 gate is reachable, with the rule "no bit tricks in proved code".
- The same gate lists "the rope's balance and contents". Nothing supports that yet: CBMC and Kani ran out of memory on a 3-byte piece table, and WP on heap-allocated trees is much harder than on a byte scan. As written, phase 0 cannot end until it is done. Make it a spike in package 1 with a fallback (differential fuzzing against an array) that is allowed to pass the gate.
- The doc gives two wall-time sets for the same benchmark (5.66 to 6.22 s in the table, 4.38 and 4.62 s medians in the pstack section). Both say "tie"; keep one.

### 7. Plan and work packages (read, not run)

- No package owns the editor itself: buffers, windows and splits, scrolling and wrapping, the status and command lines, the completion popup. Package 5 diffs a grid; nothing produces one from text. The crash journal, the terminal pane, project search, git signs and configuration, all phase deliverables, have no package either.
- "Starts after 0" is not "can be accepted after 0". Package 4's check (10,000 key sequences against Neovim) needs a working rope and undo tree, package 6's performance gates need the grid and the loop, package 12 needs the rope. Either thread 0 ships reference implementations (an array for text, a list for undo), or the table needs an "accepted after" column.
- Thread 0 writes fourteen headers before any code exists, and every later change goes through it. Those headers are guesses. A cheaper order: one thread builds a thin editor end to end (array text, terminal grid, a dozen keys), the headers are cut from what it needed, then the packages start.
- Phase 2's gate includes "how stale diagnostics get in a long Lean file". That is a measurement with no pass mark.
- Windows: every row is a hypothesis, which the doc says. The estimate of 1 to 2 months has nothing under it.

### 8. pstack (facts confirmed; the proposal is bigger and weaker than it reads)

- The proposal lists eleven skills, not ten ("the six above" are seven). One of them, statechart, is 408 files and 4.1 MB, including 40 TypeScript files, so it needs a TypeScript runtime next to the editor repo.
- architect sends large designs to the `arena` skill, which the proposal leaves out. statechart's script is TypeScript.
- The five `principle-*` skills are 9 to 18 lines each (62 in total) and restate your standing rules. A few lines in the editor's CLAUDE.md do the same job with nothing to pin or update.
- The planned test (the rope built once with the skills and once without) is one run per side. benchmark-checklist, one of the skills, asks for at least 5 alternating runs and calls a gap inside the run-to-run variation "no measurable difference". One pair cannot show that the skills help.

### 9. Smaller items

- Formal models still says "Five TLA+ models" and "all 18" and its table has no UndoTree row; the summary says six and 22 (22 is right).
- The tipping test is "dropped" in C vs Rust but still "phase 0's tipping test" in Formal models and in the pstack section.
- The stale-diagnostics risk is "measured in phase 1"; LSP is phase 2.
- `gui/measured-2026-11-17.md` has no commands, although the README says it does. SDL3 and weston's GL renderer need `LD_LIBRARY_PATH`, `__EGL_VENDOR_LIBRARY_DIRS` and `LIBGL_DRIVERS_PATH` pointed at the nix environment, and without them SDL3 fails with "Couldn't find matching render driver". `r2/gui/run.sh` has the full sequence.
- The org corpus is not stored and has no recipe; `r2/org/corpus.sh` rebuilds it.
- `sdl3-ed.c` is 48 lines, not 44.
- Headless Neovim waits 1 to 2 s on the first edit of each new buffer unless `swapfile` is off. The conformance driver will want that.

## Suggested order of changes

1. Frame callbacks and reworded latency targets (finding 1), with `Present.tla` moved into `specs/`.
2. Font fallback and shaping decided before the Wayland package is cut (finding 2).
3. An "editor core" package and an "accepted after" column (finding 7).
4. The org gate on real files and positions; drop "byte-identical"; ask about `:session` (findings 3 and 4).
5. `undo_diff.py` as the undo package's check; a decision on the tree's size limit (finding 5).
