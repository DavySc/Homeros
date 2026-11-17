#!/usr/bin/env python3
"""Differential test of src/org (package 16) against Org's own parser.

    org_conform.py DRIVER N SEED     N random org texts, fed to both
    org_conform.py DRIVER FILE...    given files

DRIVER is tests/org_drv.c built; Org runs as emacs -Q --batch with
tools/org_dump.el.  Prints the first difference of each failing text (the
text is kept in build/org-fail-*.org) and "N differ"."""
import os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WORDS = ['a', 'b', 'x', 'TODO', 'DONE', 'NEXT', 'WAIT', 'CANCELLED', 'todo', 'COMMENT', '[#A]', '[#a]',
         '[#1]', '[#12]', '[#65]', ':a:', ':b:c:', '::', ':@x_1:', '[fn:1]', '*', '-', '+', '1.', 'a)',
         '|', '#', '#+', ':', '[', ']', '<2026-11-22 Sun>', '[2026-11-22 Sun 09:00]', 'é', 'λ', '→', '\u00a0', '\u3000', '\f', '\r']


def word(r):
    return r.choice(WORDS) if r.random() < .5 else ''.join(r.choice('ab xé:*-#+|[]') for _ in range(r.randint(1, 4)))


def text(r, k=3):
    return ' '.join(word(r) for _ in range(r.randint(0, k)))


def indent(r):
    return r.choice(['', '', '', ' ', '  ', '   ', '\t', ' \t', '    ', '      ', ' ' * r.randint(0, 12)])


def ts(r, c='['):
    d = '2026-11-%02d' % r.randint(1, 30)
    e = {'[': ']', '<': '>'}[c]
    return c + d + r.choice(['', ' Sun', ' Mon 09:00', ' Sun .+1d', ' Sun .+1d/3d', ' x]y']) + e


BLOCKS = ['src', 'SRC', 'example', 'quote', 'center', 'verse', 'export', 'comment', 'foo', 'src:']   # no regexp characters: see src/org/org.c's first comment


def line(r):
    k = r.randint(0, 31)
    i = indent(r)
    if k == 0:
        return '*' * r.choice([1, 1, 2, 3]) + r.choice([' ', ' ', ' ', '', '\t']) + text(r, 5)
    if k == 1:
        return i + r.choice(['SCHEDULED:', 'DEADLINE:', 'CLOSED:', 'scheduled:']) + ' ' + ts(r, r.choice('<[')) + r.choice(['', ' DEADLINE: ' + ts(r, '<')])
    if k == 2:
        return i + r.choice([':PROPERTIES:', ':properties:', ':END:', ':end:', ':LOGBOOK:', ':a-b_c:', ':é:', ':x:y:'])
    if k == 3:
        return i + r.choice([':EFFORT: 0:12', ':STYLE: habit', ':ID:', ':a:b', ':x+: 1', ': x', ':', ':A:\tb'])
    if k == 4:
        return i + r.choice(['CLOCK: ', 'clock: ', 'CLOCK:\t']) + r.choice([
            ts(r) + '--' + ts(r) + ' =>  1:30', ts(r), ts(r) + ' x', '=> 0:10', ts(r) + '--' + ts(r), ts(r) + '--' + ts(r) + ' => 1:3'])
    if k == 5:
        return i + r.choice(['# ', '#', '#x ']) + text(r)
    if k == 6:
        return i + '#+' + r.choice(['NAME', 'name', 'CAPTION', 'CAPTION[x y]', 'RESULTS', 'RESULTS[ab]', 'ATTR_HTML',
                                    'attr_latex', 'HEADER', 'TBLNAME', 'TITLE', 'a:b', 'x[y]', 'CALL', 'TBLFM', 'STARTUP']) + r.choice([':', ': ', '', ' :']) + text(r)
    if k in (7, 8):
        b = r.choice(BLOCKS)
        return i + r.choice(['#+begin_', '#+BEGIN_', '#+end_', '#+END_']) + b + r.choice(['', ' ', ' python -n 3 :var x=1'])
    if k == 9:
        return i + r.choice(['#+BEGIN: clocktable :scope file', '#+begin: x', '#+BEGIN:', '#+END:', '#+end', '#+END'])
    if k in (10, 11, 12):
        return i + r.choice(['-', '+', '*', '1.', '2)', '10.', 'a.', '-', '-']) + r.choice([' ', ' ', '  ', '\t', '']) + r.choice(['', '', '[ ] ', '[X] ', '[x] ', '[-] ', '[@3] ', '[@start:2] ']) + text(r) + r.choice(['', '', ' :: ' + text(r), ' ::', ':: x'])
    if k == 13:
        return i + r.choice(['|', '| a | b |', '|-', '|---+---|', '| x', '+--+--+', '+--+', '| a |\t'])
    if k == 14:
        return i + r.choice(['#+TBLFM: $2=$1', '#+tblfm: x', '#+TBLFM:'])
    if k == 15:
        return i + r.choice([': x', ':', ':  y', ':x'])
    if k == 16:
        return i + r.choice(['-----', '------  ', '----', '- - - - -'])
    if k == 17:
        return r.choice(['%%(diary-anniversary 1 1 2000)', '%%x', ' %%(x)'])
    if k == 18:
        return r.choice(['[fn:1] ', '[fn:a-b] ', '[FN:x]', '[fn:] ', '[fn:1]']) + text(r)
    if k == 19:
        return i + r.choice(['\\begin{equation}', '\\end{equation}', '\\begin{align*} x', '\\end{align*}', '\\begin{x}\\end{x}', '\\BEGIN{x}'])
    if k in (20, 21, 22, 23):
        return ''
    if k == 24:
        return i
    return i + text(r, 6)


