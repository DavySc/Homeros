# Native editor: design and formal models

Nov 16, 2026 · @Davy

## Summary

**Working choice:** C (your call, 2026-11-17; nothing measured argues against it), a window drawn by the editor itself by default with a terminal mode, the same on Linux and Windows, Vim keys, a rope in memory, an undo tree, one event loop with no threads, Lean first and org-mode after. The first useful version (Lean editing with goals and widgets, on both systems) is about 8 to 13 person-months; org adds an estimated 4 to 8. Full kittymax parity is still the 3 to 5 person-years of the earlier study. Development runs as a Claude project, one thread per work package. Two Fable 5.2 reviews reproduced the measurements: the first found 12 problems, the second (of the 2026-11-17 changes) found 8, and all of them are applied below.

Every claim below carries one of three tags, per your evidence bar: **proved** (a formal proof or an exhaustive model check), **measured** (runs recorded in this session, with the count), or **hypothesis** (not yet backed; to be settled in the named phase).

- **C costs nothing in speed.** In the piece table's hot loop gcc and Rust executed 464.50 M and 464.62 M instructions, a 0.03% difference; 5 alternating timed runs overlap (measured, reproduced by the reviewer). C leads on binary size (14 KB vs 346 KB) and rebuild time (0.15 s vs 3.9 s for the same prototype without dependencies, review). Rust's lead, memory safety by construction, has to be replaced by Frama-C proofs of the kernels, sanitizers, fuzzing and differential tests. Frama-C proved a full result contract for the UTF-8 kernel once its bit tricks were rewritten as comparisons (32 of 32 goals, review); a proof of the rope is untried.
- **The data structure matters far more than the language.** ropey did 50,000 random edits on 100 MB in 0.23 s; the flat piece table took 2.2 s in Rust and 2.7 s in C (measured). C has no ropey, so the rope is written by hand and must match ropey on your files before it is accepted.
- **The editor draws its own window, on the compositor's beat.** A raw Wayland client used 1 thread, 0 wake-ups in 20 s idle and 11.7 MB; the same window through SDL3 used 11 threads, 7 wake-ups and 137 MB, because SDL3 cannot draw on Wayland without a GL driver (measured, 3 runs, no GPU). The review showed that drawing whenever a buffer is free either waits up to a refresh (p99 16.6 ms, weston pixman) or draws 1,950 frames for 2,000 keys, most never shown (weston GL). So the editor draws only after the compositor's frame callback, which a TLA+ model shows wastes no frame with either kind of compositor. Lean needs font fallback and HarfBuzz shaping: JetBrains Mono lacks ⦃ ⦄ ᵢ 𝓝 and 11 more common Lean symbols (measured).
- **Undo is a tree, and behaves like Neovim 0.12.** Its rules match Neovim on 5,000 random sequences of 30 edits, u, Ctrl-R, g-, g+ and :undo N (review, rerun here on 500); the test catches a wrong Ctrl-R rule in 278 to 1,358 of 5,000. TLC checks that every move lands on the right text and loses nothing; Lean proves any jump exact for all sizes. Like Neovim, the tree keeps the last 1,000 changes by default.
- **Org gets its own C parser, checked against Org's.** No existing parser was good enough: tree-sitter-org disagrees with Org on tables, lists and timestamps; orgize 0.9 loses the drawer under any repeater; orgize 0.10-alpha loses named tables and the drawer under a habit's .+1d/3d (measured, each reduced to a two-line case). The parser's gate is your real \~/org compared by position plus random org text, since the 35-file corpus missed one of those bugs. Export and babel run in one Emacs started on demand.
- **Nine TLA+ models check timing and ordering.** All 43 configurations give the expected result: the adopted mechanisms pass within their bounds and every rejected one fails with a counterexample.
- **Bounded model checkers did not reach the text store.** CBMC 6.11 and Kani both ran out of memory (12 to 15 GB) on a 3-byte piece table. The store is covered by differential fuzzing against a plain array, with a proof attempted as a spike.
- **Neovim, Helix and Emacs were run, not just read.** Their LSP traffic, undo, save method, external-change handling, Unicode motion and Ctrl-C behaviour are recorded and become the conformance tests.

Specs, proofs, prototypes, recordings, reviews and the feature list are in the design package that goes with this document (its README.md says how to rerun each). Every path in this document is relative to that package.

## Goals and targets

The editor is for one person, so every feature must be one you use, and every line must serve one. The top rule is yours: feature complete with maximum simplicity, no unneeded code, no wasted cycle. A new approach is adopted only when it beats every combination of existing ones in every scenario we can list, with proof or a large body of measurements; otherwise the proven option wins.

