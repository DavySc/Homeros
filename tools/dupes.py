#!/usr/bin/env python3
"""Fails on copied code (CLAUDE.md, Davy's no-duplication rule), three ways:
 - copied: any run of MIN tokens, comments and layout aside, that occurs twice
   in the C sources, inside function bodies, as PMD's copy-paste detector
   finds them (its token windows);
 - renamed: two functions of SHAPE tokens or more whose bodies are the same
   once their local names are renamed in order of first use (calls, fields,
   UPPER_CASE names, numbers and strings stay: they are what the code does);
 - owned: a mechanism's telltale (tests/owners.txt) outside its owner files,
   the same job written differently, which no token comparison can see.
  tools/dupes.py [MIN] [files...]   default: every .c and .h under src/ ref/ include/
The last two read only the editor's modules (src/*/, ref/, include/): the
programs in src/*/*/ (benches, probes, the infoview server) are separate
programs, like tests/, not the editor.
Findings listed in tests/dupes-known.txt (a copy its owner has yet to fold)
pass; exit 1 on any other, or on a listed one that is gone, so the list only
shrinks."""
import re, sys, glob, zlib, fnmatch

TOK = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|\w+|[^\s\w]')
# strings first, so a "/*" inside one starts no comment
GAP = re.compile(r'("(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|/\*.*?\*/|//[^\n]*|^\s*#[^\n]*', re.S | re.M)
KW = set('if else for while do switch case default return break continue goto sizeof '
	'int char long short unsigned signed void const static struct union enum size_t float double'.split())
SHAPE = 15


def clean(path):
	"""the source's lines with comments and preprocessor lines blanked, so
	line numbers stay right"""
	src = open(path, encoding='utf-8', errors='replace').read()
	return GAP.sub(lambda m: m.group(1) or '\n' * m.group().count('\n'), src).split('\n')


def tokens(lines):
	out, depth, prev, body = [], 0, '', False
	for n, line in enumerate(lines, 1):
		for t in TOK.findall(line):
			# only function bodies: tables and declarations repeat by design
			if t == '{' and not depth: body = prev == ')'
			depth += (t == '{') - (t == '}')
			if body and (depth or t == '}'): out.append((t, n))
			prev = t
	return out


def shapes(toks):
	"""each function body of SHAPE tokens or more, by its first line, with its
	locals renamed; (void)x; and other unused-argument casts dropped"""
	body, depth = [], 0
	for t, n in toks:
		if not body: start = n
		body.append(t)
		depth += (t == '{') - (t == '}')
		if depth: continue
		b = re.sub(r'\( void \) \w+ ;', '', ' '.join(body)).split()
		body = []
		if len(b) < SHAPE: continue
		names, s = {}, []
		for i, t in enumerate(b):
			keep = (t in KW or not re.match(r'[A-Za-z_]', t) or t.isupper() or b[i + 1:i + 2] == ['(']
				or b[i - 1] == '.' or b[i - 2:i] == ['-', '>'])
			s.append(t if keep else '$%d' % names.setdefault(t, len(names)))
		yield start, ' '.join(s)


def main(argv):
	m = int(argv[0]) if argv and argv[0].isdigit() else 50
	files = [a for a in argv if not a.isdigit()] or sorted(
		f for d in ('src', 'ref', 'include') for f in glob.glob(d + '/**/*.[ch]', recursive=True))
	mods = [f for f in files if f.count('/') <= 2 - f.startswith(('ref/', 'include/'))]
	hits = []   # (known-list key, message)
	src = {f: clean(f) for f in files}
	seen, toks = {}, {f: tokens(src[f]) for f in files}
	for f in files:
		t = toks[f]
		run = False   # inside a copy already reported: report each copy once, by its start
		for i in range(len(t) - m + 1):
			key = tuple(x for x, _ in t[i:i + m])
			g, j = seen.setdefault(key, (f, i))
			hit = g != f or abs(j - i) >= m
			if hit and not run:
				hits.append((f'{g} {f} {zlib.crc32(" ".join(key).encode()):08x}',
					f'copied: {g}:{toks[g][j][1]} {f}:{t[i][1]}'))
			run = hit
	seen = {}
	for f in mods:
		for n, s in shapes(toks[f]):
			g, j = seen.setdefault(s, (f, n))
			if j != n or g != f:
				hits.append((f'{g} {f} {zlib.crc32(s.encode()):08x}', f'renamed copy: {g}:{j} {f}:{n}'))
	for name, rx, own in (l.rstrip('\n').split(' | ') for l in open('tests/owners.txt') if l.strip() and l[0] != '#'):
		for f in mods:
			if f.startswith('include/') or any(fnmatch.fnmatch(f, o) for o in own.split()): continue
			for n, line in enumerate(src[f], 1):
				if re.search(rx, line):
					hits.append((f'{name} {f}', f'{name} outside {own}: {f}:{n}'))
	# a list, not a set: a window copied three times is listed twice
	known = [l.strip() for l in open('tests/dupes-known.txt') if l.strip() and l[0] != '#']
	bad = 0
	for k, msg in hits:
		if k in known: known.remove(k); continue
		print(f'FAIL {msg}   ({k})')
		bad = 1
	for k in sorted(known): print(f'FAIL no longer copied, delete from tests/dupes-known.txt: {k}')
	return 1 if bad or known else 0

sys.exit(main(sys.argv[1:]))
