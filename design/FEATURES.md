# Features: what the editor must do

Derived on 2026-11-17 from Davy's two working editors, which are the source of truth:

- **my-nvim** (Neovim 0.12): its live keymaps were dumped with the config loaded (`ref/keymaps/my-nvim-keymaps.tsv`, 390 mappings: scope, mode, keys, description). Options come from `init.lua`.
- **kittymax** (Emacs + evil): `kitty-evil.el` and `kitty-vim.el` say they copy my-nvim's keys ("Neovim is the main editor and the source of truth for every key"). Where kittymax has extras on keys Neovim leaves free, they are listed as such.

Davy's rule for this list: derive it from the two configs; nothing he does not use. Each line is in scope unless marked **out** (with the reason) or **later** (a phase after the one named).

Tags: **nvim** = in my-nvim, **emacs** = in kittymax, **both**.

## 1. Settings (both)

| Setting | Value |
|---|---|
| Leader / local leader | Space / backslash |
| Line numbers | absolute + relative |
| Wrap | off by default (toggle) |
| Indent | spaces, shiftwidth 4, softtabstop 4 |
| Search case | ignorecase + smartcase |
| Undo | kept across restarts (undofile); tree; 1,000 levels (Neovim default, Davy 2026-11-17) |
| scrolloff | 6 |
| :s preview | live, with the affected lines in a split (inccommand=split) |
| Visual block | may go past line ends (virtualedit=block) |
| Splits | open right and below |
| Y | yanks to end of line (Neovim default) |
| Ligatures | on: `->` drawn as an arrow (Davy 2026-11-17) |

## 2. Core Vim (both; checked against `nvim --headless`)

