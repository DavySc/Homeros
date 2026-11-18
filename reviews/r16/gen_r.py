#!/usr/bin/env python3
"""Reviewer's generator of org text for the package 16 comparison, written
without looking at what either build's generator leaves out on purpose.
gen_r.py SEED COUNT DIR   -> DIR/q<SEED>-<i>.org
Three kinds: line soup (near misses of every rule), realistic outlines (the
shape of an agenda file, then damaged), and stress shapes (deep lists, long
lines, many brackets)."""
import random, sys, os

BLOCK = ['src', 'SRC', 'Src', 'example', 'quote', 'QUOTE', 'center', 'verse', 'export', 'comment',
         'foo', 'c++', 'a.b', 'x*', 'a[b]', 'É', 'é', 'ß', 'İ', 'ǅ', 'a\\b', '^x', 'x$', 'a?', 'src:', 'a+b']
DRAWER = ['PROPERTIES', 'properties', 'Properties', 'END', 'end', 'End', 'LOGBOOK', 'logbook', 'NOTES',
          'a-b_c', 'É', 'é', "a'b", 'a$b', 'a%', 'x y', 'a:b', '日本', 'a\u00a0b', '']
WORD = ['a', 'bb', 'x', 'TODO', 'DONE', 'NEXT', 'WAIT', 'CANCELLED', 'todo', 'Todo', 'COMMENT', 'comment',
        '[#A]', '[#a]', '[#Z]', '[#0]', '[#9]', '[#10]', '[#64]', '[#65]', '[#07]', '[#]', ':a:', ':b:c:',
        '::', ':@x_1:', ':é:', ':a-b:', ':a b:', ':%#:', '[fn:1]', '[fn:x]', '*', '**', '-', '+', '1.', 'a)',
        '|', '#', '#+', ':', '[', ']', '(', ')', '[[', ']]', '<2026-11-22 Sun>', '[2026-11-22 Sun 09:00]',
        '<2026-11-22 Sun 9:00-10:00 +1w -2d>', 'é', 'É', 'λ', '→', '\u00a0', '\u3000', '\u2003', '\f', '\r',
        '\u200b', '\x0b', '=>', '1:30', 'CLOCK:', 'SCHEDULED:', '\\begin{x}', '\\end{x}', '%%(', '$', "'", '%', '\\']

def words(r, k=5):
    return ' '.join(r.choice(WORD) for _ in range(r.randint(0, k)))

def ind(r):
    return r.choice(['', '', '', '', ' ', '  ', '   ', '    ', '\t', ' \t', '\t ', '        ', '\u00a0', ' ' * r.randint(0, 10)])

def ts(r, c=None):
    c = c or r.choice('[<')
    e = ']' if c == '[' else '>'
    d = r.choice(['2026-11-22', '2026-1-22', '20261-11-22', '2026-11-222', '2026-11-22'])
    t = r.choice(['', ' Sun', ' zo.', ' Sun 9:00', ' Sun 09:00-10:00', ' Sun +1d', ' Sun .+1d/3d', ' Sun ++2w -3d', ' ]', ' x]y', ' >', '\tSun', '  Sun'])
    if r.random() < .05: e = ''
    return c + d + t + e

