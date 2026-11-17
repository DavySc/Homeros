"""Random org text for the oracle: lines drawn from the constructs Org's
parser tells apart, near misses included (wrong case, missing ends, stray
blanks, tabs, CR, NBSP, non-ASCII), so the parser meets the edge of each rule.
python3 gen.py SEED COUNT DIR  -> DIR/r<SEED>-<i>.org
python3 gen.py SEED COUNT DIR FILE...  -> DIR/m<SEED>-<i>.org: the files with
random edits (lines deleted, duplicated, moved, re-indented; bytes cut;
snippets inserted)."""
import random, sys, os

W = ['foo', 'bar', 'x', 'Ä', 'été', '日本', '*', '/a/', '=v=', '::', ':', '[#A]', '<2026-11-22 Sun>',
     '[2026-11-22 Sun]', 'CLOCK:', '#+', '|', '-', '+', '1.', 'a)', '\\\\', '%%(', '[fn:1]', 'TODO',
     'COMMENT', ':a:b:', ' ', ' ', '$', "'", 'SCHEDULED:', '#', '@', '%']

def ts(r, active=None):
    a = r.random() < 0.5 if active is None else active
    o, c = ('<', '>') if a else ('[', ']')
    s = '%04d-%02d-%02d' % (r.choice([2026, 1999, 26]), r.randint(0, 13), r.randint(0, 32))
    if r.random() < 0.8: s += ' ' + r.choice(['Mon', 'Sun', 'zo', 'Dé', ''])
    if r.random() < 0.4: s += ' %d:%02d' % (r.randint(0, 25), r.randint(0, 61))
    if r.random() < 0.2: s += '-%d:%02d' % (r.randint(0, 25), r.randint(0, 61))
    if r.random() < 0.4: s += ' ' + r.choice(['+', '++', '.+', '+']) + str(r.randint(0, 12)) + r.choice('hdwmyDx')
    if r.random() < 0.2: s += '/' + str(r.randint(1, 9)) + r.choice('hdwmy')
    if r.random() < 0.3: s += ' ' + r.choice(['-', '--']) + str(r.randint(0, 9)) + r.choice('hdwmy')
    if r.random() < 0.05: c = r.choice(['>', ']', ''])
    t = o + s + c
    if r.random() < 0.15: t += '--' + ts(r, a)
    if r.random() < 0.05: t = '<%%(diary-float t 4 2)' + r.choice(['', ' 10:00', ' 9:30-11:00']) + '>'
    return t

def words(r, n=None):
    return ' '.join(r.choice(W) for _ in range(r.randint(0, 6) if n is None else n))

def ind(r):
    return r.choice(['', '', '', ' ', '  ', '\t', '    ', '   '])

def case(r, s):
    return s.lower() if r.random() < 0.15 else s

