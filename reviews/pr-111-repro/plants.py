#!/usr/bin/env python3
"""Review of PR 111: the reviewer's own plants in src/lsp/lsp.c.  Each is one exact
replacement; tests/test_lsp.c is built and run (ASan+UBSan) once per plant.
From the repo root, after run.sh has been run once: python3 reviews/pr-111-repro/plants.py"""
import subprocess, sys, os
P = [
 ("fz does not follow edits", "fz = shift_end(g->fz, pos, ndel, nins);", "fz = g->fz;"),
 ("another file asked with the old file's RPC session", "if (d != G.d) G.sess.n = G.shown = 0;", "if (d != G.d) G.shown = 0;"),
 ("a range wholly deleted since is kept", "if (!point && !before(a, z)) continue;", "if (0) continue;"),
 ("a closed document stays the goals window's", "	if (G.d == d) G.d = NULL;\n", ""),
 ("asked while Lean elaborates the cursor's line", "if (over(&G.d->prog) || G.d->prog.ver < 0)", "if (G.d->prog.ver < 0)"),
 ("a removed hypothesis drawn plain", "? FACE_DIFF_MINUS : f;", "? f : f;"),
 ("willInsert drawn as deleted", "FACE_DIFF_DELTA, FACE_DIFF_MINUS, FACE_DIFF_PLUS, FACE_DIFF_PLUS,", "FACE_DIFF_DELTA, FACE_DIFF_MINUS, FACE_DIFF_PLUS, FACE_DIFF_MINUS,"),
 ("never the bottom split on a tall screen", "int v = 2 * e->cols > 5 * e->rows;", "int v = 1;"),
 ("the window closed with \\i opens again at the next key", "int fresh = !G.off && !gbuf();", "int fresh = !gbuf();"),
 ("goals accomplished do not follow edits", "	shift_set(&d->done, pos, ndel, nins);\n", ""),
 ("inaccessible names not dimmed", "? FACE_COMMENT : FACE_TYPE", "? FACE_TYPE : FACE_TYPE"),
 ("a let's value left out", "{ putz(o, \" := \", FACE_NONE); tagged(o, m, x, FACE_NONE); }", "{ }"),
 ("message severity word always 'error'", "putz(o, sev[g->sev - 1], f);", "putz(o, sev[0], f);"),
 # second review (head 018df26)
 ("a change of colours alone is not shown", " && !memcmp(o->f.s, G.all.f.s, o->f.n)) return;", ") return;"),
 ("another file's connect answer taken as this one's session", "if (x >= 0 && ctx == G.d) {", "if (x >= 0 && (ctx == G.d || 1)) {"),
 ("after one outdated session, never renewed again", "G.want = 1; G.again = 0; }", "G.want = 1; }"),
 ("new diagnostics do not reach the goals window", "			if (d) gpoke(d);\n", ""),
 # third review (head e6d8fcf)
 ("a pending retry's window emptied meanwhile", "	if (G.want || !G.d) return;\n	G.goals.t.n", "	if (!G.d) return;\n	G.goals.t.n"),
 ("a refused first question leaves 'Processing file...'", "	G.ng = -1;\n	G.shown = 1;\n", "	G.ng = -1;\n"),
 ("a refused question keeps the old 'No goals.'", "	G.ng = -1;\n	G.shown = 1;\n", "	G.shown = 1;\n"),
 ("goals that are not an array counted", "G.ng = gs >= 0 && tk[gs].type == 'a' ? tk[gs].n : -1;", "G.ng = gs >= 0 ? tk[gs].n : -1;"),
]
src = "src/lsp/lsp.c"
orig = open(src).read()
only = sys.argv[1:]
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", XDG_STATE_HOME=os.getcwd() + "/build/rvstate")
try:
    for i, (name, a, b) in enumerate(P):
        if only and str(i) not in only: continue
        assert orig.count(a) == 1, name
        open(src, "w").write(orig.replace(a, b))
        r = subprocess.run("make -s -f Makefile -f build/rv-lt.mk lt T=tests/test_lsp.c O=build/san/test_lsp_plant", shell=True, capture_output=True, text=True)
        if r.returncode: print(f"{i} {name}: DOES NOT BUILD (caught)\n{r.stderr[-300:]}", flush=True); continue
        r = subprocess.run("./build/san/test_lsp_plant", shell=True, capture_output=True, text=True, env=env)
        tail = (r.stdout + r.stderr).strip().splitlines()
        print(f"{i} {name}: {'caught' if r.returncode else 'MISSED'} (exit {r.returncode}; {tail[0] if tail else ''}; {[l for l in tail if 'FAIL' in l or 'ERROR' in l or 'WRONG' in l][:1]})", flush=True)
finally:
    open(src, "w").write(orig)
