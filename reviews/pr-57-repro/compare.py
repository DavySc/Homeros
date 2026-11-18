#!/usr/bin/env python3
# usage: compare.py <theme.c> <catppuccin dir> [transparent 0|1]
# Compares every face in ref/theme.c with the group a live nvim shows.
import re, subprocess, sys, os, tempfile
theme, cat, tr = sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else "0")
ui = {  # theme.c comment name -> Neovim groups it claims
 "cursor line": ["CursorLine"], "cursor line nr": ["CursorLineNr"], "line nr": ["LineNr"],
 "status line": ["StatusLine", "MiniStatuslineFilename"], "status line nc": ["MiniStatuslineInactive", "StatusLineNC"],
 "win separator": ["WinSeparator"], "end of buffer": ["EndOfBuffer"], "visual": ["Visual"], "search": ["Search"],
 "cur search": ["CurSearch"], "match paren": ["MatchParen"], "pmenu": ["Pmenu"], "pmenu sel": ["PmenuSel"],
 "normal float": ["NormalFloat", "WhichKey", "WhichKeyNormal"], "float border": ["FloatBorder", "SnacksPickerBorder", "WhichKeyBorder"],
 "float title": ["FloatTitle"], "cursor": ["Cursor"],
 "mode normal": ["MiniStatuslineModeNormal"], "mode insert": ["MiniStatuslineModeInsert"], "mode visual": ["MiniStatuslineModeVisual"],
 "mode replace": ["MiniStatuslineModeReplace"], "mode command": ["MiniStatuslineModeCommand"], "mode other": ["MiniStatuslineModeOther"],
 "status info": ["MiniStatuslineDevinfo", "MiniStatuslineFileinfo"],
 "git add": ["GitSignsAdd"], "git change": ["GitSignsChange"], "git delete": ["GitSignsDelete"],
 "diag error": ["DiagnosticError"], "diag warn": ["DiagnosticWarn"], "diag info": ["DiagnosticInfo"], "diag hint": ["DiagnosticHint"],
 "diag under error": ["DiagnosticUnderlineError"], "diag under warn": ["DiagnosticUnderlineWarn"],
 "diag under info": ["DiagnosticUnderlineInfo"], "diag under hint": ["DiagnosticUnderlineHint"],
 "diag vt error": ["DiagnosticVirtualTextError"], "diag vt warn": ["DiagnosticVirtualTextWarn"],
 "diag vt info": ["DiagnosticVirtualTextInfo"], "diag vt hint": ["DiagnosticVirtualTextHint"],
 "indent": ["SnacksIndent"], "indent scope": ["SnacksIndentScope"], "pick match": ["SnacksPickerMatch"], "pick prompt": ["SnacksPickerPrompt"],
 "which-key group": ["WhichKeyGroup"], "which-key desc": ["WhichKeyDesc"], "which-key separator": ["WhichKeySeparator"],
 "dashboard title": ["SnacksDashboardTitle", "SnacksDashboardHeader"], "dashboard footer": ["SnacksDashboardFooter"],
}
for i in range(1, 7): ui["heading %d background" % i] = ["RenderMarkdownH%dBg" % i]
rows = []
for line in open(theme):
    m = re.match(r"\s*\{ \{(\d+), (\d+), (\d+)\}, \{(\d+), (\d+), (\d+)\}, ([^,]+), \{([^}]*)\} \},\s*/\* ([^:]+?)(:| \*/)", line)
    if not m: continue
    name = m.group(9)
    style = m.group(7).strip()
    sp = [int(x) for x in m.group(8).split(",")]
    sp = (sp + [0, 0, 0])[:3] if len(sp) == 3 else [0, 0, 0]
    rows.append((name, [int(m.group(i)) for i in (1, 2, 3)], [int(m.group(i)) for i in (4, 5, 6)], style, sp))
print(len(rows), "faces parsed from", theme)
want = []
for name, *_ in rows:
    if name in ui: gs = ui[name]
    elif name == "none" or name.startswith("kitten") or name.startswith("dashboard"): gs = []
    elif re.match(r"markup\.heading\.\d", name): gs = ["@" + name + ".markdown"]
    else: gs = ["@" + name]
    want.append(gs)
with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
    for gs in want:
        for g in gs: f.write(g + "\n")
out = subprocess.run(["nvim", "--clean", "--headless", "-l", os.path.join(os.path.dirname(os.path.abspath(__file__)), "dump_all.lua"), cat, tr, f.name],
                     capture_output=True, text=True)
got = {}
for l in (out.stdout + out.stderr).splitlines():
    p = l.split()
    if len(p) == 6 and p[1].startswith("fg="): got[p[0]] = {k: v for k, v in (x.split("=", 1) for x in p[1:])}
bits = {"FACE_BOLD": "bold", "FACE_ITALIC": "italic", "FACE_UNDERLINE": "underline", "FACE_STRIKE": "strikethrough", "FACE_UNDERCURL": "undercurl"}
bad = n = 0
for (name, fg, bg, style, sp), gs in zip(rows, want):
    if not gs: print("  no group claimed:", name); continue
    fl = [s.strip() for s in style.split("|")]
    mine = "fg=%s bg=%s sp=%s st=%s" % (",".join(map(str, fg)) if "FACE_FG" in fl else "-", ",".join(map(str, bg)) if "FACE_BG" in fl else "-",
            ",".join(map(str, sp)) if "FACE_SP" in fl else "-", "+".join(sorted(bits[b] for b in fl if b in bits)))
    for g in gs:
        n += 1
        h = got.get(g)
        if not h: print("MISSING from nvim output:", g); bad += 1; continue
        theirs = "fg=%s bg=%s sp=%s st=%s" % (h["fg"], h["bg"], h["sp"], "+".join(sorted(x for x in h["st"].split("+") if x)))
        if mine != theirs: bad += 1; print("MISMATCH %-22s vs %-28s theme.c: %s\n%56s nvim:    %s (via %s)" % (name, g, mine, "", theirs, h["via"]))
print("%d comparisons, %d mismatches" % (n, bad))
