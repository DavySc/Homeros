# Native editor: design package

This folder is everything a new project needs to build Davy's native editor. It does not depend on anything outside itself, except the public tools and repositories named below and Davy's own configs, which are the behavioural source of truth.

"You" in DESIGN.md is Davy, the only user the editor is for.

## Read in this order

1. `DESIGN.md`: the design, its evidence and its plan. Every claim is tagged proved, measured or hypothesis.
2. `FEATURES.md`: what the editor must do, derived from Davy's two editors.
3. `CLAUDE.md`: the seed for the editor repo's CLAUDE.md (rules for every agent).
4. `PROJECT-INSTRUCTIONS.md`: the seed for the Claude project's instructions (how threads split and merge the work).
5. `reviews/REVIEW1.md`, `reviews/REVIEW2.md`: two independent reviews of the design (Fable 5.2), each finding reproduced, all applied to DESIGN.md. Their repro files are in `reviews/r1/` and `reviews/r2/`. The reviews name the old folders `native-editor/` (now this package) and `native-editor-review/` (now `reviews/`), and paths like `/home/claude/...` from the machine they ran on.

## Folders

| Folder | What |
|---|---|
| `specs/` | 8 TLA+ models, 35 configurations; each .cfg's first line says whether TLC must pass or fail it |
| `proofs/` | `Undo.lean`: undo inversion law and undo-tree jump, Lean 4.35 |
| `proto/` | the C and Rust piece tables, benchmark, CBMC and Kani harnesses, UTF-8 kernel with ACSL contract |
| `gui/` | SDL3 and raw Wayland prototypes, their measurements and how to run them |
| `org/` | the org parser comparison: scripts, outputs, measurements, corpus recipe |
| `ref/` | reference-editor driver and recordings: fake LSP server, tmux driver, Neovim/Helix/Emacs results, Neovim undo-tree recording and differential test, my-nvim keymap dump |
| `git/` | integrated magit: magit's git calls per refresh, the 5-process replacement, hunk staging times, the Emacs script that counted them |
| `reviews/` | the two reviews and their repro files |
| `env.nix` | the verification tool set (nixpkgs-unstable): TLC, Lean, Frama-C with Z3 and CVC5, CBMC, valgrind, Neovim, Helix, Emacs, tmux and more |

## Outside sources

- Davy's configs, the source of truth for behaviour: **my-nvim** (github.com/davysc/my-nvim, Neovim 0.12) and **kittymax** (the `kittymax/` folder of github.com/davysc/nixfiles, Emacs 31 with evil). They may be private; ask Davy for access. "The earlier study" in DESIGN.md is a kittymax performance study that estimated 3 to 5 person-years for a rewrite at full parity.
- Public: Neovim, Helix, Emacs/Org, VS Code's Lean 4 extension (the infoview), tree-sitter, orgize, SDL3, weston, foot, pstack (github.com/poteto/pstack, MIT).

## Reproduce

Tools: `nix build -f env.nix tools -o tools` (nixpkgs-unstable), then put `tools/bin` on PATH. Rust from rustup; Kani via `cargo install --locked kani-verifier && cargo kani setup`.

| What | Command |
| --- | --- |
| All TLA+ models (35 configs, including the review's Present.tla; each .cfg's first line says pass or fail) | `bash specs/check.sh` from a local disk (6 min 38 s on 4 cores; last result in `specs/check-result-2026-11-17.txt`) |
| Undo inversion law and undo-tree `jump` | `lean proofs/Undo.lean` (Lean 4.35) |
| Neovim's undo tree, recorded | `nvim --clean --headless -c 'luafile ref/undo/tree.lua'` (output: `ref/undo/nvim-undo-tree.txt`); differential test against Neovim: `python3 ref/undo/undo_diff.py 5000 30 2` (`MUT=firstchild|newest|nogotolast` shows it can fail); `undolevels.lua` shows Neovim's cap |
| Org parsers vs Org's own | `emacs -Q --batch -l org/el-sum.el corpus/*.org > el.json`; tree-sitter-org via `python3 org/ts-sum.py $PWD/org.so corpus/*.org`; orgize via `org/orgize09.rs` and `org/orgize010.rs` as `src/main.rs` of a cargo project. Corpus (rebuild with `org/corpus.sh`): Org's manual, guide and testing/examples, nvim-orgmode fixtures, `org/synthetic-agenda.org`. Results: `org/measured-2026-11-17.md` |
| GUI prototypes | `nix-build gui/env.nix -o guitools`; `gui/run.sh` builds and runs both prototypes (results in `gui/measured-2026-11-17.md`) |
| Benchmark inputs | `cd proto && python3 gen_input.py` (big.txt 100 MB, mid.txt 10 MB) |
| C piece table | `gcc -O2 -o bench proto/c/pt.c proto/c/bench.c && python3 proto/rss.py ./bench big.txt 50000` |
| Rust piece table | `cd proto/rs && cargo build --release` then `rss.py target/release/rsmin big.txt 50000`; `main_with_ropey.rs` (+ `ropey = "1"`) adds the `rope` mode |
| Instruction counts | `valgrind --tool=callgrind BIN mid.txt 5000` then `callgrind_annotate` |
| CBMC (runs out of memory) | `cbmc proto/c/cbmc_harness.c proto/c/pt.c -DN_ORIG=3 -DN_INS=2 --bounds-check --pointer-check --unwind 8 --unwinding-assertions` |
| Kani on the piece table (runs out of memory) | `cd proto/rs && cargo kani --harness insert_delete_roundtrip` |
| Frama-C on the UTF-8 kernel | `why3 config detect; frama-c -wp -wp-rte -wp-prover z3,cvc5 proto/u8/u8.c` |
| Kani on the UTF-8 kernel | `cd proto/u8rs && cargo kani` |
| Reference editors | `ref/scenario.py nvim|helix|emacs`, `ref/graph.py`, `ref/big.py`, `ref/idle.py`. They expect this package at `/opt/ne` (`ln -s $PWD /opt/ne`) and the tools on PATH. Results: `ref/out/` |

| my-nvim's keymaps | `ref/keymaps/dump.lua`, run inside Neovim with my-nvim loaded, writes `keymaps.tsv` (stored as `ref/keymaps/my-nvim-keymaps.tsv`) |

## Decisions by Davy (2026-11-17)

C; a window drawn by the editor by default, plus a terminal mode, on Linux (Hyprland only) and Windows (his work laptop, same fonts); undo is a tree, sized like Neovim's undolevels; org-mode is in, RSS is out; ligatures on (`->` as an arrow); org babel `:session` wanted; org-roam graph view nice to have; the Vim feature set derived from my-nvim and kittymax; development as a Claude project starting from an empty repo, agents working in parallel.

Git: integrated magit (Davy, 2026-11-17), package 17 in phase 4; evidence in `git/`.