def gen(r):
    return '\n'.join(line(r) for _ in range(r.randint(1, 30))) + r.choice(['\n', '\n', '', '\n\n', '\n  '])


def run(driver, files):
    el = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(HERE, 'org_dump.el')] + files,
                        capture_output=True)
    c = subprocess.run([driver] + files, capture_output=True)
    if c.returncode:
        sys.exit('driver failed: ' + c.stderr[-2000:].decode('utf-8', 'replace'))

    def split(out):
        d, cur = {}, None
        for l in out.split('\n'):
            if not l and cur is None:
                continue
            if l.startswith('== '):
                cur = l[3:]
                d[cur] = []
            elif l:
                d[cur].append(l)
        return d
    # bytes: a title may hold \r or \f, which text mode would take for line ends
    return split(el.stdout.decode('utf-8', 'replace')), split(c.stdout.decode('utf-8', 'replace'))


def main():
    driver = sys.argv[1]
    os.makedirs('build', exist_ok=True)
    tmp = tempfile.mkdtemp()
    if len(sys.argv) == 4 and sys.argv[2].isdigit():
        n, seed = int(sys.argv[2]), int(sys.argv[3])
        r = random.Random(seed)
        files = []
        for i in range(n):
            p = os.path.join(tmp, 't%05d.org' % i)
            with open(p, 'w', encoding='utf-8', newline='') as f:
                f.write(gen(r))
            files.append(p)
        print('org_conform: %d texts, seed %d' % (n, seed))
    else:
        files = sys.argv[2:]
    bad = errors = 0
    for k in range(0, len(files), 400):
        part = files[k:k + 400]
        el, c = run(driver, part)
        for p in part:
            name = os.path.basename(p)
            a, b = el.get(name), c.get(name)
            if a and a[-1].startswith('!error'):
                errors += 1   # no answer from Org to compare with
                continue
            if a == b:
                continue
            bad += 1
            if bad <= 5:
                keep = os.path.join('build', 'org-fail-' + name)
                with open(p, 'rb') as f, open(keep, 'wb') as g:
                    g.write(f.read())
                a, b = a or ['(no output)'], b or ['(no output)']
                j = next((j for j in range(max(len(a), len(b))) if j >= len(a) or j >= len(b) or a[j] != b[j]), 0)
                print('%s (kept as %s): line %d\n  org: %s\n  ours: %s' % (
                    name, keep, j + 1, a[j] if j < len(a) else '(end)', b[j] if j < len(b) else '(end)'))
    print('%d of %d differ (Org failed on %d)' % (bad, len(files), errors))
    sys.exit(1 if bad else 0)


main()