def line(r):
    k = r.randint(0, 36)
    i = ind(r)
    if k < 3:
        return '*' * r.choice([1, 1, 1, 2, 2, 3, 4, 7, 15, 16]) + r.choice([' ', ' ', ' ', '  ', '', '\t', ' \t', '\u00a0']) + words(r, 6) + r.choice(['', '', ' :a:', '\t:b:c:', ' :a: ', ' :a:\t', ':a:', ' ::', ' :a::b:', '  :é_1@:', ' :a :'])
    if k == 3:
        return i + ' '.join(r.choice(['SCHEDULED:', 'DEADLINE:', 'CLOSED:', 'scheduled:', 'Deadline:', 'SCHEDULED']) + r.choice([' ', '', '\t']) + ts(r) for _ in range(r.randint(1, 3)))
    if k == 4:
        return i + ':' + r.choice(DRAWER) + ':' + r.choice(['', '', '', ' ', '\t', ' x', '\r'])
    if k == 5:
        return i + ':' + r.choice(['ID', 'STYLE', 'a+', 'É', 'a:b', '', ' ', 'x y']) + ':' + r.choice(['', ' ', ' v', '\tv', 'v', ' v ', '  ', ' :x:'])
    if k == 6:
        return i + r.choice(['CLOCK:', 'clock:', 'Clock:', 'CLOCK']) + r.choice([' ', '\t', '', '  ']) + r.choice([
            ts(r, '[') + '--' + ts(r, '[') + r.choice([' =>  1:30', ' => 1:3', '=> 1:30', ' =>1:30', ' => 123:45 ', '\t=>\t0:00', '']),
            ts(r, '['), ts(r, '<'), ts(r, '[') + ' x', '=> 0:10', ' => 0:10', ts(r, '[') + '-' + ts(r, '['), ts(r, '[') + '--' + ts(r, '<') + ' => 1:00'])
    if k == 7:
        return i + r.choice(['# ', '#', '#x', '#\t', '# ' + words(r)])
    if k in (8, 9):
        kw = r.choice(['NAME', 'name', 'CAPTION', 'caption', 'CAPTION[x y]', 'CAPTION[x]', 'CAPTION[]', 'CAPTION[a]b]', 'RESULTS', 'RESULTS[ab]',
                       'NAME[x]', 'ATTR_HTML', 'attr_latex', 'ATTR_', 'ATTR_é', 'HEADER', 'HEADERS', 'TBLNAME', 'SOURCE', 'SRCNAME', 'RESNAME',
                       'RESULT', 'DATA', 'LABEL', 'PLOT', 'TITLE', 'a:b', 'x[y]', '[x]', 'x[y]z'] + ([] if os.environ.get('NOCALL') else ['CALL', 'call']) + [ 'TBLFM', 'STARTUP', 'É', '', 'BEGIN', 'END'])
        return i + '#+' + kw + r.choice([':', ':', ': ', '', ' :', ':\t']) + words(r)
    if k in (10, 11):
        b = r.choice(BLOCK)
        return i + r.choice(['#+begin_', '#+BEGIN_', '#+end_', '#+END_', '#+Begin_', '#+End_']) + b + r.choice(['', '', ' ', '\t', ' python -n 3 :var x=1', ' x'])
    if k == 12:
        return i + r.choice(['#+BEGIN: clocktable :scope file', '#+begin: x', '#+BEGIN:', '#+BEGIN: ', '#+BEGIN:\u00a0x', '#+BEGIN:x', '#+END:', '#+end', '#+END', '#+END: ', '#+END:x', '#+end:\t'])
    if k in (13, 14, 15):
        return i + r.choice(['-', '+', '*', '1.', '2)', '10.', 'a.', 'A)', '-', '-', '1', '1.)', '١.']) + r.choice([' ', ' ', '  ', '\t', '', '\u00a0']) + \
            r.choice(['', '', '[ ] ', '[X] ', '[x] ', '[-] ', '[@3] ', '[@start:2] ', '[@a] ', '[@3][X] ', '[@3] [ ] ', '[X]', '[ ]x ', '[@] ', '[@start:] ']) + \
            words(r) + r.choice(['', '', ' :: ' + words(r), ' ::', ':: x', ' :: a :: b', '\t::\t'])
    if k == 16:
        return i + r.choice(['|', '| a | b |', '|-', '|---+---|', '| x', '+--+--+', '+--+', '+-+ ', '+--', '+', '| a |\t', '|\t', '||', '|é| '])
    if k == 17:
        return i + r.choice(['#+TBLFM: $2=$1', '#+tblfm: x', '#+TBLFM:', '#+TBLFM:  ', '#+TBLFM:\tx', '#+TBLFM: x  '])
    if k == 18:
        return i + r.choice([': x', ':', ':  y', ':x', ':\t', ': '])
    if k == 19:
        return i + r.choice(['-----', '------  ', '----', '- - - - -', '-----x', '-----\t', '--------------------'])
    if k == 20:
        return r.choice(['%%(diary-anniversary 1 1 2000)', '%%x', ' %%(x)', '%%(', '%%'])
    if k == 21:
        return r.choice(['[fn:1] ', '[fn:a-b] ', '[FN:x]', '[Fn:x] y', '[fn:] ', '[fn:1]', '[fn:é]', "[fn:a'b]", '[fn:a b]', ' [fn:1]', '[fn:1][fn:2]']) + words(r)
    if k == 22:
        return i + r.choice(['\\begin{equation}', '\\end{equation}', '\\begin{align*} x', '\\end{align*}', '\\begin{x}\\end{x}', '\\BEGIN{x}', '\\end{X}', '\\begin{x} y \\end{x} z',
                             '\\begin{}', '\\begin{a b}', '\\end{x} ', '\\end{x}\t', 'x \\end{x}', '\\begin{é}', '\\end{é}'])
    if k == 23 and os.environ.get('NOCALL'): return ''
    if k == 23:   # babel calls: brackets and parentheses, closed here, later or never
        return i + r.choice(['#+CALL:', '#+call:', '#+CALL: ']) + r.choice(['f', 'f ', '', 'a b']) + r.choice(['', '[', '[x]', '[x', '[[x]', ']', '[:results output]']) + \
            r.choice(['', '(', '(x=1)', '(x=(1', '((', ')', '(x=1) ', '(x)[y]', '(x)[y']) + r.choice(['', '', ' ' + words(r)])
    if k == 24:
        return r.choice([']', ')', '])', ')]', ' ]', '] x', ') y ('])
    if k < 30:
        return ''
    if k == 30:
        return r.choice([' ', '\t', '  \t', '\r', '\u00a0', '\f', ' \r'])
    return i + words(r, 7)

