#!/usr/bin/env bash
# Review of PR 66: which copies `make dupes` sees.  Run from the repo root;
# each probe plants into a scratch copy and prints dupes.py's exit status
# (1 = caught).
root=$PWD; tmp=$(mktemp -d)
fresh() { rm -rf "$tmp/q"; mkdir "$tmp/q"; cp -r "$root"/{Makefile,include,ref,src,tests,tools} "$tmp/q/"; cd "$tmp/q"; }
run() { python3 tools/dupes.py | cut -c1-110; echo "  -> exit ${PIPESTATUS[0]}"; }
BODY='	for (int i = 0; i < MAXT; i++) if (t[i].fn && (first < 0 || t[i].at < first)) first = t[i].at;
	if (first < 0) return timeout_ms;
	long long d = first - loop_now();
	int ms = d <= 0 ? 0 : (int)((d + 999999) / 1000000);
	return timeout_ms < 0 || ms < timeout_ms ? ms : timeout_ms;'
fn() { printf 'static int twin(long long %s, int timeout_ms)\n{\n%s\n}\n' "$1" "$2"; }
echo "1 timer_wait's body copied into src/wl/wl.c";          fresh; fn first "$BODY" >> src/wl/wl.c; run
echo "2 the same, one name changed (first -> soon)";         fresh; fn soon "${BODY//first/soon}" >> src/wl/wl.c; run
echo "3 the same, other layout and comments";                fresh; fn first "$(echo "$BODY" | sed 's/;/; \/* c *\/\n\n/g')" >> src/wl/wl.c; run
echo "4 the same, into tests/test_loop.c";                   fresh; fn first "$BODY" >> tests/test_loop.c; run
echo "5 the same, as a macro's body";                        fresh; printf '#define TWIN(first, timeout_ms) do { \\\n%s \\\n} while (0)\n' "$(echo "$BODY" | sed 's/$/ \\/')" >> src/wl/wl.c; run
echo "6 the same, static inline in include/loop.h";          fresh; fn first "$BODY" | sed 's/^static/static inline/' >> include/loop.h; run
echo "7 the same, after a string holding /* (a later string holds */)"; fresh; fn first "	const char *o = \"/*\";
$BODY" >> src/wl/wl.c; echo 'static const char *shut(void) { return "*/"; }' >> src/wl/wl.c; run
echo "8 copy 1 with its line added to tests/dupes-known.txt"; fresh; fn first "$BODY" >> src/wl/wl.c
python3 tools/dupes.py | sed -n 's/.*(\(.*\))/\1/p' >> tests/dupes-known.txt; run
echo "9 that listed copy then grows by two more identical lines on both sides"
python3 - <<'PY'
more = '''	for (int i = 0; i < MAXT; i++) if (t[i].fn == 0) { t[i].ctx = 0; t[i].at = 0; }
	for (int i = 0; i < MAXT; i++) if (t[i].fn == 0) { t[i].ctx = 0; t[i].at = 1; }'''
for f in ('src/wl/wl.c', 'src/loop/timer.c'):
    s = open(f).read()
    k = s.index('\n', s.rindex('	int ms = d <= 0'))
    open(f, 'w').write(s[:k] + '\n' + more + s[k:])
PY
run
echo "10 a third copy of a listed pair, in the same file (src/tools/pick.c 480..489)"; fresh
python3 - <<'PY'
s = open('src/tools/pick.c').read().split('\n')
a = next(i for i in range(488, 420, -1) if s[i].startswith('{'))
b = next(i for i in range(a, len(s)) if s[i].startswith('}'))
open('src/tools/pick.c', 'w').write('\n'.join(s) + '\nstatic void twin3(void)\n' + '\n'.join(s[a:b + 1]) + '\n')
PY
run
cd "$root"; rm -rf "$tmp"
