#!/usr/bin/env python3
"""Review of PR 29: plants beyond tests/plants/git.txt.  Each is planted in a
scratch copy of the repo and `make test TESTS=git` must fail (as
tools/planted.sh).  Run from the repo root: python3 reviews/pr-29-repro/extra-plants.py"""
import subprocess, tempfile, shutil, sys, re
P = [
 ("a hunk patch keeps its new start (magit-apply--adjust-hunk-new-starts)", "src/git/keys.c",
  '"%ld", c - off))', '"%ld", c - 0 * off))'),
 ("a write is tagged with the newest version, not the shown one", "src/git/keys.c",
  "W.ver = f && f->a ? f->a->ver : r->shown;", "W.ver = f && f->a ? f->a->ver : r->ver;"),
 ("a write starts without reading the watch", "src/git/write.c",
  "	r->qn--;\n	git_drain(r);", "	r->qn--;"),
 ("octal escapes in quoted paths not decoded", "src/git/sec.c",
  "if (c >= '0' && c <= '7' && i + 2 < n) {", "if (0) {"),
 ("untracked files leave the index but are never trashed", "src/git/keys.c",
  "		W.trash = 1;\n		break;", "		break;"),
 ("the sleeping editor finishes on any answer", "src/git/git.c",
  'read -r r; test \\"$r\\" = y; }; f"', 'read -r r; true; }; f"'),
 ("a write's end leaves the log and diff buffers stale", "src/git/write.c",
  "	for (Aux *a = r->aux; a; a = a->next) git_aux_fill(a);", ""),
 ("new directories are not watched", "src/git/git.c",
  "			if ((ev->mask & (IN_CREATE | IN_MOVED_TO)) && (ev->mask & IN_ISDIR)) watch_dir(r, path, 0);", ""),
 ("events of ignored files refresh", "src/git/git.c",
  "				if (ignored(r, rel) || ignored(r, reld)) continue;", ""),
 ("the queue takes a ninth write", "src/git/write.c",
  'if (r->qn == QMAX) { git_write_free(w); say("git: %s", "too many commands waiting"); return -1; }', ""),
 ("tags in packed-refs not seen", "src/git/git.c",
  '	while (!any && fgets(line, sizeof line, f)) any = strstr(line, " refs/tags/") != NULL;',
  '	while (!any && fgets(line, sizeof line, f)) any = 0 * (strstr(line, " refs/tags/") != NULL);'),
 ("gutter signs all say added", "src/git/keys.c",
  "if (n == t) return del ? '~' : '+';", "if (n == t) return '+';"),
 ("SPC h s stages every hunk of the file", "src/git/keys.c",
  "	if (apply(&r->st, h, r->st.sec[h].after, k == 's'", "	if (apply(&r->st, r->st.sec[h].parent + 1, r->st.sec[r->st.sec[h].parent].after, k == 's'"),
 ("SPC g B opens the line above", "src/git/keys.c",
  "o->rel, e->win->line + 1);", "o->rel, e->win->line);"),
 ("a refresh that cannot run git diff shows no unstaged changes", "src/git/git.c",
  "		git_spawn(r, k ? SLOT_CACHED : SLOT_DIFF, &c);", "		if (k) git_spawn(r, SLOT_CACHED, &c);"),
 ("the stash list numbered oldest first", "src/git/status.c",
  'int nl = snprintf(name, sizeof name, "stash@{%d}", k);', 'int nl = snprintf(name, sizeof name, "stash@{%d}", count - 1 - k);'),
 ("git_free leaves git's children running", "src/git/git.c",
  "if (r->busy && r->pid[s] >= 0) proc_kill(r->pid[s]);", ";"),
 ("SPC g d of a file diffs the whole tree", "src/git/keys.c",
  "	case 'd': diff_open(e, r, T_UNSTAGED, o && o->rel ? o->rel : NULL); return 0;", "	case 'd': diff_open(e, r, T_UNSTAGED, NULL); return 0;"),
]
only = sys.argv[1:]
rc = 0
for name, file, old, new in P:
    if only and not any(o in name for o in only): continue
    tmp = tempfile.mkdtemp()
    for d in ("Makefile", "include", "ref", "src", "tests", "tools", "design"):
        (shutil.copytree if d != "Makefile" else shutil.copy)(d, tmp + "/" + d)
    s = open(tmp + "/" + file).read()
    if s.count(old) != 1:
        print("BROKEN PLANT  " + name); rc = 1; shutil.rmtree(tmp); continue
    open(tmp + "/" + file, "w").write(s.replace(old, new))
    r = subprocess.run("make test TESTS=git", shell=True, cwd=tmp, stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode == 0: print("NOT CAUGHT    " + name); rc = 1
    else:
        why = [l for l in r.stdout.splitlines() if re.search(r"CHECK\(|magit|ERROR|FAIL|error", l) and not l.startswith("gcc")]
        print("caught        %s: %s" % (name, (why or ["?"])[0][:110]))
    sys.stdout.flush()
    shutil.rmtree(tmp)
sys.exit(rc)