| Target | Value | Reference measured here |
| --- | --- | --- |
| Frame ready after a key (or after the compositor's frame callback, if a key came while waiting) | under 2 ms, p99 | the prototype redraws the whole 1600x1000 window in 0.66 to 1.21 ms on the CPU (measured, review) |
| Key to screen | within one display refresh | prototype without frame callbacks: p99 16.6 ms on weston pixman, 1.9 ms on weston GL but drawing 1,950 frames for 2,000 keys (review) |
| First frame committed after start (window) | under 10 ms; on screen at the compositor's next repaint | prototype 5.0 to 5.8 ms committed, on screen 18.0 and 22.6 ms; SDL3 74 to 86 ms committed (measured, review) |
| Frames drawn that are never shown | 0 | Present.tla: frame callbacks pass, drawing on a free buffer fails with GL compositors and hidden windows |
| Idle CPU | 0 wake-ups in 20 s with no server running | Neovim 1, Helix 0, Emacs 1, raw Wayland 0, SDL3 7 (measured) |
| RSS at start, no file | under 15 MB | raw Wayland prototype 10.7 to 11.7 MB without font fallback, Helix 19 MB, my-nvim 30 MB, kittymax 286 MB |
| RSS with a 100 MB file | under 1.1 x file size + 15 MB | Neovim 135 MB, Helix 130 MB, Emacs 141 MB (measured) |
| Open a 100 MB file | under 0.3 s | Helix 0.34 s, Neovim 0.45 s (measured, includes tmux); building a ropey rope alone took 0.30 s (review), so this target is at risk |
| Edits on a 100 MB file | O(log n) per edit | ropey 50k edits in 0.23 s; flat piece table 2.2 s (measured) |
| Week agenda over your org files | under 20 ms (hypothesis) | Emacs 0.49 to 0.59 s over 600 headings (measured, review) |
| Lean server load | never more than one per project | kittymax bug fixed in PR 6 (two servers raced at start-up) |

In scope since 2026-11-17, by your decision: org-mode, an undo tree, Windows, and a window of its own. The full feature list, derived from my-nvim and kittymax, is FEATURES.md in the design package. Not goals: a plugin or scripting system (configuration is source code; changing it means a rebuild), mail, RSS (your call), macOS, and a GUI toolkit (the editor draws its window itself, see Architecture). Git is in, as an integrated magit (your call).

## Sources of truth

The three editors disagree on most of what was measured, so "behave like X" has to name X per behaviour. Neovim 0.12.6, Helix 25.07.1 and Emacs 31.1 (`-nw -Q`, eglot) were driven in tmux with real keystrokes against a fake language server that logs every message with its time (`ref/scenario.py`, one run each).

| Behaviour | Neovim | Helix | Emacs | Chosen |
| --- | --- | --- | --- | --- |
| didChange timing | batched, about 150 ms after typing (3 keys, 1 message) | one message per key, at once | batched on an idle timer (about 0.5 s) | Helix: one per command; the server does its own debounce |
| Versions sent | jump by more than 1 (changedtick) | +1 per change | jump | +1 per change |
| Hover, then an edit 200 ms later | no `$/cancelRequest` | none | none; also hovers on every idle | cancel the old request, drop a stale reply (LspSync model) |
| Undo after `o` + typing "hello world" | one step removes the line and the newline | one step, same | one step removes the words, the newline needs a second | Neovim: an insert session is one step |
| Save | new inode (rename) | new inode | new inode (rename, backup file) | temp file, fsync, rename (Save model) |
| File changed on disk, then edit and save | asks y/n at save | refuses: "use :w! to overwrite" | asks at the first edit | Emacs's early warning plus Helix's refusal at save |
| Cursor right / delete over e + combining accent, family emoji (ZWJ), flag, CJK | whole grapheme each time | whole grapheme each time | one code point: splits the emoji and the accent | Neovim and Helix (grapheme clusters, Unicode 16 rules) |
| Open 100 MB text file | 0.45 s, 135 MB | 0.34 s, 130 MB | asks first, 141 MB | read into memory (see Architecture: why not mmap) |
| Idle for 20 s | 1 context switch | 0 | 1 | 0 |

Two more rows came from the review's runs. **A runaway command:** Neovim stops an endless `:while` loop on Ctrl-C and takes keys again; Helix keeps a core busy and ignores input after Ctrl-C during a 100-million-repeat macro. Chosen: Neovim. **Completion while typing:** the reviewer reports that all three filter the last completion list locally rather than wait for a fresh reply (not run here; hypothesis), so completion is the one request where a stale reply is reused (filtered, never re-positioned).

The other two sources were not run here. **VS Code** defines Lean behaviour: the infoview (goals, widgets, traces), Unicode abbreviations (`\alpha` to α), file-progress bars. The editor reuses VS Code's own infoview through the bridge kittymax already ships (`kitty-lean-webview.el`, tested in PR 8). **SQL Server Management Studio** defines the SQL client, from [Microsoft's query-editor docs](https://learn.microsoft.com/en-us/ssms/f1-help/database-engine-query-editor-sql-server-management-studio) and [Redgate's shortcut list](https://www.red-gate.com/simple-talk/databases/sql-server/tools-sql-server/ssms-the-query-window-keyboard-shortcuts/): F5 runs the selection or, with none, the whole buffer; Ctrl+F5 parses the same; Alt+Break cancels; Ctrl+D, Ctrl+T, Ctrl+Shift+F send results to grid, text or file; Ctrl+R hides the results pane; Ctrl+L and Ctrl+M show the estimated and actual plan. GO batches and the Messages tab are not described on those pages and still need checking against a real SSMS.

How conformance is kept: each row above becomes a recorded trace, and the new editor must produce the same trace. For Vim keys the oracle is Neovim itself: random key sequences from the supported subset run in `nvim --headless` and in the new editor, and the buffer, cursor and registers must match. That gives the large body of tests the evidence bar asks for, at no cost per test.

**Undo tree, recorded in Neovim 0.12.6** (ref/undo/tree.lua, headless, real keys). u goes to the parent. Ctrl-R goes to the child visited last, so u then Ctrl-R always returns; after :undo 2 it follows the branch :undo walked down. An edit after u starts a new branch and keeps the old one. States are numbered in creation order across branches; g- and g+ step through those numbers, :undo N jumps to state N, :earlier and :later stop at the ends. A cursor move inside insert mode (Left) splits the insert into two undo steps, and Ctrl-G u does the same on purpose. You use the tree in both editors: my-nvim has :Undotree with undofile on, kittymax has vundo with undo-fu-session, so the tree is also saved across restarts.

**Org.** Org's own parser (org-element, Org 9.7 in Emacs 31.1) is the reference for what a file means; Emacs with your kittymax org config is the reference for behaviour (agenda, capture, refile, logging, clocking, habits, export). nvim-orgmode, which my-nvim uses, shows the same split works: 25,700 lines of Lua for the editing side, with export handed to Emacs or pandoc.

## Architecture

One process, one thread, one event loop: poll(2) on Linux, overlapped I/O on Windows. Everything else is a child process on a pipe: the language servers, the Lean infoview webview, one Emacs started on demand for org export and babel, a terminal's shell, git. The core turns the buffer into a grid of cells; a backend shows the grid in a Wayland window, a Windows window or a terminal. Each part below says what it is, what existing program it copies, and what backs the choice.

```
                 +---------------------------+
                 | Window (Wayland or Win32) |
                 | or terminal               |
                 +-------------+-------------+
                               | keys in, pixels or cells out
+------------------------------v------------------------------+      +----------------------+
| Editor process: one thread, one event loop                  |<---->| Files on disk        |
|                                                             |      | temp, fsync, swap    |
|  +----------------------+     +--------------------------+  |      +----------------------+
|  | Vim keys             |---->| Text: rope, in C         |  |      +----------------------+
|  | operators, motions,  |     | byte offsets, graphemes  |  |<---->| Infoview (browser)   |
|  | ex commands          |     +------------+-------------+  |      | localhost, on demand |
|  +----------------------+                  |                |      +----------------------+
|  +----------------------+     +------------v-------------+  |      +----------------------+
|  | Screen diff          |<----| Undo tree                |  |<---->| lake serve, other    |
|  | changed cells, after |     | branches kept, saved per |  |      | LSP servers (stdio)  |
|  | the frame callback   |     | file                     |  |      +----------------------+
|  +----------------------+     +--------------------------+  |      +----------------------+
|  | tree-sitter          |     | LSP client               |  |<---->| Emacs (org export,   |
|  | incremental, after   |     | stale results ignored    |  |      | babel), on demand    |
|  | the frame            |     |                          |  |      +----------------------+
|  +----------------------+     +--------------------------+  |
+-------------------------------------------------------------+
```

The editor process never waits on a server, a file or the webview: it reacts to whichever pipe becomes readable, then draws.

| Part | Design | Copied from | Backed by |
| --- | --- | --- | --- |
| Language | C17 with gcc and clang, -Wall -Wextra -Werror; ASan and UBSan in every test run; Frama-C result contracts on the kernels (UTF-8, graphemes, UTF-16); no bit tricks in proved code | Neovim, vis, foot | measured: C ties Rust on instructions, 14 KB vs 346 KB, 0.15 s vs 3.9 s rebuild like for like; Frama-C 32 of 32 on the UTF-8 kernel written as comparisons, 24 of 28 with shifts (review) |
| Text | a balanced rope (B-tree of chunks, like ropey) written in C, holding the file in memory; a flat piece table is ruled out | Helix (ropey), xi | measured: ropey 0.23 s vs flat piece table 2.2 s for 50k edits; the C rope must match ropey on your files; a Frama-C proof of it is a spike, with differential fuzzing as the fallback that may pass the gate |
| Positions | byte offsets inside; UTF-16 at the LSP boundary, converted per line | Helix | measured (review): Lean 4.35 does not accept UTF-8 positions |
| Cursor motion | extended grapheme clusters; width from Unicode East Asian Width; tables generated from Unicode 16 data | Neovim 0.12, Helix | measured: both agree on 4 test clusters, Emacs does not |
| Editor core | buffers, windows and splits, scrolling and wrapping, status and command lines, completion popup; turns text into the cell grid | Neovim | hypothesis: conformance traces against Neovim |
| Undo | a tree: each node holds one group (an insert session, ., a macro or a server edit) as edits from its parent, a timestamp and a save mark; u, Ctrl-R, g-, g+, :undo N, :earlier and :later (by count, time or save) as in Neovim; a jump walks up to the common ancestor, then down; the oldest changes beyond undolevels (default 1,000, as Neovim) are dropped; the tree is saved per file with the file's hash | Neovim 0.12 (undotree, undofile), vundo, undo-fu-session | proved: Lean jump and undo\_group, all sizes; TLC: every move exact, nothing lost within the cap (UndoTree, 4 edits); Neovim differential test: 0 of 5,000 sequences differ, and it is the only check that catches a wrong last-visited rule |
| Crash recovery | append each command's edits to a journal next to the file, fsync at most once per second; replay it on next open | Vim's swap file | hypothesis: design only |
| Main loop | wait for input, pipes and one timer; run at most B queued commands; draw only when the screen changed and the compositor's frame callback has come; then do deferred work (parsing); never draw inside a command; poll for Ctrl-C inside long commands | Neovim, Helix, foot (frame callbacks) | TLC: Render (between commands) and Present (frame callbacks waste no frame with either kind of compositor and draw nothing while hidden) and Break (Ctrl-C ends an endless command only if input is read inside it, also when the key queue is full: ok passes; nocheck, full fail); Ctrl-C measured in Neovim |
| Pipes | every write to a child is non-blocking, through an output queue; on Windows, overlapped named pipes, since anonymous pipes cannot be waited on | Neovim (libuv does the same on Windows) | measured (review): a blocking 1 MB write deadlocks; TLC: Pipe (queue passes, block fails) and Exit (the exit is reported at the child's exit after its output, and no signal reaches a reaped pid: ok passes; waiteof, early fail); the Windows half is a hypothesis |
| Screen | the core produces a cell grid and diffs it against the last frame; backends draw only changed cells | Neovim's grid, foot | Render model; prototype numbers below |
| Window, Linux | a Wayland client with no toolkit: xdg-shell (taking the size the compositor configures), wl\_shm buffers drawn on the CPU, frame callbacks, output scale, pointer and cursor shapes, xkbcommon keys with client-side repeat, text-input-v3 for IME, wl\_data\_device for the clipboard; glyphs from FreeType, fallback fonts found with fontconfig, clusters shaped with HarfBuzz | foot, which links pixman, fontconfig, fcft, HarfBuzz and utf8proc | measured: 1 thread, 0 idle wake-ups, 11.7 MB without fallback fonts; the rest is hypothesis until package 6 |
| Window, Windows | a Win32 window drawing the same cell grid into a DIB section with the same FreeType and HarfBuzz code; fallback fonts from a fixed list checked per glyph; IMM32 for IME; one message loop with MsgWaitForMultipleObjectsEx | the Wayland backend's twin | hypothesis: built so far (package 9) are the window, keys, mouse, clipboard, the grid in a DIB section with GDI glyphs (FreeType, HarfBuzz and the fallback list come with package 7), and frame pacing at most once per refresh plus P/8, as no frame callback exists (Pace.tla); IME is DefWindowProc's, untested. Tested under Wine (measured); phase 1 measures it on your machine |
| Terminal mode | the same grid as escape sequences, changed cells only, synchronized update (CSI ? 2026) | Neovim, Helix | hypothesis: for SSH and quick edits |
| Syntax | tree-sitter, incremental, after the frame is drawn; colours lag one frame at most | Helix, Neovim | hypothesis: measure in phase 0 |
| LSP | incremental sync, one didChange per command, versions +1; ignore stale diagnostics; a reply is shown only if the buffer has not changed since; completion lists filtered locally; on restart clear and resend didOpen | Helix, VS Code | proved within bounds: LspSync "simple" |
| Lean | lake serve, Lean's extensions, hasWidgets and incremental diagnostics on, one server per project | kittymax, lean.nvim | measured in kittymax: 420 MB of JSON without incremental diagnostics |
| Infoview | text goals in a split; for widgets, VS Code's infoview in a browser on localhost, served by a C port of kittymax's HTTP+SSE bridge | lean.nvim, VS Code | measured in kittymax PR 8; bridge tests must be rewritten |
| Org | see Org below: own parser, native agenda, capture, refile, logging, clocking, habits; export and babel through one Emacs started on demand | kittymax, nvim-orgmode | measured parser comparison and Emacs timings |
| Git (magit) | magit's status buffer, sections and keys, drawn by the editor: a refresh runs 5 git processes (git --no-optional-locks status --porcelain=v2 -z --branch, log, diff, diff --cached, stash list); a hunk is staged with git apply --cached and unstaged with -R, so git itself refuses a hunk that no longer matches; writes run one at a time; the buffer is read again after each write and when .git or the work tree changes; gutter signs from the same diff | magit with evil-collection keys (kittymax), neogit (my-nvim) | measured: magit runs 52 git processes and about 50 ms per refresh, the 5 above take 10.5 to 12.6 ms, staging a hunk 3.6 to 5.4 ms (5 runs each); plain git status takes index.lock, --no-optional-locks does not; TLC: Git (ok passes; parallel, blind, nowatch fail) |
| Save, Linux | identity check; temp file in the same directory, fsync, swap atomically (renameat2 RENAME\_EXCHANGE), check what came out; fsync the directory | new combination, found in review | proved within bounds: Save2 |
| Save, Windows | identity check; temp file, FlushFileBuffers, then ReplaceFileW, which keeps the original's attributes and ACLs; the Save model's rename mechanism, not Save2's swap, since Windows has no exchange call | Vim and VS Code on Windows | hypothesis: the Save model (rename) covers the logic; Windows file-system behaviour is measured in phase 1 |
| Save, fallback | hard links, read-only directories: copy the original to a backup, then write in place | Neovim's backupcopy | Save model's inplace variant shows the risk |
| External change | inotify on Linux, ReadDirectoryChangesW on Windows, both on the file's directory; ignore our own save; warn at the first edit after a change, refuse a plain save over it | Emacs + Helix | measured: their behaviour above |
| Keys | Vim grammar, registers, ., macros, the ex commands you use; leader keys from my-nvim; Vim's regex engine semantics (backtracking with a step limit, as Neovim) | Neovim, my-nvim | measured per release: differential tests against nvim --headless (with swapfile off, or each new buffer waits 1 to 2 s) |
| Configuration | a C source file, rebuilt on change | dwm, suckless | hypothesis: 0.15 s measured for a 150-line prototype, not for the editor |

**Why not mmap the file.** vis does it and it could save memory on big files, but the saving was never measured, and it fails a scenario the review ran: when another program writes the file in place, the mapped bytes change under the editor with no signal (`mm.py` shows "original text" turning into "CLOBBERED by"), silently corrupting the buffer. SIGBUS on truncation is a second, handleable, problem. All three reference editors copy the file into memory. By your rule the proven option wins.

**Why no threads.** Every part above fits in a loop that blocks in `poll`, and the slow work (type checking, parsing big projects) already runs in other processes. A thread would add locking for no measured gain. If tree-sitter parsing of a large file breaks the frame budget, that is the first place to revisit, with a measurement.

**Why the editor draws its own window.** You asked for a window by default, with the same experience on Windows and Linux. Three routes were possible: a toolkit (GTK, Qt), a portability layer (SDL3, GLFW), or a thin backend per system with shared drawing code. A toolkit fails the minimalism rule outright. SDL3 was built and measured against a raw Wayland client doing the same job (gui/ in the project files, weston headless, 200 keystrokes, 3 runs each; the reviewer's reruns in brackets):

|  | SDL3 + SDL3\_ttf | Raw Wayland + FreeType |
| --- | --- | --- |
| Our code | 48 lines | 109 lines |
| Libraries loaded | SDL3 4.8 MB, HarfBuzz 1.8 MB, FreeType 1.0 MB, SDL3\_ttf, plus Mesa | libwayland-client, xkbcommon, FreeType 1.0 MB |
| First frame committed after start | 74 and 82 ms, 458 ms cold (83 to 86) | 5.0 to 5.6 ms (5.3 to 5.8) |
| Full redraw | 3.05 to 3.31 ms, GL on llvmpipe (3.42 to 3.56) | 0.66 to 0.89 ms on the CPU (1.03 to 1.21) |
| Threads | 11 (11) | 1 (1) |
| Wake-ups in 20 s idle | 7 (7) | 0 (0) |
| Memory | 137 MB (128) | 11.7 MB (10.7) |

SDL3 has no software path on Wayland ("Window framebuffer support not available"), so it always loads a GL driver with its threads and memory; on your NVIDIA machine the numbers will differ, but the threads and the driver stay (hypothesis). It also crashed under a compositor without a seat until its GTK decoration plugin was disabled. The prototype's "first frame" is when its commit is queued; weston showed it 18.0 and 22.6 ms after start, and SDL3's number has the same caveat, so the comparison stands.

**When to draw (from the review).** The prototype drew whenever one of its two buffers was free. On weston's CPU renderer that holds a buffer until the next repaint, so a key waits up to a refresh (p50 9.4 ms, p99 16.6 ms); on weston's GL renderer, like Hyprland's, buffers come back at once and the prototype drew 1,950 full frames for 2,000 keys at 1 ms, about 1,830 never shown. The fewer frames I had called the client's coalescing were the compositor's. Present.tla (specs/, 9 configs) shows the trade-off is in the goals: no rule both commits every key before the next arrives and never draws an unseen frame. The design takes the frame callback, as foot does: draw when the compositor says it will show the result, so no frame is wasted and a hidden window draws nothing, and measure latency as frame-ready time plus one refresh.

The raw client is longer, and the backend still needs font fallback, HarfBuzz shaping, the configured size, output scale, a pointer and cursor shapes, IME, clipboard and key repeat (a Wayland client repeats keys itself), an estimated 2,500 to 4,000 more lines for both backends (hypothesis). That is the price of 0 wake-ups and about 12 MB. foot, a widely used Wayland terminal, draws on the CPU into shared memory with the same libraries (fontconfig, HarfBuzz), so this copies foot, not a new approach.

**Fonts.** FreeType draws the glyphs on both systems, with JetBrains Mono first. Font fallback is required: JetBrains Mono (the Nerd Font build) has no glyph for 15 of 89 common Lean and Mathlib symbols, among them ⦃ ⦄ ᵢ 𝓝 𝒰 ⁿ ⥤ 𝟙 (review; ⦃, ᵢ and 𝓝 rechecked here with hb-shape). On Linux fontconfig finds the fallback font, as in foot; on Windows a short fixed list is searched per missing glyph. Shaping is required too: q with a combining tilde has no precomposed glyph, and HarfBuzz places the mark; a ZWJ emoji is one glyph only after shaping. Glyphs are cached per font and cluster, so a line that did not change is never shaped again. Ligatures then cost only a feature switch in HarfBuzz, and they are on: you asked for -> drawn as an arrow.

## Org

Org is the largest single item, so the design splits it by what has to be fast and what has to be exactly Org's. Editing, agenda, capture, refile and clocking run inside the editor on its own parser. Export and babel run Org itself, in one Emacs that the editor starts when an org job needs it: they are slower jobs where Org's exact output matters more than speed, and rewriting them is most of the 12 to 24 months the earlier study counted. That split is the one nvim-orgmode made, and it is what my-nvim already uses.

**Parser, measured.** Three existing parsers were run against Org's own on a corpus of 35 files, 32,336 lines: Org's manual and guide, Org's own test examples, nvim-orgmode's fixtures and a 600-heading file with your keywords, tags and drawers (its weekday names and clock times are not realistic). org/corpus.sh rebuilds the corpus. Counts of headings, TODOs, tags, blocks, tables, timestamps, property drawers and lists were compared per file.

| Element (files agreeing with Org, of 35) | tree-sitter-org | orgize 0.9 | orgize 0.10-alpha |
| --- | --- | --- | --- |
| headings, TODO keywords, tags | 34, 35, 35 | 35, 35, 35 | 35 |
| source and example blocks | 35, 35 | 34, 34 | 35, 35 |
| tables | 33 | 35 | 34 |
| property drawers | 33 | 33 | 35 |
| plain lists | 33 | 34 | 34 |
| error nodes | 69 in 2 files | none | none |
| real misparses, each reduced to two lines | tables 197 vs 143, lists 630 vs 802 (not reduced) | a planning line with any repeater (+1d, ++1w, .+1d) is not read, so the drawer under it is lost | a table under #+NAME, #+CAPTION, #+TBLNAME or #+ATTR\_HTML is not a table; the drawer under a habit's .+1d/3d is lost |
| whole corpus | 0.77 s | 0.027 s | 0.036 s |

Timestamp counts differ for all three for a counting reason, not a parse error: orgize counts the timestamps on planning and clock lines, Org's element map does not (348 + 404 + 170 = 922, exactly orgize 0.10's count). Each failure above hits something you use daily (habits, named tables for babel), and orgize is Rust. So the parser is written in C. The review showed the corpus is too thin to accept it: orgize 0.10 scored 35 of 35 on drawers and still loses the one under .+1d/3d, which the corpus has only inside an example block. The gate is therefore your real \~/org compared with Org by position, not by count, plus random org text fed to both, plus the reduced cases above as fixed tests. The parser reparses only the heading subtree an edit touches (hypothesis that this meets the 2 ms budget; measured in phase 5).

**Emacs as the export and babel backend, measured** (Emacs 31.1, emacs -Q, 600-heading file; reviewer's reruns agree): starting Emacs in batch and loading the HTML exporter takes 0.22 to 0.25 s; a cold batch export to HTML takes 1.0 to 1.3 s. A running Emacs first looked slower (1.3 to 1.9 s), but that was its garbage collector, which batch mode runs less often: with gc-cons-threshold at 100 MB the review measured 0.74 to 0.81 s and I measured 0.45 to 0.53 s after the first job. A running Emacs also keeps babel state between blocks, which one batch job per block loses (the review's two-block elisp case gives void-variable). So the editor starts one Emacs on the first org job, with your export settings and the higher threshold, talks to it over a pipe, and stops it after some minutes without org jobs, unless a :session block still holds state, so idle wake-ups stay at 0 when you are not using org. The buffer is sent as text, not read from the file, and results come back as text that the editor applies as one undo group; Emacs never writes your file, which keeps it out of the Save model's outside-writer case. Start-up with your full kittymax org settings (xelatex, the docx backend, htmlize, ox-gfm, dbbliss) is not measured yet. One trap: a job that hits a broken link waits for an answer forever, so every job gets no input, a timeout, and broken links marked rather than fatal.

| Your kittymax feature | In the editor | Backed by |
| --- | --- | --- |
| Agenda over \~/org | native: an index of headings with planning, updated per file on change | Emacs 0.49 to 0.59 s for a week agenda over 600 headings (measured); target under 20 ms (hypothesis) |
| TODO, NEXT, WAIT, DONE, CANCELLED with logging into a drawer | native; behaviour recorded from Emacs with your config | recorded traces, as for Vim keys |
| Capture (inbox, notes, journal datetree), refile | native | recorded traces |
| Clocking (persisted), clock table, habits | native; clock table output compared with Org's | Org as the oracle |
| Babel (C, C#, SQL via dbbliss, shell, Python, elisp, dot) | the org Emacs runs the block; state kept between blocks while it runs; results applied as an undo group | exact Org semantics; whether your files use :session is an open question |
| Export HTML, PDF, docx, GitHub Markdown (ox-gfm) | the org Emacs with your export config | 0.45 to 0.81 s per HTML export once running (measured, review) |
| Table of contents (toc-org) | native: the TOC heading is rewritten on save | hypothesis |
| Images (org-download) | pasted or dropped images saved next to the file and linked; drawn inline in the window | hypothesis |
| LaTeX previews | rendered by latex or tectonic to an image, drawn inline in the window | hypothesis |
| org-roam | IDs and links indexed natively, backlinks in a split; no SQLite | hypothesis; graph view left out unless you want it |
| org-modern, org-appear look | the renderer draws bullets and hides markup except on the cursor line | hypothesis |

## Formal models

Nine TLA+ models cover the parts where timing or ordering decides correctness. Each states its property before its mechanism, and each configuration file says on its first line whether TLC must pass or fail it. specs/check.sh runs all 43 and confirms each (2026-11-17, all as expected; 26 must fail and do). Guarantees hold only within the stated bounds.

| Model | Property | Bound, states | Adopted mechanism | Variants that must fail (shortest trace) |
| --- | --- | --- | --- | --- |
| LspSync | anything shown from the server points at the character it meant; once editing stops, diagnostics catch up | 3 edits, about 5.8 M states | "simple": ignore stale diagnostics, no edit log (passes). The logged "ok" variant also passes, so the log is unneeded code | show raw positions (6 steps); drop the log on send (6); map a reply from its request version (6) |
| Undo | undo lands exactly on the text before the group, redo on the text after | 4 operations, 76,179 states | close the open group first; clear redo on a new edit | keep redo (4 steps); undo with a session open (3) |
| UndoTree | every move (u, Ctrl-R, :undo N, g-, g+) lands on exactly the text its target had; no text is lost within the size cap; Ctrl-R right after u returns to where u left | 4 edits, 256,801 states, 2 min 8 s | each node stores its edit from the parent; a jump walks up to the common ancestor, then down | plain linear undo: a text is lost (5 steps); Ctrl-R to the oldest child (6); a jump applying only the target's edit (3) |
| Render | no frame shows a command half-done; every change reaches the screen at any input rate | queue 3, batch 2, 32 states | draw between commands, at most B commands per frame | drain all input first: screen starves (lasso); draw mid-macro (6) |
| Present (review 2) | no frame is drawn that the compositor never shows; once typing stops, the screen shows the last key | 4 keys, both kinds of compositor, visible and hidden | draw only after the compositor's frame callback | draw whenever a buffer is free: wasted frames with a compositor that releases at commit, and while hidden; no rule also commits every key before the next (Prompt fails for both) |
| Pace (package 9) | a GDI window on Windows, which has no frame callback, draws no frame that DWM never shows; once typing stops, the screen shows the last key, the loop being woken by the backend's timer | 4 keys, P 3, composition slip D 0 or 1, visible and minimised, a timer that may fire a tick early; specs/pace-sweep.sh also runs P 2 to 4, D 0 to 1 and every gap from 0 to P + D + 2 (78 runs): NoWaste passes exactly when the gap exceeds P + D, Fresh at every bound | draw a frame only when more than P + D has passed since the last (the code waits P + P/8), never while minimised; a waitable timer wakes the loop when the gap has passed | draw whenever there is something new (WM\_PAINT style); frames exactly P apart, since a draw and a composition in the same instant may come in either order; frames P + D apart; drawing while minimised; no timer (Fresh fails: the last key waits for the next input) |
| Save | no torn file after a crash; an outside change made before save is never overwritten unasked | 2 outside writes, 1,912 states | check, temp, rename | write in place: torn file; skip the check: change lost; stronger property "no outside change ever lost" fails for this mechanism |
| Save2 (review 1) | the stronger property, plus no torn file when data can sit in the page cache | 2 outside writes, 262 states | fsync the temp, then swap atomically (RENAME\_EXCHANGE) and check what came out | rename without fsync: empty file after power loss (5 steps) |

Limits the reviews found, stated rather than hidden. Render's "no torn frame" invariant restates its own guard, and its commands have at most 2 steps, so it cannot show the real risk of a long command; that risk is handled by polling Ctrl-C (Neovim's behaviour), not by the model. LspSync has one document, point positions instead of ranges, one request in flight, and no server restart or didOpen/didClose. Save2 covers outside writers that replace the file by rename, not a program that writes in place later. UndoTree does not need the rule that a jump remembers the children it walks into (with it removed TLC still passes, 404,013 states) and would also pass a jump through the root; only the Neovim differential test (ref/undo/undo\_diff.py) checks that rule, so that test is part of the evidence, not an extra. Its bound (4 edits) is far below the 1,000-change cap, so dropping old changes is tested, not model-checked.

The models have corrected the design three times. The first LspSync model published diagnostics in the same step as applying an edit, which hid the reply race until elaboration became its own later step. The review then showed the edit log was unneeded (a simpler rule passes the same properties) and that a save can avoid the lost-write window that I had called unavoidable.

Proofs and code checkers. proofs/Undo.lean proves undo\_group, the inversion law for any group of edits on any text, and jump: if the edits on the path from a common ancestor give the current text and the edits on another path give the target text, then undoing the first path and redoing the second lands exactly on the target. That covers every move in the undo tree, for all sizes, using only Lean's standard axioms. That the editor picks the right ancestor and remembers the right child is TLC's part. A third theorem, redo\_group, was removed after the first review because it restated its own hypothesis. Tool trials on real code:

| Tool | Language | Target | Result |
| --- | --- | --- | --- |
| Frama-C 33 WP (Z3, CVC5) | C | UTF-8 sequence length; contract only says the result is 1 to 4 and stays in bounds | 23 of 23 goals, 2.8 s. Weak: `return 1;` proves the same contract (review), and the function accepts overlong forms and surrogates, as Neovim does |
| Kani | Rust | same function and contract | 66 checks pass, 0.4 s; the 8-byte bound is exhaustive since the function reads at most 4 bytes |
| CBMC 6.11 | C | piece table: insert then delete returns the original, no out-of-bounds | out of memory (15 GB with up to 4-byte texts, 12 GB cap with 3); two harness rewrites by the reviewer also ran out |
| Kani | Rust | same harness, 3-byte text | out of memory under a 12 GB cap |

Neither the kernel results nor the piece-table failures separated the languages: both tools succeed and fail together. The second review then wrote a contract that says what the result is for the UTF-8 kernel: with the function as written (shifts and masks) 24 of 28 goals prove and 4 time out; with the same logic written as comparisons, 32 of 32 prove in 5.5 s, and a planted return 1; fails, as it should (reviews/r2/fc). Hence the rule: no bit tricks in proved code.

## C vs Rust

C is the choice, by your decision on 2026-11-17. The measurements below were made to decide between C and Rust; they found no difference in speed, so the choice costs nothing there, and they say what C has to add to match Rust on safety. The same piece table was written in both (same algorithm; a 10 MB or 100 MB text, random 1 to 10 byte inserts and deletes, each logged, then all undone and checked by hash) and measured with callgrind, which counts instructions exactly. The reviewer reran every number.

| Measure | C (gcc 13 -O2) | C (clang 18 -O2) | Rust 1.97 (LTO, panic=abort) | Tag |
| --- | --- | --- | --- | --- |
| Piece lookup (split), 5,000 edits + undo on 10 MB | 464.50 M | 506.63 M | 464.62 M | measured, exact (review) |
| libc memmove, same run | 399.4 M | 399.4 M | 399.7 M | measured (review) |
| Benchmark's own code incl. FNV hash | 120.7 M | 75.7 M | 68.3 M | measured (review); not editor code |
| Wall time, 50,000 edits + undo on 100 MB, 5 alternating runs | median 4.62 s (4.35 to 5.00) | not run | median 4.38 s (4.31 to 4.47) | measured: ranges overlap, a tie |
| Peak RSS, same run | 196 MB | not run | 197 MB | measured |
| Stripped binary | 14.5 KB | 14.6 KB | 346 KB (std linked in) | measured |
| Runtime dependencies | libc | libc | libc, libgcc\_s | measured (ldd) |

In the code that matters, gcc and Rust differ by 0.03%. The earlier "Rust 5% fewer" came from the benchmark's hash function and is withdrawn. All of this is on a flat structure the design does not use; tree code (allocation, pointer chasing, bounds checks) has not been measured in either language, so speed is a hypothesis for the real editor.

| Factor | C | Rust | Tag |
| --- | --- | --- | --- |
| Memory safety in the editor's own code | by discipline, sanitizers, fuzzing | by construction outside `unsafe` | proved for the language core ([RustBelt](https://plv.mpi-sws.org/rustbelt/popl18/)); hypothesis for this dependency set: the named crates hold 454 `unsafe` (tree-sitter binding), 333 (memchr), 227 (aho-corasick) and more (review) |
| Untrusted input via tree-sitter | C runtime and generated C parsers | the same C, through bindings | measured: no difference, the main parsing path is C either way |
| Deductive proof | Frama-C WP, mature | Verus, Creusot: not tried | neither shown: our kernel's contract was too weak to tell (review) |
| Bounded checking | CBMC | Kani (built on CBMC) | measured: identical outcomes |
| LSP, JSON | hand-written or a small JSON library | `lsp-types`, `serde_json`, or hand-written minimal structs | documented crates; cost of the crate route measured below |
| Unicode | utf8proc, libgrapheme | `unicode-segmentation`, `unicode-width` | documented libraries |
| Regex | PCRE2 (backtracking, can be exponential) | `regex` (linear time, a documented guarantee, not a proof) | documented; Vim's pattern syntax needs a translator in either language |
| Terminal emulator | libvterm (in Neovim) | `alacritty_terminal` | documented libraries |
| Size of what you depend on | small, but you write more | the named crates: 32 packages, 464,679 vendored lines, 1.99 MB stripped binary | measured (review, 9-line program) |
| Rebuild after a one-line change | 0.18 s (prototype) | 12.7 s release with LTO, 0.2 s debug (review) | measured |

**Against your top rule.** "No unneeded code" now has numbers: Rust's library route pulls in 465,000 lines to avoid writing perhaps a few thousand (a JSON reader, LSP types, a UTF-16 converter); C's route writes those few thousand by hand. Neither count is "code you need" in the strict sense, and a third route was not measured: Rust with hand-written minimal LSP structs and no `lsp-types`. "No wasted cycle" is a tie where measured. "Absolute perfection" favours Rust for the editor's own code, since use-after-free and data races cannot be written outside `unsafe`, but not for tree-sitter, which is C in both.

**What C has to add.** Rust gives memory safety outside unsafe for free; in C it is earned and checked at every gate: Frama-C proofs whose contracts state the result, not only bounds, on the kernels (UTF-8 decoding, grapheme stepping, UTF-16 conversion), written without bit tricks so the provers finish; ASan and UBSan on every test run; libFuzzer on the rope, the org parser, the JSON reader and the Vim key interpreter, each against a simple reference model; and no allocation in the redraw path after start-up (hypothesis that this can be kept). A proof of the rope itself is untried and much harder than a byte scan (CBMC and Kani already ran out of memory on a smaller structure), so it is a spike inside package 1, and differential fuzzing against a plain array is the fallback that may pass the gate. The earlier tipping test is dropped: you chose, and nothing measured argues the other way. Like for like, the rebuild after a one-line change is 0.15 s for the C prototype and 3.9 s for the Rust one with no dependencies (review); the 12.7 s in the table above is Rust with 32 crates.

**Zig and Go, briefly.** Zig matches C on size and checks safety only in debug builds; no 1.0 release and the smallest library set. Go idles fine (the reviewer measured 0 context switches in 20 s for a Go 1.24 program blocked on input), but its garbage collector adds pauses and memory overhead the other three do not have (hypothesis, not measured here).

## Plan

Six phases, each ending in a gate you can check by running something. Estimates are person-months of one experienced C developer, with agents doing the typing; they are judgement, not measurement, and work packages in parallel threads shorten the calendar, not the total. The earlier study's 3 to 5 person-years for full kittymax parity still stands.

| Phase | Delivers | Gate (all must pass) | Estimate |
| --- | --- | --- | --- |
| 0. Core on Linux | rope, undo tree saved per file, crash journal, editor core (buffers, windows, status and command lines), Vim keys you use, cell grid with Wayland window and terminal mode, font fallback and shaping, tree-sitter colours, safe save, external-change warning | the C rope matches ropey's time and memory on your files; Frama-C proves the kernels' result contracts; the rope passes differential fuzzing (its proof is a spike, not a gate); 10,000 random key sequences match nvim --headless, and 5,000 random undo sequences match Neovim's undo tree; ASan and UBSan clean; 24 h of fuzzing finds nothing; all TLA+ configs and Lean proofs pass; frame ready under 2 ms p99, key to screen within one refresh on Hyprland, 0 frames drawn unseen, first frame committed under 10 ms, 0 idle wake-ups, RSS targets; every Lean symbol in Mathlib's notation files draws | 4 to 6 months |
| 1. Windows | Win32 window, overlapped pipes, ReplaceFileW save, ReadDirectoryChangesW | the same conformance traces and performance gates on your Windows machine; the Save model's rename variant checked against Windows' rename rules | 1 to 2 months (judgement with nothing measured under it) |
| 2. LSP and Lean | LSP client, one Lean server per project, goals and messages, progress bars, \\ abbreviations, gutter diagnostics | LspSync's rules as tests against a fake server that reorders and delays replies, restarts and sends ranges; on your longest Lean file, typing continuously must never hide diagnostics for more than 2 s, or the edit log comes back | 2 to 3 months |
| 3. Widgets | VS Code's infoview in a browser, served by a C port of kittymax's bridge | the widget, trace and "Try this" cases from kittymax PR 8 as tests | 1 to 2 months |
| 4. Daily tools | SQL client with SSMS keys and a result grid, terminal pane, project search, integrated magit (status, hunks, commit, log, diff, branches, fetch, pull, push, stash) with gutter signs, debugger (DAP), Claude Code integration, file manager | each feature's recorded SSMS or Emacs behaviour reproduced | 3 to 6 months |
| 5. Org | parser, agenda, capture, refile, logging, clocking, habits, toc, images, roam links, look; export and babel through the org Emacs | zero disagreements with Org's parser, by position, on your \~/org, on random org text and on the reduced cases; agenda under 20 ms; recorded Emacs traces for TODO, capture, refile and clocking reproduced; export and babel give what Emacs gives for the same text (anchor ids normalised) | 4 to 8 months |

After phase 2 the editor is useful for Lean every day on both systems (8 to 13 months in total, with phase 3). Org follows in phase 5; until then it stays in Emacs. Mail stays outside; RSS is out.

## Building it as a Claude project

You will run development as a Claude project, with a coordinator and one thread per piece of work. Parallel agents break each other's work when they share files or guess at each other's interfaces, so the work is cut along C headers. Headers written before any code exists would be guesses, so thread 0 first builds a thin editor end to end (text in an array, undo as a list, a terminal grid, a dozen Vim keys, open and save), then cuts the headers from what that editor needed, with those simple versions as reference implementations. After that every package owns one directory, reads only the headers, and is tested against the reference implementations of the others, so it can be accepted without waiting for them; the last column says when it can be accepted against the real ones.

| Package | Owns | Contract | Accepted when | Starts after | Accepted with the real parts after |
| --- | --- | --- | --- | --- | --- |
| 0. Thin editor, headers, harness | include/, ref/ (reference implementations), CLAUDE.md, check scripts, CI, conformance driver | this doc, specs/, proofs/ | the thin editor opens, edits and saves a file; every check runs and fails on a planted bug | nothing | nothing |
| 1. Text rope | src/text/ | text.h with ACSL contracts | differential fuzzing against the array, callgrind budget against ropey; proof is a spike | 0 | 0 |
| 2. Undo tree and its saved file | src/undo/ | undo.h; UndoTree.tla, Undo.tla, Undo.lean | ref/undo/undo\_diff.py: 5,000 random sequences match Neovim; size cap and :earlier by time tested | 0 | 1 |
| 3. Unicode kernels | src/uni/ | uni.h with result contracts | Frama-C proves them; tables generated from Unicode 16 data | 0 | 0 |
| 4. Editor core | src/core/ | core.h | buffers, windows and splits, scrolling and wrapping, status and command lines, completion popup; traces against Neovim | 0 | 1, 2, 3, 6 |
| 5. Vim keys | src/keys/ | keys.h | 10,000 random sequences match nvim --headless | 0 | 1, 2, 4 |
| 6. Grid and terminal backend | src/grid/, src/tty/ | grid.h; Render.tla | frame diff tests; Render's rules as tests | 0 | 3 |
| 7. Fonts and shaping | src/font/ | font.h | every Lean symbol in Mathlib's notation files draws; combining marks and ZWJ emoji shaped; glyph cache hit rate | 0 | 3 |
| 8. Wayland backend | src/wl/ | backend.h; Present.tla | frame callbacks; performance gates on Hyprland; configure size, scale, pointer, IME, clipboard, key repeat tested under weston | 0 | 6, 7, 10 |
| 9. Win32 backend | src/win/ | backend.h | the same gates on Windows | 0 | 6, 7, 10 |
| 10. Loop and processes | src/loop/ | loop.h | non-blocking pipe tests, Ctrl-C test, 0 idle wake-ups | 0 | 0 |
| 11. Files and crash journal | src/file/ | file.h; Save.tla, Save2.tla | crash and outside-writer tests on Linux and Windows; journal replay | 0 | 1, 2 |
| 12. Syntax | src/syntax/ | syntax.h | frame budget held with tree-sitter on | 0 | 1, 6 |
| 13. LSP and Lean | src/lsp/ | lsp.h; LspSync.tla | fake-server tests; the 2 s stale-diagnostics limit | 10 | 1, 4 |
| 14. Infoview bridge | src/infoview/ | bridge.h | PR 8 cases | 13 | 13 |
| 15. Daily tools: SQL client, terminal pane, project search, debugger, Claude Code integration, file manager | src/tools/ | tools.h | recorded SSMS and Emacs behaviour | 10 | 4, 10 |
| 16. Org | src/org/ | org.h | the phase 5 gate | 10 | 1, 4, 10 |
| 17. Git (magit) | src/git/ | git.h; Git.tla | recorded magit traces (evil-collection keys) reproduced on a scratch repository; a refresh runs at most 5 git processes; reads never take index.lock; Git.tla passes | 10 | 4, 10 |

Rules for the threads, to put in the project instructions: a header changes only through thread 0; a package never edits another package's directory; every merge runs the whole check; each package gets a Fable 5.2 review in its own thread before merging, and the review reproduces what it reports; measurements are recorded with their run count, as here. For a choice where one attempt could lock in the wrong shape (the rope, the Wayland backend, the org parser), two threads build it independently and the better one wins on the package's own checks, which is pstack's arena idea run as threads.

## New approaches and their risk

Your rule: a new approach is adopted only if it beats every combination of existing ones in all scenarios, and unknown scenarios exist. Three choices in this design are not straight copies of a reference editor.

| Choice | Existing alternatives compared | Scenarios considered | Evidence | Residual risk |
| --- | --- | --- | --- | --- |
| Save by atomic swap (renameat2 RENAME\_EXCHANGE) and check what came out | rename over the file (Neovim, Helix, Emacs); in-place write with backup (Neovim's fallback) | outside rename during the save window; crash with data in the page cache; outside write before save | Save2 passes where rename fails (TLC, 262 states); xchg.py on ext4 (review) | file systems without RENAME\_EXCHANGE (some network and FUSE ones) need the rename fallback; in-place outside writers are not covered; not run on your file system yet |
| Win32 frame pacing: frames at least P + P/8 apart (P from DwmGetCompositionTimingInfo), a waitable timer in the one loop (package 9) | DXGI swap chain with a frame-latency waitable object (Windows Terminal); a timer placed from DWM's own vblank time (qpcVBlank from the same call), which may allow one frame per refresh instead of one per 1.125 (hypothesis, from the review's memory of Chromium's Windows vsync provider; not checked); a vsync thread with DwmFlush or D3DKMTWaitForVerticalBlankEvent (Firefox, Chromium); drawing on WM\_PAINT (Emacs, gvim) | keys faster than a refresh; DWM composing early or late; minimised; window moved to a monitor of another refresh rate; DWM dropping a frame under load | Pace.tla (proved within its bounds); test\_win under Wine 11.0 (measured); DWM's slip on your laptop (measured, 1 idle run: 0 of 1,199 gaps off by more than P/8); under steady input, computed from the model's picture by the package 9 review (not measured on Windows): 2,000 keys 1 ms apart at 60 Hz give 108 frames and key to screen median 18.3 ms, p99 33.3 ms, against 8.5 and 16.6 ms when drawing every key, which wastes 1,880 frames | not better in all scenarios, so not settled: a frame is wasted if DWM slips by more than P/8 or drops a composition, and the period is read only at start and on WM\_DISPLAYCHANGE (DWM's slip measured on your laptop, 1 run, idle, 60 Hz: 1,199 compositions, gaps 16.666 to 16.674 ms against P/8 = 2.083 ms, none skipped, by `src/win/probe/dwmprobe.c`; under load and on another monitor not yet measured); the DXGI waitable object is exact but loads a D3D device, its memory and driver threads not measured; the threaded routes break the one-loop rule; WM\_PAINT alone wastes frames (Pace "noguard"); the gates "key to screen within one refresh" and "0 frames unseen" pull against each other under steady input, so phase 1 measures both |
| Ignore stale diagnostics, no edit log | map old diagnostics through an edit log (VS Code's markers move with edits) | all LspSync scenarios | both pass TLC; the simple one has less code | during a long Lean elaboration, typing may hide new diagnostics until you pause; measured in phase 2 against a 2 s limit before the choice is final |

Everything else copies one of the reference editors and is backed by their behaviour as recorded above.

Scenarios listed but not yet designed or tested: invalid UTF-8, CRLF, a byte-order mark and a missing final newline must survive a save byte for byte; terminal resize and Ctrl-Z; a 10 MB paste as one command; fsync on a slow or network disk; a saved undo log or crash journal that meets a file changed by another program.

## pstack as the project harness

In a Claude project the project itself is the harness: the coordinator, one thread per package, project memory, the repo's CLAUDE.md and Fable review threads. pstack adds to that only as copied skills: four of them, after the review (see Proposal). Its router, its team-kit dependencies and its fan-out skills stay out; their ideas (arena, swarm) are run as parallel threads instead. Whether the skills help is a hypothesis to test, because nobody has published evidence that pstack improves results.

**What it is.** [pstack](https://github.com/cursor/plugins/tree/main/pstack) is a Cursor plugin by Lauren Tan (poteto), MIT, version 0.15.43, last changed 2026-11-16: 58 skills and 2 agents, 6,692 lines of Markdown, cloned and read here. A router skill, `poteto-mode`, picks a playbook and calls the rest. Claude Code runs it through a community port, [pstack-claude](https://github.com/michael-denyer/pstack-claude) (version 0.9.138, last commit 2026-11-13, about 3,400 stars per the GitHub page). A companion, [agent-formal-verify](https://github.com/michael-denyer/agent-formal-verify), adds guided TLA+, Lean 4 and Kani, under GPL-3.0.

**Where it matches your rules.** Several skills say what you have asked for, in nearly your words:

- `principle-laziness-protocol` and `principle-subtract-before-you-add`: bias to deletion, smallest change.
- `principle-prove-it-works` and `principle-explain-the-number`: check the real artefact, name what limits a number.
- `benchmark-checklist`: at least 5 alternating runs per side, median and range, name the limiter, call it no measurable difference when the gap is inside the noise.
- `create-formal-verification-skill`: "A check that you have never seen fail proves nothing. Show each property failing on a broken variant." This is the rule the specs here already follow, and it also leaves a project skill that records which code each check covers and at which commit.
- `principle-exhaust-the-design-space`: build 2 or 3 competing prototypes before a novel decision, which is phase 0's tipping test.

**Tried on this design.** I ran `benchmark-checklist` on the C vs Rust timing, which had been one run per side. With 5 alternating runs of 50,000 edits on 100 MB, Rust took a median of 4.38 s (range 4.31 to 4.47) and C (gcc) 4.62 s (4.35 to 5.00). The ranges overlap, so the verdict is no measurable difference. The limiter is the linear scan in `split` plus memmove (callgrind). The checklist caught a gap the doc had, and the reviewer found the same one.

**Where it does not fit.**

- The router assumes Cursor and Cursor's `cursor-team-kit` (`/deslop`, `control-cli`), a PR-per-change team flow with worktrees and `gh`, and one TypeScript skill.
- Its autonomy default ("Just do it", post to team chat and tickets without asking) is looser than this project's rules.
- `swarm`, `arena` and `interrogate` fan out to several models, and the port's own docs warn that the bill grows fast.
- Using all 58 skills breaks your minimalism rule for the harness itself.
- agent-formal-verify is GPL-3.0, so copying it into the editor repo needs a licence decision.

**Proposal (revised after review).** The five principle skills are 9 to 18 lines each and restate your standing rules, so they become a few lines of the editor's CLAUDE.md, with nothing to pin. Copy four skills into .claude/skills, pinned to a commit with the MIT notice: benchmark-checklist, create-formal-verification-skill, maintain-formal-verification-skill and tdd. Leave out statechart (408 files, 4.1 MB, TypeScript) and architect (it hands large designs to arena). To test whether the four help, one pair of runs is not enough by benchmark-checklist's own rule: run five small, comparable tasks from package 3 (Unicode kernels) with the skills and five without, alternating, and compare defects found by the Fable review, lines of code and tokens. Keep them only if the difference is outside the run-to-run spread.

## Open questions for you

- Settled on 2026-11-17: C; a window by default with a terminal mode, on Linux and Windows; undo is a tree; org is in; RSS is out; development as a Claude project, handed over to a new project that starts from an empty repo.
- Answered on 2026-11-17: the Vim features are derived from my-nvim and kittymax (FEATURES.md, from a live dump of my-nvim's 390 mappings); ligatures on, -> drawn as an arrow; undo history as Neovim (1,000 levels); :session rarely used but wanted, so the org Emacs keeps running while a session block has state; Windows is your work laptop, with the same fonts as Linux; Hyprland only, so no client-side decorations; org-roam's graph view is nice to have, after phase 5.
- Answered later on 2026-11-17: you want integrated magit, so git is package 17 in phase 4 (Architecture, Git (magit)). The debugger (DAP) and the Claude integration (SPC a) are in the configs and are now in phase 4.