def soup(r):
    s = '\n'.join(line(r) for _ in range(r.randint(1, 40))) + r.choice(['\n', '\n', '', '\n\n', '\n  ', '\n\t\n'])
    if r.random() < .05: s = s.replace('\n', '\r\n')
    return s

def outline(r):
    """An agenda file's shape."""
    out = []
    if r.random() < .5:
        out += ['#+TITLE: t', '#+STARTUP: overview', '#+FILETAGS: :x:'][:r.randint(1, 3)]
        if r.random() < .3: out += ['# c', ':PROPERTIES:', ':ID: 1', ':END:']
        if r.random() < .5: out += ['', 'Intro ' + words(r), '']
    lvl = 1
    for _ in range(r.randint(1, 25)):
        lvl = max(1, min(6, lvl + r.choice([-2, -1, 0, 0, 0, 1, 1])))
        h = '*' * lvl + ' ' + r.choice(['', '', 'TODO ', 'NEXT ', 'WAIT ', 'DONE ', 'CANCELLED ']) + r.choice(['', '', '[#A] ', '[#C] ']) + \
            r.choice(['Buy milk', 'Write report for é', 'Call [[id:x][Bob]]', 'COMMENT hidden', 'Review PR #12', '']) + r.choice(['', '', '   :work:', ' :home:errand:'])
        out.append(h)
        if r.random() < .5:
            out.append(' '.join(r.sample(['SCHEDULED: <2026-11-22 Sun .+1d/3d>', 'DEADLINE: <2026-12-01 Tue -3d>', 'CLOSED: [2026-11-20 Fri 10:00]'], r.randint(1, 3))))
        if r.random() < .5:
            out += [':PROPERTIES:'] + r.sample([':STYLE:    habit', ':ID:       7f3a', ':EFFORT:   0:30', ':CUSTOM_ID: x', ':LAST_REPEAT: [2026-11-21 Sat 08:00]'], r.randint(0, 4)) + [':END:']
        if r.random() < .4:
            out += [':LOGBOOK:'] + [r.choice(['CLOCK: [2026-11-01 Sun 09:00]--[2026-11-01 Sun 10:30] =>  1:30', 'CLOCK: [2026-11-02 Mon 09:00]',
                                               '- State "DONE"       from "TODO"       [2026-11-21 Sat 08:00]', '- Note taken on [2026-11-20 Fri 12:00] \\\\', '  the note']) for _ in range(r.randint(0, 5))] + [':END:']
        for _ in range(r.randint(0, 4)):
            k = r.randint(0, 9)
            if k == 0: out += ['Some text ' + words(r), 'more text.', '']
            elif k == 1: out += ['- [ ] one', '- [X] two', '  - nested ' + words(r), '    1. deep', '    2. deeper', '  - back', '- term :: def', '']
            elif k == 2: out += ['#+NAME: tbl', '| a | b |', '|---+---|', '| 1 | 2 |', '#+TBLFM: $2=$1*2', '']
            elif k == 3: out += ['#+BEGIN_SRC ' + r.choice(['python', 'C', 'sql', 'sh', 'emacs-lisp']) + ' :results output', 'print("* not a heading")', '  x = [1, 2]', '#+END_SRC', '', '#+RESULTS:', ': 3', '']
            elif k == 4: out += ['#+begin_quote', 'q ' + words(r), '#+end_quote']
            elif k == 5: out += ['[fn:1] A note.', '', 'Text again[fn:1].']
            elif k == 6: out += ['#+CAPTION: c', '#+ATTR_HTML: :width 100', '[[file:img.png]]', '']
            elif k == 7: out += ['-----', '#+CALL: f(x=1)', '\\begin{equation}', 'x', '\\end{equation}']
            elif k == 8: out += ['#+BEGIN: clocktable :scope subtree', '| Headline | Time |', '#+END:', '']
            else: out += ['', '']
    return out