def line(r):
    k = r.randint(0, 40)
    if k < 4:
        s = '*' * r.choice([1, 1, 2, 3, 4, 15]) + r.choice([' ', ' ', ' ', '', '\t'])
        if r.random() < 0.5: s += r.choice(['TODO', 'NEXT', 'WAIT', 'DONE', 'CANCELLED', 'todo', 'TODOx']) + r.choice([' ', '', '  '])
        if r.random() < 0.3: s += r.choice(['[#A] ', '[#a] ', '[#5] ', '[#64] ', '[#Z]', '[# A] '])
        if r.random() < 0.2: s += r.choice(['COMMENT ', 'COMMENT', 'comment '])
        s += words(r)
        if r.random() < 0.3: s += r.choice([' :work:', '\t:a:b:', ' :é:', ' ::', ' :a b:', ' :x:  ', ':y:'])
        return s
    if k < 6:
        return ind(r) + ' '.join(case(r, r.choice(['SCHEDULED:', 'DEADLINE:', 'CLOSED:'])) + r.choice([' ', '', '  ']) + ts(r)
                                for _ in range(r.randint(1, 3)))
    if k < 8:
        return ind(r) + case(r, r.choice([':PROPERTIES:', ':END:', ':LOGBOOK:', ':MyDrawer:', ':end:', ':é:', ':a b:', ':PROPERTIES: x']))
    if k < 10:
        return ind(r) + ':' + r.choice(['ID', 'STYLE', 'a:b', 'X+', '', 'é']) + ':' + r.choice(['', ' ', ' v', '\tv w', 'v'])
    if k < 11:
        s = ind(r) + case(r, 'CLOCK:') + r.choice([' ', '\t', ''])
        return s + r.choice([ts(r, False), ts(r, False) + '--' + ts(r, False) + ' =>  %d:%02d' % (r.randint(0, 99), r.randint(0, 99)), '=> 1:00', ' => 1:00'])
    if k < 15:
        b = r.choice(['-', '+', '*', '1.', '2)', 'a.', '10.', '-', '-'])
        s = ind(r) + b + r.choice([' ', ' ', '', '\t', '  '])
        if r.random() < 0.1: s += r.choice(['[@3] ', '[@start:2] ', '[@b] '])
        if r.random() < 0.3: s += r.choice(['[ ] ', '[X] ', '[x] ', '[-] ', '[X]', '[?] '])
        if r.random() < 0.2: s += words(r, 1) + r.choice([' :: ', ' ::', ':: ', ' :: x'])
        return s + words(r)
    if k < 17:
        return ind(r) + r.choice(['|', '| a | b |', '|---+---|', '|-', '+---+---+', '| x', '+--+', '|é|'])
    if k < 18:
        return case(r, r.choice(['#+TBLFM: $2=$1', '#+TBLFM:', '#+tblfm: @1=2']))
    if k < 22:
        n = r.choice(['SRC', 'EXAMPLE', 'QUOTE', 'CENTER', 'VERSE', 'COMMENT', 'EXPORT', 'foo', 'é', 'a.b', 'SRC'])
        e = r.choice(['BEGIN_', 'END_', 'BEGIN_', 'END_', 'begin_', 'end_'])
        s = ind(r) + '#+' + e + n
        if e.lower() == 'begin_' and r.random() < 0.5: s += r.choice([' python', ' c -n 2 :results output', ' html', ' -i', ''])
        return s
    if k < 23:
        return ind(r) + case(r, r.choice(['#+BEGIN: clocktable :scope file', '#+END:', '#+END', '#+BEGIN:', '#+BEGIN: x']))
    if k < 26:
        return ind(r) + case(r, r.choice(['#+NAME: tbl', '#+CAPTION[s]: c', '#+CAPTION: c', '#+RESULTS:', '#+RESULTS[ab]:',
                                          '#+ATTR_HTML: :w 1', '#+TITLE: t', '#+CALL: f(x=1)', '#+', '#+FOO[x]: y', '#+HEADER: :var',
                                          '#+TBLNAME: t', '#+a:b: c', '#+:']))
    if k < 27:
        return ind(r) + r.choice(['#', '# c', '#c', '# '])
    if k < 28:
        return ind(r) + r.choice([':', ': x', ':x'])
    if k < 29:
        return ind(r) + r.choice(['-----', '-----  ', '----', '--------x'])
    if k < 30:
        return r.choice(['\\begin{equation}', '\\end{equation}', '\\begin{a*} x \\end{a*}', '  \\begin{align}', '\\end{align}  ', '\\begin{x}'])
    if k < 31:
        return r.choice(['%%(diary-anniversary 1 1 2000)', ' %%(x)', '%%'])
    if k < 33:
        return r.choice(['[fn:1] note', '[fn:é]', '[fn:a-b]x', '[fn:]', ' [fn:1] x', '[fn:x_y] ' + words(r)])
    if k < 37:
        return ''
    if k < 38:
        return r.choice([' ', '\t', '  \t', '\r', ' '])
    return ind(r) + words(r)

