#!/usr/bin/env python3
"""Fails on copied code (CLAUDE.md, Davy's no-duplication rule): any run of
MIN tokens, comments and layout aside, that occurs twice in the C sources,
inside function bodies, as PMD's copy-paste detector finds them (its token
windows).
  tools/dupes.py [MIN] [files...]   default: every .c and .h under src/ ref/ include/
Copies listed in tests/dupes-known.txt (file file key, a copy its owner has
yet to fold) pass; exit 1 on any other, or on a listed one that is gone, so
the list only shrinks."""
import re, sys, glob, zlib

TOK = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|\w+|[^\s\w]')
GAP = re.compile(r'/\*.*?\*/|//[^\n]*|^\s*#[^\n]*', re.S | re.M)


def tokens(path):
	src = open(path, encoding='utf-8', errors='replace').read()
	# comments and preprocessor lines blanked, newlines kept so lines stay right
	src = GAP.sub(lambda m: '\n' * m.group().count('\n'), src)
	out, depth, prev, body = [], 0, '', False
	for n, line in enumerate(src.split('\n'), 1):
		for t in TOK.findall(line):
			# only function bodies: tables and declarations repeat by design
			if t == '{' and not depth: body = prev == ')'
			depth += (t == '{') - (t == '}')
			if body and (depth or t == '}'): out.append((t, n))
			prev = t
	return out


def main(argv):
	m = int(argv[0]) if argv and argv[0].isdigit() else 50
	files = [a for a in argv if not a.isdigit()] or sorted(
		f for d in ('src', 'ref', 'include') for f in glob.glob(d + '/**/*.[ch]', recursive=True))
	seen, hits = {}, []
	for f in files:
		t = tokens(f)
		run = False   # inside a copy already reported: report each copy once, by its start
		for i in range(len(t) - m + 1):
			key = tuple(x for x, _ in t[i:i + m])
			g, j = seen.setdefault(key, (f, i))
			hit = g != f or abs(j - i) >= m
			if hit and not run:
				hits.append((g, j, f, i))
			run = hit
	known = {l.strip() for l in open('tests/dupes-known.txt') if l.strip() and l[0] != '#'}
	toks = {f: tokens(f) for f in {h[0] for h in hits} | {h[2] for h in hits}}
	bad = 0
	for g, j, f, i in hits:
		k = f'{g} {f} {zlib.crc32(" ".join(x for x, _ in toks[f][i:i + m]).encode()):08x}'
		if k in known: known.discard(k); continue
		print(f'FAIL copied: {g}:{toks[g][j][1]} {f}:{toks[f][i][1]}   ({k})')
		bad = 1
	for k in sorted(known): print(f'FAIL no longer copied, delete from tests/dupes-known.txt: {k}')
	return 1 if bad or known else 0

sys.exit(main(sys.argv[1:]))