Normal, insert, visual (char, line, block), operator-pending, replace and command-line modes. Counts. Operators `d c y > < = g~ gu gU gq !` plus `gc` (comment), `ys`/`cs`/`ds`/`gs` (surround), `ga`/`gA` (align). Motions: `h j k l w W b B e E ge gE 0 ^ $ g_ f F t T ; , % { } ( ) [[ ]] gg G H M L * # n N / ?` with offsets, `gj gk` in markdown. Text objects `iw aw iW aW is as ip ap` quotes, brackets, tags. Registers (named, numbered, `"+` clipboard, `"_`, `".`, `":`, `"/`), macros (`q @ @@`), `.` repeat, marks (`m ' `` ` ``), jump list (`C-o C-i`), change list (`g; g,`), `C-a C-x` (numbers, and dates in org), `J gJ`, `r R ~`, `C-v` block insert/append, `gv`, `o` in visual, `:s` with Vim regex, `:g`, `:norm`, ranges, `:w :q :e :sp :vs :b`, `C-w` window keys, `C-u C-d C-e C-y zz zt zb`, folds (`za zc zo zR zM`, Tab in org), spell (`]s z=` via the toggle), digraphs (`C-k` in insert stays Vim's), insert keys `C-w C-u C-r C-o C-t C-d`, `C-s` (signature help), Tab/S-Tab snippet jumps.

Neovim 0.12 defaults that my-nvim keeps: `[b ]b [B ]B` buffers, `[q ]q [Q ]Q [C-q ]C-q` quickfix, `[l ]l` location list, `[a ]a` args, `[t ]t` tags, `[<Space> ]<Space>` blank lines, `[d ]d [D ]D` diagnostics, `C-w d` diagnostic float, `gx` open URL, `gO` outline, `grn gra grr gri grt grx` LSP, `gd gD K`, visual `* #`, `&`, `Q`, `@` in visual, `C-l` clears highlight.

## 3. Editing extras (both; mini.nvim in my-nvim, evil packages in kittymax)

- Surround: `ys{motion}{char}`, `cs{old}{new}`, `ds{char}`, visual `gs`; `*` makes `**bold**` in markdown.
- Align: `ga{motion}{char}` (`gaip=`), `gA` with preview.
- Comment: `gc{motion}`, `gcc`, `gc` text object.
- mini.ai text objects: `ia/aa` argument, `if/af` call, `iq/aq` any quote, `ib/ab` any bracket, `iN(`/`aN(` next pair, `iL(`/`aL(` last pair, `g[ g]` to the edges of an "around" object.
- Syntax-node selection: `S` (select node), visual `an`/`in` grow/shrink, `]n [n ]N [N` next/previous node and sibling.
- Jump anywhere: `s` (labels on screen), `S` (node labels), remote `r` in operator-pending, `R` search; `f F t T` enhanced with labels (flash). kittymax: avy on `s`.
- Multiple cursors: `C-n` cursor on the word and its next match, `SPC M` all matches, `Q` toggle a cursor here, `]C [C` between them, `gQ` restore, `C-l` clear (kittymax lists these; my-nvim has `multicursor.lua`).
- Markdown: `o` and Enter continue lists, `j k` move by screen line, `[[ ]]` sections, `SPC ce` edit a code block in its own buffer.
- SQL: `[[ ]]` statements, `iq/aq` statement object, `K` object info.

## 4. Leader keys (SPC; both unless marked)

| Group | Keys |
|---|---|
| top | `SPC ,` buffers, `SPC e` file explorer, `SPC pv` and `-` file manager (oil), `SPC R` reload config, `SPC X` run line or selection (Lua in nvim, elisp in emacs: **out**, configuration is C), `SPC M` cursors on all matches |
| f find | `ff` files, `fg` grep, `fr` registers, `ft` and `C-/` floating terminal; emacs extras: `fo` recent, `fp` config, `fR` rename file, `fD` delete file, `fy` copy path, `fu` sudo |
| u toggles | `uu` undo tree view, `uz` zen, `uk` kitten on save, `uh` inlay hints; emacs extras: line numbers, wrap, indent guides, theme flavour, spell, hide markup, read only, cursor line, org pretty symbols |
| x diagnostics | `xx` project, `xX` buffer, `xL` location list, `xQ` quickfix |
| c code | `ca` code action, `cs` symbols, `cl` references; emacs: `cf` format, `cc`/`cC` compile, `cR` restart server, `co` organize imports |
| r refactor | `rn` rename |
| g git | `gg` git UI (neogit / magit), `gs` status, `gl`/`gL` log, `gh`/`gH` history, `gd` diff, `gb` branches, `gB` open on GitHub |
| h hunks | stage, reset, stage buffer, unstage, preview hunk; signs in the gutter |
| d debug | `db` breakpoint, `dB` conditional, `dc` continue, `ds` over, `di` into, `do` out, `du` UI, `dl` run last, `dq` stop, `de` eval |
| a AI (Claude) | `ac` toggle, `af` focus, `aC` continue session, `ab` add buffer, `aa`/`ad` accept/reject diff; visual `as` send, `ae` explain, `ar` refactor, `at` tips |
| o org | see section 6 |
| m local | per language, section 5 |
| emacs only | `SPC p` projects, `SPC b` buffers, `SPC s` search (in buffer, all buffers, symbols, outline, marks, kill ring), `SPC n` notes (find, search, link, journal, todo), `SPC t` tools (terminal, terminal here, database connect, markdown preview, file manager), `SPC i` insert (from kill ring, snippet, unicode, emoji, file name), `SPC q` quit/restart, `SPC H` help |

Scope decisions:
- Git UI: **in**, as an integrated magit (Davy, 2026-11-17: "Yes, I want integrated magit"), phase 4, package 17. magit with kittymax's evil-collection keys is the reference: status buffer with sections, stage/unstage/discard of files and hunks, commit (message in an editor buffer), log and file log, diff, branches, fetch, pull, push, stash, plus `SPC gB` open on GitHub and gutter signs. Measurements and the timing model: `git/measured-2026-11-17.md`, `specs/Git.tla`.
- Debugger (DAP, `SPC d`): **in**, phase 4, not yet in the design's package list; add it to package 15 (daily tools). Languages: C/C++ (my-nvim's `SPC md`).
- Claude integration (`SPC a`): **in**, phase 4; my-nvim uses claudecode.nvim (the editor exposes buffers, selections and diffs to Claude Code). Add to package 15.
- RSS (`SPC t r`): **out** (Davy, 2026-11-17).
- eshell, M-x, eval: **out** (Emacs-specific).

