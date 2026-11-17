# Review: native editor design and formal models

Reviewed 2026-11-16/17 on Fable 5.2: the design doc (rev 15) and everything in `native-editor/`.
Everything below was run, not read. Reproduction files are in this folder (`native-editor-review/`).
Tools: nixpkgs-unstable from `env.nix` (TLC, Lean 4.35.0, Frama-C 33.0, CBMC 6.11.0, Neovim 0.12.6, Helix 25.07.1, Emacs 31.1), gcc 13.3, clang 18.1.3, rustc 1.97.0, Kani 0.68.0, 4 cores, 15 GB.

## Verdict

The measurements and model checks in the doc reproduce. The conclusions drawn from them are weaker than the doc says in six places, and two of the four TLA+ models cannot fail on the thing they are cited for. The Rust verdict is defensible but is not yet backed at the "proved or large body of measurements" bar: its three legs are a speed tie measured on a data structure that will not ship, a safety proof that does not cover the editor's main untrusted-input path, and a library advantage whose cost was not counted.

## What reproduced

| Claim in the doc | My run |
|---|---|
| `specs/check.sh`, 14 configs as expected, 2 min 51 s | 14 of 14 as expected, 3 min 13 s |
| `Undo.lean` checks, standard axioms only | passes; `undo_group`: propext, Classical.choice, Quot.sound; `redo_group`: propext, Quot.sound |
| callgrind 1,046 M / 1,039 M / 987 M (gcc / clang / Rust) | 1,047.7 M / 1,040.1 M / 987.9 M (on a text file I generated; the original input is not in the folder) |
| wall 5.7 s C, 5.2 s Rust, 1 run | 3 runs each: gcc 5.66 to 6.22 s, clang 6.38 to 6.71 s, Rust 5.66 to 5.94 s. Overlapping, a tie |
| RSS 196 / 197 MB | 196 / 197 MB |
| binary 14.5 KB / 14.6 KB / 346 KB | 14,480 / 14,632 / 345,736 bytes; ldd as stated |
| Frama-C WP 23 of 23, 2.8 s | 23 of 23, 2.8 s (needs z3, cvc5, why3, which `env.nix` does not list) |
| Kani UTF-8 kernel, 66 checks, 0.4 s | 0 of 66 failed, 0.36 s |
| CBMC and Kani run out of memory on the piece table | both did (12 GB cap). Two cheap rewrites of the C harness also did: concrete positions with symbolic text (`h2.c`), and typed loops instead of memmove (`pt_loop.c`) |
| ropey beats the flat table tenfold | 0.38 s vs 2.5 s here (6.5 times). 0.30 s of ropey's 0.38 s is building the rope, so the edits alone are about 30 times faster |
| Helix and Neovim LSP traces, undo, save, external change | reran `ref/scenario.py` for both: same messages, versions, undo result, save prompt |
| grapheme motion: Neovim and Helix whole cluster, Emacs splits | reran `ref/graph.py` for all three: same |

## Findings

Each has a tag check, a reproduction and a suggested change. Ordered by how much they change the design.

### 1. A simpler LSP mechanism passes both properties of the LspSync model, so the model does not justify the edit log

The model's properties are `Correct` (what is shown points at the right character) and `CatchUp`. I added a variant with no edit log at all: annotations already shown still shift through local edits, and an arriving diagnostic is shown only if its version is the current one, otherwise ignored. TLC: 5,808,775 distinct states, no error, safety and liveness.

Reproduce: `tlc -workers 4 -deadlock -config LspSync_dropstale.cfg LspSync_dropstale.tla` (2 min 33 s).