def damage(r, ls):
    for _ in range(r.randint(0, 12)):
        if not ls: ls = ['']
        i = r.randrange(len(ls))
        k = r.randint(0, 9)
        if k == 0: del ls[i]
        elif k == 1: ls.insert(i, ls[r.randrange(len(ls))])
        elif k == 2: ls[i] = ind(r) + ls[i].lstrip(' \t')
        elif k == 3: ls.insert(i, line(r))
        elif k == 4 and ls[i]: j = r.randrange(len(ls[i])); ls[i] = ls[i][:j] + ls[i][j + r.randint(1, 4):]
        elif k == 5: ls.insert(i, '')
        elif k == 6: ls[i] = ls[i].swapcase()
        elif k == 7 and i + 1 < len(ls): ls[i:i + 2] = [ls[i] + ls[i + 1]]
        elif k == 8 and ls[i]: j = r.randrange(len(ls[i])); ls[i:i + 1] = [ls[i][:j], ls[i][j:]]
        else: ls[i] = ls[i] + r.choice([' ', '\t', '\r', ' x', '\u00a0'])
    return ls

def stress(r):
    k = r.randint(0, 5)
    if k == 0:   # a deep list
        return '\n'.join(' ' * (j % r.randint(2, 40)) + r.choice(['- ', '+ ', '1. ']) + 'x' for j in range(r.randint(20, 300))) + '\n'
    if k == 1:   # one long line of brackets and colons after a keyword or bullet
        return r.choice(['#+', '#+CAPTION', '- ', 'CLOCK: ', '* ', '#+CALL: f']) + ''.join(r.choice('[]:[ ]:') for _ in range(r.randint(50, 600))) + '\n' + line(r) + '\n'
    if k == 2:   # a long property drawer
        return '* h\n:PROPERTIES:\n' + ''.join(':K%d: v\n' % j for j in range(r.randint(50, 800))) + r.choice([':END:\n', ':END\n', '']) + 'x\n'
    if k == 3:   # many unclosed blocks and drawers
        return '\n'.join(r.choice(['#+begin_quote', ':D:', '\\begin{x}', '#+BEGIN: x', '- a', '#+begin_center']) for _ in range(r.randint(20, 200))) + '\n' + r.choice(['#+end_quote\n', ':END:\n', ''])
    if k == 4:   # nested greater blocks with lists
        d = r.randint(2, 30)
        return ''.join(' ' * j + '- a\n' + ' ' * (j + 2) + '#+begin_quote\n' for j in range(d)) + ''.join(' ' * (j + 2) + '#+end_quote\n' for j in reversed(range(d)))
    return '\n'.join('*' * r.randint(1, 30) + ' h' for _ in range(r.randint(20, 300))) + '\n'

seed, count, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
os.makedirs(out, exist_ok=True)
for i in range(count):
    r = random.Random(seed * 7919 + i)
    k = r.random()
    s = soup(r) if k < .6 else '\n'.join(damage(r, outline(r))) + '\n' if k < .92 else stress(r)
    with open(os.path.join(out, 'q%d-%d.org' % (seed, i)), 'w', encoding='utf-8', newline='') as f:
        f.write(s)