## 5. Per language (local leader `\` and `SPC m`)

- **Lean (nvim, lean.nvim; emacs equivalents)**: `K` hover, `\r` restart file, `\\` how to type the symbol under the cursor, `\s` accept first suggestion, `\Tab` jump to infoview, `\v` view options, `\w`/`\W` widgets on/off, pins (`\x` add, `\c` clear, `\p` pause), diff pins (`\dx \dc \dd \dt`), `\i` toggle infoview, `\S` a sorry per open goal, `\f` files in core and dependencies, `\/` grep them, `\l` Loogle, `\u` symbol picker; `SPC mc` Mathlib cache get, `SPC mm` lake build to quickfix, `SPC mr` run main. Abbreviations `\alpha` to α as in VS Code.
- **C/C++ (nvim)**: `SPC mm` build, `mr` run, `md` debug, `mg` configure, `mt` target, `mb` build type, `ma` alternate source/header, `ms` symbol info (clangd).
- **SQL (both, dbbliss)**: SSMS keys (design doc, Sources of truth), statement motions and objects above, `K` object info, result grid.
- **Markdown (both)**: list continuation, sections, outline, code-block editing, rendered view toggle (`um` hide markup).
- Other languages with LSP and tree-sitter in my-nvim: Rust (with crates info), Nix, Lua, GLSL, Verilog, CMake, Soufflé. LSP + tree-sitter only; no extra keys.

## 6. Org (both; nvim-orgmode keys, kittymax equivalents)

`SPC oa` agenda, `SPC oc` capture (inbox, notes, journal datetree), `SPC or` refile, `SPC o$` archive subtree, `SPC oA` archive tag, `SPC ot` tags, `SPC oe` export, `SPC oo` open link, `SPC oli`/`ols` insert/store link, `SPC o'` edit block in its own buffer, `SPC obt` tangle, `SPC ois` schedule, `SPC oid` deadline, `SPC oi.`/`oi!` active/inactive timestamp, `SPC od!` toggle timestamp type, `SPC oit`/`oiT`/`oih` insert TODO/heading, `SPC o*` toggle heading, `SPC o,` priority, `SPC ona` add note, `SPC oJ`/`oK` move subtree, clock `SPC oxi` in, `oxo` out, `oxq` cancel, `oxj` goto, `oxe` effort; `cit`/`ciT` next/previous TODO state, `cir`/`ciR` priority, `cid` change date; `<< >> <s >s` promote/demote heading/subtree; `[[ ]] { } g{` heading navigation; Tab / S-Tab folding; `C-Space` checkbox; `C-a C-x S-Up S-Down` on timestamps; `SPC Enter` meta-return; `g?` help. TODO keywords TODO NEXT WAIT | DONE CANCELLED with logging into a drawer. Habits, clocking (persisted), clock table, babel (C, C#, SQL via dbbliss, shell, Python, elisp, dot) with `:session` support kept available (Davy: "haven't really used much :session but I might want to"), export HTML/PDF/docx/GFM, LaTeX previews, toc-org, org-download, org-roam (links and backlinks; graph view nice to have, **later**), org-modern / org-appear / org-bullets look, `SPC up` pretty symbols.

## 7. Look and UI (both)

Catppuccin theme (flavour toggle), which-key style hints after a prefix, floating terminal, file manager (oil-style buffer editing of a directory), fuzzy pickers (files, grep, buffers, registers, symbols), Trouble-style diagnostics list, inlay hints, zen mode, rendered markdown, images and LaTeX inline (window mode).

Start screen (nvim; Davy, 2026-11-18: started without a file, it shows a start screen): my-nvim's snacks dashboard, with the kittymax cat (`config/core/splash.lua`), the keys f find file, n new file, g find text, r recent files, q quit (c config is **out**: configuration is C), and the "loaded in" footer; recent files are Neovim's v:oldfiles (ShaDa), kept across sessions.

## 8. Platforms (Davy, 2026-11-17)

Linux: Hyprland only (no client-side decorations needed). Windows: his work laptop, with the same fonts as Linux installed.