So the edit log, the mapping of old diagnostics and the pruning rule are code the stated properties do not need. They may still be worth having (diagnostics keep updating while you type during a long Lean elaboration), but that is a freshness property the model does not state, and a measurement nobody made (how long Lean's diagnostics are stale in practice). By the "no unneeded code" rule: either add that property and show the simple variant fails it, or delete the log.

Related: `Correct` is satisfied by an editor that shows nothing. The reply rule (show only if the buffer has not changed since the request) never shows anything while you type faster than the server answers. For goals that is fine. For completion it loses to all three reference editors, which filter the last list locally. That scenario is not listed.

### 2. "No editor can close this without a lock" is false in the model's own terms

`Save_ok_strong.cfg` fails for one mechanism. That shows the mechanism loses a write, not that every mechanism must. Counter-mechanism: swap temp and target atomically (`renameat2` with `RENAME_EXCHANGE`), then look at what came out. If it is not the version the editor loaded, the outside version is intact under the temp name and the user is asked.

Reproduce: `tlc -deadlock -config Save2_exchange.cfg Save2.tla` passes `NoLossAtAll` and `WholeAfterCrash` (262 states). `python3 xchg.py` does it on a real file system: after an outside replace in the window, the editor's text is in place, the outside text is preserved, and the inode comparison detects it.

Limit of my claim: this closes the window for programs that replace the file by rename, which is the only kind of outside write `Save.tla` models. A program that keeps a descriptor open and writes in place later is not covered. The doc's sentence should be tagged hypothesis, or replaced by this.

### 3. The Save model cannot fail on durability, and the design contradicts the model for hard links

In `Save.tla` nothing on the "ok" path ever sets `whole = FALSE`, and `Crash` leaves the disk as it is. So `WholeAfterCrash` would pass just as well for "write temp, rename" with no fsync at all, which is the known way to get an empty file after power loss. The two fsync calls in the design are not backed by the model (the doc says fsync is not modelled, but still tags the row "proved within bounds").

Reproduce: `Save2.tla` separates cached from durable data. `Save2_nofsync.cfg` violates `WholeAfterCrash` in 5 steps; `Save2_fsync.cfg` passes.

The design row also says "copy instead of rename for hard-linked files". That is the `inplace` variant, which the doc's own model rejects. The same path is forced when the directory is not writable, when the owner cannot be kept, and for bind-mounted single files. The design needs to say what happens then (Neovim writes a backup first).

Tested and not a problem: I rewrote a file in place with the same size 2,000 times; inode, size and mtime_ns differed every time on this kernel (6.18, ext4). The identity check held.

### 4. The Rust speed numbers are a benchmark-harness artefact; the piece-table code is identical in cost

Per-function callgrind counts:

| | gcc -O2 | clang -O2 | Rust |
|---|---|---|---|
| `split` (the hot loop) | 464,501,210 | 506,629,741 | 464,621,149 |
| libc memmove | 399,392,028 | 399,392,028 | 399,677,230 |
| benchmark `main` incl. the FNV hash | 120,684,898 | 75,690,950 | 68,328,971 |

The whole "Rust 5% fewer instructions" comes from how each compiler handled the benchmark's hash function. In the piece table itself gcc and Rust differ by 0.03%. The "8% fewer in the hot loop" compares Rust with clang; gcc was "not broken down" and equals Rust. `gcc -O3 -flto` gives the same 464.5 M.

Reproduce: `valgrind --tool=callgrind BIN t10.txt 5000`, then `callgrind_annotate`; build one copy with `gcc -O2 -fno-inline -g` to see `fnv` separately (120.0 M).

The doc already calls speed a tie, so the verdict stands, but the 5% and 8% figures should go. More important: 38% of the count is libc in both, and the rest is a linear scan in a structure the design rejects. Nothing was measured on tree code (allocation, pointer chasing, bounds checks), which is where the languages could differ. Tag should read "measured on the flat prototype; hypothesis for the tree".

### 5. The evidence in the Text row is for ropey, not for the piece tree the design picks

"Tree 0.23 s vs flat 2.2 s" measured the ropey crate, an existing rope used by Helix. No piece tree was built or measured. Under the novelty rule the proven option is ropey, and a hand-written balanced piece tree has to beat it with numbers. The only argument for the piece tree is memory, and my run puts the gap at about a tenth of the file size: with a 100 MB file and no edits the ropey harness peaked at 302 MB and the piece-table harness at 192 MB, both holding the input and one output copy, so the rope itself costs about 110 MB against about 100 MB.

Reproduce: add `ropey = "1"` to `proto/rs/Cargo.toml`, use `main_with_ropey.rs`, run `rsmin big.txt 0 rope` and `rsmin big.txt 0` under `rss.py`.

Also: building the rope from 100 MB took 0.30 s here, which is already the whole "open in under 0.3 s" target.

### 6. The Render model is true by construction and cannot express the real risk

`torn` is set exactly when `Draw` fires with `steps > 0`, and the "ok" guard is `steps = 0`. The invariant restates the guard (32 states). Commands have at most 2 steps, so the model cannot contain a long command, which is where "never draw inside a command" on one thread hurts.

Reference behaviour, run here:
- Neovim: an endless `:while 1 | endwhile` uses a full core; after Ctrl-C CPU drops to 0 and the next keys work (`intr3.py`).
- Helix: a macro run 100,000,000 times keeps a core busy after Ctrl-C and ignores input (`intr.py`).

The two editors disagree, the scenario is not in the sources-of-truth table, and the design does not say whether input is polled inside a command.

Two more loop problems the model hides:
- Blocking writes. "The editor never waits on a server" needs non-blocking writes with an output queue. With a plain blocking write of a large didOpen while the server is itself writing, both sides block forever. `python3 dead.py` shows it with a 1 MB write against a child that sends 200 KB first.
- The syntax row gives tree-sitter 5 ms per command on the main thread; the target is 2 ms keystroke to frame at p99. Both cannot hold unless parsing happens after the draw, which the design does not say.

### 7. "Undo is proved for all sizes" overstates what Lean proves

`undo_group` is a real proof of the inversion law for one group on lists. `redo_group` is its own hypothesis restated: after `inv_inv` the goal is `run es s = some t`, which is `h`. The rules that actually have bugs in editors (close the open group before undo, clear redo on a new edit) are checked only by TLC at 4 operations. Neither artefact touches the piece tree or the undo log saved to disk.

Missing scenario: the saved undo log is loaded after the file was changed by another program. Inverse inserts check only the position, so undo would apply to a text the edits were not made for.

Suggested wording: "the inversion law is proved for all sizes; the stack rules are checked to 4 operations".

### 8. Tags that do not hold

- "Memory safety by construction, proved (RustBelt)". RustBelt covers a core calculus and some standard-library types. The editor's main untrusted-input path is tree-sitter, whose runtime and generated parsers are C under either language choice. In the crates the doc names I counted `unsafe` occurrences: tree-sitter binding 454, memchr 333, aho-corasick 227, regex-automata 55, ropey 19, serde_json 17. Tag: proved for the language model, hypothesis for this dependency set.
- "regex: guaranteed linear time, proved by design". That is a documented guarantee, not a proof. Also not costed: Vim's pattern syntax differs from the regex crate's, so the planned differential tests against Neovim will fail on `/` and `:s` without a translator.
- "Frama-C proved a UTF-8 kernel for all inputs". The contract only bounds the result. A function that is just `return 1;` proves the same contract, 4 of 4 goals (`triv.c`). The function accepts overlong forms, surrogates and values past U+10FFFF (`ill.c`: C0 80 gives 2, ED A0 80 gives 3, F7 BF BF BF gives 4). Neovim also treats these as one character, so the behaviour matches the reference, but the comment says "well-formed" and such bytes cannot be sent to a language server as they are.
- "C wins on deductive verification". Not shown by this kernel: the function reads at most 4 bytes, so Kani's 8-byte bound is exhaustive too.
- "Go breaks the zero-wake-up idle target by design". Measured: a Go 1.24 program blocked on stdin, 5 threads, 0 context switches in 20 s (`goidle.go`). The garbage-collector argument may still hold; this one does not.
- "fact: crates exist" is not one of the three tags.
- "positionEncoding: hypothesis". Now settled: Lean 4.35's server does not return `positionEncoding` when offered utf-8 and utf-16 (`lsp_init.py`), so it speaks UTF-16 and the converter is required code.

### 9. "No unneeded code" was decided without counting

I built a 9-line program using the crates the doc names for LSP, JSON, graphemes, width, regex and tree-sitter (`deps-Cargo.toml`, `deps-main.rs`): 32 packages, 464,679 lines of vendored source (about 67,000 of them build-time only), a 1.99 MB stripped binary, 27 s clean build, 12.7 s to rebuild after a one-line change with the doc's release profile, 0.2 s with the dev profile.

This does not make C the winner. It makes the claim a hypothesis, and it already breaks "configuration is a source file; rebuild in a few seconds" for release builds. Options not compared: Rust with hand-written minimal LSP structs instead of lsp-types; a small JSON reader in either language.

### 10. The mmap rejection reaches the right answer for a weak reason, and exposes a gap

SIGBUS on truncation is real but can be handled. The stronger reason: an outside in-place write silently changes the "original" buffer under the piece table, with no signal (`mm.py`: the mapped bytes change from "original text" to "CLOBBERED by "). The memory mmap would save was never measured.

The argument "the editor dies with unsaved work" applies to every other way the editor can die, and the design has no crash recovery at all (no swap or journal file). With a Lean server at about 900 MB, an out-of-memory kill is a realistic scenario.

### 11. Other abstractions in LspSync that hide real cases

- Positions are single characters. Real diagnostics are ranges, and an insert exactly at a range boundary needs a rule.
- One document, one request at a time, no didOpen or didClose, no server restart. Lean users restart the file worker often; if versions restart, an old diagnostic has a version above the current one and is shown unmapped.
- `Undo.tla` says of server edits "its version already matched (LspSync)". LspSync has no server-edit action.
- The log is pruned only when a versioned diagnostic arrives; `version` is optional in publishDiagnostics, so for other servers the log grows without bound.

### 12. Smaller items

- Phase 2's gate says kittymax's PR 8 tests "pass unchanged". Those are ERT tests run inside Emacs (`pr8-review/t.sh`), and the bridge is Emacs Lisp. Both must be rewritten, and something must host the webview, which meets the "no GUI toolkit" non-goal.
- Missing scenarios: inotify when another program replaces the file by rename (the watch follows the old inode) and telling the editor's own save apart from an outside one; invalid UTF-8, CRLF, BOM and missing final newline surviving a save byte for byte; terminal resize and Ctrl-Z; a 10 MB paste as one command; fsync on a slow or network disk (100 MB took 0.08 s here, on one thread).
- Reproducibility: the README gives no commands for the benchmark, callgrind, CBMC, Kani or Frama-C; `env.nix` lacks z3, cvc5, why3 and valgrind; the 10 MB and 100 MB inputs are not in the folder; `ref/*.py` hard-code `/home/claude/...`; the outputs of `graph.py`, `big.py` and the idle measurement are not stored, and the idle measurement has no script.

## On C vs Rust

Nothing I ran favours C on speed or Rust on speed. What the doc has for Rust is: safety that is real for the editor's own code but not for tree-sitter; libraries that save writing but add 465,000 lines and seconds of build; a verification story (Verus, Creusot) that is untried. What it has for C is a 14 KB binary and Frama-C on one small function with a weak contract. The doc's own tipping test, whether Verus or Frama-C can reach the tree, has not been run for either. I would keep Rust as the working choice and tag the verdict hypothesis until phase 0 has the same tree in both with a contract that says what the result is.