def snippet(r):
    k = r.randint(0, 8)
    if k == 0:
        out = [line(r) if r.random() < 0.2 else '*' * r.randint(1, 3) + ' ' + words(r)]
        if r.random() < 0.6: out.append(ind(r) + ' '.join(r.choice(['SCHEDULED: ', 'DEADLINE: ', 'CLOSED: ']) + ts(r) for _ in range(r.randint(1, 2))))
        if r.random() < 0.6:
            out.append(ind(r) + case(r, ':PROPERTIES:'))
            out += [ind(r) + ':' + r.choice(['ID', 'STYLE', 'X+', 'a:b', 'CUSTOM_ID', '']) + ':' + r.choice(['', ' v', ' habit', '  x y']) for _ in range(r.randint(0, 3))]
            if r.random() < 0.15: out.append(line(r))
            out.append(ind(r) + case(r, ':END:'))
        if r.random() < 0.4:
            out.append(ind(r) + r.choice([':LOGBOOK:', ':logbook:', ':NOTES:']))
            out += [ind(r) + 'CLOCK: ' + ts(r, False) + '--' + ts(r, False) + ' =>  1:%02d' % r.randint(0, 59) for _ in range(r.randint(0, 3))]
            if r.random() < 0.3: out.append('- State "DONE" from "TODO" ' + ts(r, False))
            if r.random() < 0.9: out.append(ind(r) + ':END:')
        return out
    if k == 1:
        n = r.choice(['SRC', 'EXAMPLE', 'QUOTE', 'CENTER', 'VERSE', 'COMMENT', 'EXPORT', 'foo', 'src', 'a.b'])
        out = [ind(r) + '#+BEGIN_' + n + r.choice(['', ' python', ' c -n :results output', ' html'])]
        out += [line(r) for _ in range(r.randint(0, 4))]
        if r.random() < 0.85: out.append(ind(r) + case(r, '#+END_' + (n if r.random() < 0.9 else 'SRC')) + r.choice(['', ' ', ' x']))
        return out
    if k == 2:
        out = []
        for _ in range(r.randint(1, 6)):
            out.append(r.choice(['', ' ', '  ', '    ', '\t']) + r.choice(['-', '+', '1.', '2)', '*', 'a.']) + ' ' + r.choice(['', '[X] ', '[ ] ', 'tag :: ']) + words(r))
            for _ in range(r.randint(0, 2)): out.append(r.choice(['', ' ', '  ', '     ']) + r.choice([words(r), '', line(r)]))
        return out
    if k == 3:
        out = [ind(r) + r.choice(['#+NAME: t', '#+CAPTION: c', '#+TBLNAME: x', '#+RESULTS:'])] if r.random() < 0.5 else []
        out += [ind(r) + r.choice(['| a | b |', '|---+---|', '| 1 | 2 |', '|']) for _ in range(r.randint(1, 4))]
        if r.random() < 0.4: out.append(r.choice(['#+TBLFM: $2=$1', '#+TBLFM: x', '  #+TBLFM: @1=1']))
        return out
    if k == 4:
        out = ['+---+---+'] + [r.choice(['| a | b |', '+---+---+', '|   |   |', 'x']) for _ in range(r.randint(0, 4))]
        return out
    if k == 5:
        return [r.choice(['[fn:1] note', '[fn:a]', '[fn:x] y'])] + [r.choice([words(r), '', '  ' + words(r), line(r)]) for _ in range(r.randint(0, 4))]
    if k == 6:
        return [r.choice(['#+NAME: n', '#+ATTR_HTML: :w 1', '#+CAPTION[s]: c', '#+HEADER: :var x=1', '#+RESULTS[h]:'])
                for _ in range(r.randint(1, 3))] + [line(r) for _ in range(r.randint(0, 2))]
    if k == 7:
        n = r.choice(['equation', 'align*', 'x'])
        return ['\\begin{%s}' % n] + [words(r) for _ in range(r.randint(0, 2))] + (['\\end{%s}' % n] if r.random() < 0.8 else [])
    return [ind(r) + case(r, r.choice([':DRAWER:', ':PROPERTIES:', ':A-b_c:']))] + [line(r) for _ in range(r.randint(0, 3))] + ([ind(r) + case(r, ':END:')] if r.random() < 0.8 else [])

def doc(r):
    out = []
    for _ in range(r.randint(0, 25)):
        if r.random() < 0.3: out += snippet(r)
        else: out.append(line(r))
    s = '\n'.join(out)
    if r.random() < 0.8: s += '\n'
    return s

def mutate(r, text):
    ls = text.split('\n')
    for _ in range(r.randint(1, 30)):
        if not ls: ls = ['']
        i = r.randrange(len(ls))
        k = r.randint(0, 7)
        if k == 0: del ls[i]
        elif k == 1: ls.insert(i, ls[r.randrange(len(ls))])
        elif k == 2: ls[i] = ind(r) + ls[i].lstrip(' \t')
        elif k == 3: ls[i:i] = snippet(r)
        elif k == 4: ls.insert(i, line(r))
        elif k == 5 and ls[i]: j = r.randrange(len(ls[i])); ls[i] = ls[i][:j] + ls[i][j + r.randint(1, 5):]
        elif k == 6: ls.insert(i, '')
        else: ls[i] = ls[i].upper() if r.random() < 0.5 else ls[i].lower()
    return '\n'.join(ls)

seed, count, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
srcs = [open(f, encoding='utf-8').read() for f in sys.argv[4:]]
os.makedirs(out, exist_ok=True)
for i in range(count):
    r = random.Random(seed * 1000003 + i)
    name = ('m%d-%d.org' if srcs else 'r%d-%d.org') % (seed, i)
    with open(os.path.join(out, name), 'w', encoding='utf-8', newline='') as f:
        f.write(mutate(r, r.choice(srcs)) if srcs else doc(r))
