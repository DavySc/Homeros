#!/usr/bin/env python3
"""Differential test of src/org (package 16) against Org's own parser.

    org_conform.py [-f|-t|-s] DRIVER N SEED     N random org texts, fed to both
    org_conform.py [-f|-t|-s] DRIVER FILE...    given files
    org_conform.py -g                          writes src/org/entities.h and scripts.h

DRIVER is tests/org_drv.c built; Org runs as emacs -Q --batch with
tools/org_dump.el, or with -f, the colours: tools/org_faces.el, its faces
named as src/org shows them (FACES), or with -t, org-todo as cit and ciT
run it: tools/org_todo.el, on FILE.cmd's position and directions, or with
-s, what each line shows (orgfx_conceal): tools/org_show.el's runs, drawn
on a cell grid (shown).  Prints the first difference of each
failing text (the text is kept in build/org-fail-*.org) and "N differ"."""
import os, random, re, shutil, subprocess, sys, tempfile, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
# links (org-activate-links): bracket, angle and plain ones, over lines, with backslashes
LINKS = ['[[a]]', '[[https://orgmode.org][Org *site*]]', '[[id:x-1][a note]]', '[[a][b\nc]]', '[[a\nb]]', '[[x\\]y]]',
         '[[x\\\\]]', '[[a][]]]', '[[a][b]', '[[]]', '[[a]', '[[file:a.org::*h][h]]', 'https://example.com/a_(b)', 'http://x.',
         'mailto:me@x.org', 'xhttps://a', 'file:a.org', 'doi:10.1/x', 'HTTP://x', '<https://x y>', '<mailto:a\n b>', '<id:',
         '[[a\n\nb]]', '[[a][x^{2}]]', '[[a][\\alpha]]', '=[[a]]=', '[[a][=b=]]', 'https://x.org/(a(b)c)', 'http://é', 'shell:ls|x',
         '[[a\\\\\\]b]]', '[[a\\\\\\\\]]', 'id:', '[[a][b\n- c]]', '<http://a\n\nb>', 'https://a.b/c-', 'http://a(b', 'https://a.b/c/']
# footnotes (org-activate-footnote-links): references, inline ones over lines, in links, LaTeX macros and fragments
FNS = ['[fn:1]', '[fn:a-b]', '[fn::inline (x)]', '[fn:n:inline [a] text]', '[fn:x: (open', '[fn:1:\n x]', '[fn:é]', '[FN:1]', '[fn:]',
       '[[fn:1]]', '[[a][[fn:1]]]', '\\cite{[fn:1]}', '\\cite[x]{[fn:2]}', '"[fn:1]"', '[fn:1:"a]"]', '[fn:1: ⟨x]⟩', '=a [fn:1: b=',
       '[fn:2:\n\n]', '[fn:x:<a]>', '[fn:1:{]', '"', '[fn:1:"]', '\\(x [fn:1]\\)', '\\[[fn:1]\\]', '$[fn:1]$', '$$[fn:1]$$',
       '$[fn:1] $', '$ [fn:1]$', '$[fn:1]$x', '\\alpha [fn:1]', '\\alpha{}[fn:1]']
WORDS = ['a', 'b', 'x', 'TODO', 'DONE', 'NEXT', 'WAIT', 'CANCELLED', 'todo', 'COMMENT', '[#A]', '[#a]',
         '[#1]', '[#12]', '[#65]', ':a:', ':b:c:', '::', ':@x_1:', '[fn:1]', '*', '-', '+', '1.', 'a)',
         '|', '#', '#+', ':', '[', ']', '<2026-11-22 Sun>', '[2026-11-22 Sun 09:00]', 'é', 'λ', '→', '\u00a0', '\u3000', '\f', '\r',
         '[150%]', '[050%]', '@@a:b@@c:d@@', 'COMMENTx'] + LINKS + FNS
# above ASCII, Emacs's [[:word:]] and [[:alnum:]] differ: é both, ² and · word only, º alnum only, → € neither
U = ['é', '²', '·', 'º', '→', '€']


def uni(r):
    return ''.join(r.choice(U + ['a', '_', '-', '@']) for _ in range(r.randint(1, 3)))


def word(r):
    return r.choice(WORDS) if r.random() < .5 else ''.join(r.choice('ab xé:*-#+|[]') for _ in range(r.randint(1, 4)))


def text(r, k=3):
    return ' '.join(word(r) for _ in range(r.randint(0, k)))


def indent(r):
    return r.choice(['', '', '', ' ', '  ', '   ', '\t', ' \t', '    ', '      ', ' ' * r.randint(0, 12)])


def ts(r, c='['):
    if r.random() < .1:
        return '<%%(' + r.choice(['diary-float t 4 2', 'x', 'a)b', '']) + ')' + r.choice(['', ' 9:00', ' 9:00-10:30', ' x 10:00-11:00', ']', ' 39:30-10:00']) + '>'
    d = '%04d-%02d-%02d' % (r.choice([2026, 2026, 999, 12345]) % 10000, r.randint(0, 13), r.randint(0, 32))
    e = {'[': ']', '<': '>'}[c]
    if r.random() < .1:
        e = r.choice('[]<>')
    tail = ''.join(r.choice(['', ' Sun', ' Mon.', ' Sun', '  Sun', ' 9:00', ' 09:00', ' 19:30', ' 9:00-10:30', ' 123:45', '-11:00',
                             ' +1d', ' ++1w', ' .+1d', ' .+1d/3d', ' +2D', ' -2d', ' --3w', ' -1M', ' +1y/x', ' x]y', ' x', '+5h'])
                   for _ in range(r.randint(0, 3)))
    if tail and tail[0] != ' ' and r.random() < .7:
        tail = ' ' + tail
    t = c + d + tail + e
    if r.random() < .15:
        t += '--' + ts(r, r.choice('<['))
    return t


BLOCKS = ['src', 'SRC', 'example', 'quote', 'center', 'verse', 'export', 'comment', 'foo', 'src:']   # no regexp characters: see src/org/org.c's first comment


def line(r):
    return line_k(r, r.randint(0, 31))


def line_k(r, k):
    i = indent(r)
    if k == 0:
        return '*' * r.choice([1, 1, 2, 3]) + r.choice([' ', ' ', ' ', '', '\t']) + text(r, 5) + r.choice(['', ' :' + uni(r) + ':'])
    if k == 1:
        return i + ' '.join(r.choice(['SCHEDULED:', 'DEADLINE:', 'CLOSED:', 'scheduled:', 'xCLOSED:']) + r.choice([' ', '', '\t', '  ']) + ts(r, r.choice('<['))
                            for _ in range(r.randint(1, 3))) + r.choice(['', ' x', ' DEADLINE:'])
    if k == 2:
        return i + r.choice([':PROPERTIES:', ':properties:', ':END:', ':end:', ':LOGBOOK:', ':a-b_c:', ':é:', ':x:y:', ':' + uni(r) + ':'])
    if k == 3:
        return i + r.choice([':EFFORT: 0:12', ':STYLE: habit', ':ID:', ':a:b', ':x+: 1', ': x', ':', ':A:\tb', ':k: |<5>| x'])
    if k == 4:
        return i + r.choice(['CLOCK: ', 'clock: ', 'CLOCK:\t']) + r.choice([
            ts(r) + '--' + ts(r) + ' =>  1:30', ts(r), ts(r) + ' x', '=> 0:10', ts(r) + '--' + ts(r), ts(r) + '--' + ts(r) + ' => 1:3',
            ts(r, r.choice('<[')) + r.choice([' => 1:30 ', ' =>  x y', ' => ', '=> 2:00', ' => 1:30\f'])])
    if k == 5:
        return i + r.choice(['# ', '#', '#x ']) + text(r)
    if k == 6:
        return i + '#+' + r.choice(['NAME', 'name', 'CAPTION', 'CAPTION[x y]', 'RESULTS', 'RESULTS[ab]', 'ATTR_HTML',
                                    'attr_latex', 'HEADER', 'TBLNAME', 'TITLE', 'a:b', 'x[y]', 'CALL', 'TBLFM', 'STARTUP', 'caption[a]: b]']) + r.choice([':', ': ', '', ' :']) + text(r)
    if k in (7, 8):
        b = r.choice(BLOCKS)
        return i + r.choice(['#+begin_', '#+BEGIN_', '#+end_', '#+END_']) + b + r.choice(['', ' ', ' python -n 3 :var x=1'])
    if k == 9:
        return i + r.choice(['#+BEGIN: clocktable :scope file', '#+begin: x', '#+BEGIN:', '#+END:', '#+end', '#+END'])
    if k in (10, 11, 12):
        return i + r.choice(['-', '+', '*', '1.', '2)', '10.', 'a.', '-', '-']) + r.choice([' ', ' ', '  ', '\t', '']) + r.choice(['', '', '[ ] ', '[X] ', '[x] ', '[-] ', '[@3] ', '[@start:2] ']) + text(r) + r.choice(['', '', ' :: ' + text(r), ' ::', ':: x'])
    if k == 13:
        return i + r.choice(['|', '| a | b |', '|-', '|---+---|', '| x', '+--+--+', '+--+', '| a |\t', '| <c> |'])
    if k == 14:
        return i + r.choice(['#+TBLFM: $2=$1', '#+tblfm: x', '#+TBLFM:'])
    if k == 15:
        return i + r.choice([': x', ':', ':  y', ':x'])
    if k == 16:
        return i + r.choice(['-----', '------  ', '----', '- - - - -'])
    if k == 17:
        return r.choice(['%%(diary-anniversary 1 1 2000)', '%%x', ' %%(x)'])
    if k == 18:
        return r.choice(['[fn:1] ', '[fn:a-b] ', '[FN:x]', '[fn:] ', '[fn:1]', '[fn:' + uni(r) + '] ']) + text(r)
    if k == 19:
        return i + r.choice(['\\begin{equation}', '\\end{equation}', '\\begin{align*} x', '\\end{align*}', '\\begin{x}\\end{x}', '\\BEGIN{x}'])
    if k in (20, 21, 22, 23):
        return ''
    if k == 24:
        return i
    return i + text(r, 6)


# lines for the colours: Org's font-lock reads these by regexp, anywhere
FACE_LINES = ['| a | :=$1+1 |', '| # | x |', '| $ | a=1 |', '|<l5>| x |', '| <r> |', '|=x|', '| x | =y | z', '| * |', '| ! | x | y |',
              '#+TITLE: x', '#+author: me', '#+date:', '#+subtitle:  s ', '#+email x', '#+CAPTION[a]: b', '#+caption: x', '#+captionx',
              '#+caption[a]x', '#+title_x: y', '# x', '#', '#+', '#x', '#+begin:', '#+begin_', '#+BEGIN_SRC c', '#+end_src_x',
              '#+END_SRCx', '#+end_src:y', '*\tx', '*', '- [3/3] x', 'a [100%] b', '[2/3]', '[/] [%]', '[03/3]', '- term :: desc',
              '-   :: x', '  * a :: b', '+ x::y :: z', '- [@3] [X] x', '- [@start:2] [ ] y', 'a. [-] z', '10) [X]', '* x :ARCHIVE:',
              '* COMMENT x', '* TODO COMMENT', '* [#A] COMMENT y', '** DONE x  ', '* TODO\tx', '*[#B] x', '* x [#C] y', '* NEXT',
              '* CANCELLED  done  :t:', 'see DEADLINE: x', 'aCLOCK: x', 'éSCHEDULED:', ':a: b', ':a:b:', ':x::', ': v', ':a:\fb',
              '<%%(x)> <%%(y z)>', '&%%(a)', 'x <2026-01-02 Fri>--<2026-01-03 Sat> y', '<2026-01-02>---<2026-01-03>',
              '<2026-01-02>----<2026-01-03>', '[2026-01-02 Fri 10:00]', '[<2026-01-02 Fri>]',
              '@@html:<b>@@ x @@latex:y@@', '@@a-b:@@@@', '@@:x@@', '@@X:y@@', '****** six', '******* seven :t:', '******** DONE eight',
              '********* nine', '*** TODO [#B] three :a:b:', '**** WAIT x [50%]']


def line_f(r):
    return indent(r) * (r.random() < .2) + r.choice(FACE_LINES) if r.random() < .3 else line(r)


def gen_faces(r):
    return '\n'.join(line_f(r) for _ in range(r.randint(1, 30))) + r.choice(['\n', ''])


# Org's faces as src/org names them (tests/org_drv.c -f); None: no colour
FACES = dict({'org-level-%d' % i: 'markup.heading.%d' % i for i in range(1, 7)}, **{
    'org-level-7': 'org.level.7', 'org-level-8': 'org.level.8', 'org-todo': 'org.todo',
    'org-checkbox-statistics-todo': 'org.todo', 'org-done': 'comment', 'org-headline-done': 'comment',
    'org-checkbox-statistics-done': 'comment', 'org-meta-line': 'comment', 'font-lock-comment-face': 'comment',
    'org-document-info-keyword': 'comment', 'org-archived': 'comment', 'org-priority': 'org.priority',
    'org-tag': 'org.tag', 'org-special-keyword': 'keyword', 'org-property-value': None, 'org-date': 'org.date',
    'org-sexp-date': 'org.date', 'org-drawer': 'org.drawer', 'org-table': 'org.table', 'org-table-row': 'org.table',
    'org-formula': 'org.formula', 'org-checkbox': 'org.checkbox', 'org-list-dt': 'org.checkbox',
    'org-code': 'markup.raw', 'org-document-title': 'markup.heading', 'org-document-info': 'org.info',
    'org-block': 'org.block', 'org-block-begin-line': 'org.block.line', 'org-block-end-line': 'org.block.line',
    'org-link': 'markup.link.url', 'org-footnote': 'markup.link.label'})


def runs(lines, names):
    """'begin end face' lines as runs of one face, faces renamed by names"""
    out = []
    for l in lines:
        a, b, f = l.split(' ')
        f = names.get(f, '?' + f) if names else f
        if f is None:
            continue
        if out and out[-1][1] == a and out[-1][2] == f:
            out[-1][1] = b
        else:
            out.append([a, b, f])
    return [' '.join(x) for x in out]


def planning(r):
    return '* ' + text(r, 2) + '\n' + line_k(r, 1) + '\n' + '\n'.join(line_k(r, 4) for _ in range(r.randint(0, 3)))


def gen(r):
    if r.random() < .3:   # headlines with planning and clock lines, where Org reads their timestamps
        return '\n'.join(planning(r) for _ in range(r.randint(1, 6))) + r.choice(['\n', ''])
    return '\n'.join(line(r) for _ in range(r.randint(1, 30))) + r.choice(['\n', '\n', '', '\n\n', '\n  '])


def ts_todo(r, open_='<'):
    day = r.randint(12, 28)
    hm = r.random() < .4
    t = '%s2026-11-%02d' % (open_, day) + r.choice([' ' + 'ThuFriSatSunMonTueWed'[3 * ((day - 19) % 7):][:3], ' Thu', '', '  Mon'])
    if hm:
        t += r.choice([' 10:00', ' 9:30', ' 17:30', ' 18:00-19:15', ' 23:50'])
    rep = r.choice(['', '+1d', '++1w', '.+2d', '+1m', '+1y', '+0d', '.+1d/3d', '+2w', '++2d', '.+1m', '+3h'] +
                   (['+2h', '.+1h'] + (['++3h'] if day == 19 else []) if hm else []))
    if day < 13 and rep.startswith('++'):
        rep = '+1d'
    t += (' ' + rep if rep else '') + r.choice(['', '', ' -2d', ' --1d'])
    return t + {'<': '>', '[': ']'}[open_]


def gen_todo(r):
    """entries for org-todo: keywords, planning, drawers, children, repeaters.
    No source blocks: when a new LOGBOOK lands inside one (after a stray
    :LOGBOOK: line there), org-indent-region re-indents the block's code in
    its language's mode and escapes it, which src/org does not copy (seed 5,
    1 of 10,000 texts)"""
    out = []
    for _ in range(r.randint(1, 5)):
        h = '*' * r.choice([1, 1, 2, 2, 3]) + r.choice([' ', ' ', '  '])
        k = r.choice(['', '', 'TODO ', 'NEXT ', 'WAIT ', 'DONE ', 'CANCELLED ', 'todo ', 'TODO  ', 'DONE\t', 'TODOx '])
        # "[#A] COMMENT" only where org-todo's regexps find the cookie too: after
        # anything else org-element calls the headline commented, they do not,
        # and Org drops text ("* TODOx [#A] COMMENT x", left: "* [#A] x")
        h += k + r.choice(['a', 'task b', '', 'x :tag:', '[#A] y', 'COMMENT z', '<2026-11-20 Fri +1w> c'] +
                          (['[#A] COMMENT x'] if k.strip() in ('', 'TODO', 'NEXT', 'WAIT', 'DONE', 'CANCELLED') and '\t' not in k else []))
        out.append(h.replace('\\t', '\t').rstrip(' ') if r.random() < .8 else h)
        if r.random() < .5:
            k = r.sample(['SCHEDULED: ' + ts_todo(r), 'DEADLINE: ' + ts_todo(r), 'CLOSED: ' + ts_todo(r, '[')], r.randint(1, 3))
            out.append(r.choice(['', ' ', '  ']) + ' '.join(k) + r.choice(['', ' ']))
        if r.random() < .4:
            props = r.sample([':ORDERED: t', ':ORDERED: nil', ':LAST_REPEAT: [2026-11-01 Sun 10:00]', ':CUSTOM_ID: x',
                              ':ordered: t', ':Effort: 1:00', ':ORDEREDX: t', ':LAST_REPEAT:x: 1'], r.randint(0, 3))
            i = r.choice(['', '', '         ', ' \t'])
            out += [i + l for l in [':PROPERTIES:'] + props + [':END:']]
        if r.random() < .3:
            out += [r.choice([':LOGBOOK:', ':logbook:']), '- State "DONE"       from "TODO"       [2026-11-10 Tue 09:00]', ':END:']
        for _ in range(r.randint(0, 3)):
            out.append(r.choice(['text', '', '- item', 'see ' + ts_todo(r), ts_todo(r), '  ' + ts_todo(r, '['),
                                 ':NOTES:', ':END:', '#+begin_quote', '#+end_quote', 'SCHEDULED: ' + ts_todo(r),
                                 '| ' + ts_todo(r) + ' |', 'scheduled: ' + ts_todo(r), '  :LOGBOOK:', 'xCLOSED: ' + ts_todo(r, '['),
                                 'SCHEDULED: <2026-11-19 Thu 18:00-19:15 .+1h>', 'SCHEDULED: <2026-11-12 Thu +1d',
                                 'DEADLINE: <2026-11-12 Thu .+1w x', '  :LAST: <2026-11-12 Thu ++1d']))
    return '\n'.join(out) + r.choice(['\n', '\n', ''])


def script_table(tag):
    """Unicode's <super> or <sub> form of each character, as src/org/scripts.h:
    SUPERSCRIPT/SUBSCRIPT names first, then MODIFIER LETTER, then the lowest"""
    best = {}
    for c in range(0x80, 0x30000):
        d = unicodedata.decomposition(chr(c)).split()
        if len(d) == 2 and d[0] == tag:
            name = unicodedata.name(chr(c), '')
            rank = (0 if name.startswith(('SUPERSCRIPT', 'SUBSCRIPT')) else 1 if 'MODIFIER LETTER' in name or 'SUBSCRIPT' in name else 2, c)
            b = chr(int(d[1], 16))
            if b not in best or rank < best[b][0]:
                best[b] = (rank, chr(c))
    return {b: v[1] for b, v in best.items()}


SCRIPTS = {}


def one_cell(c):
    return len(c) == 1 and unicodedata.category(c) != 'Cf' and unicodedata.east_asian_width(c) not in 'WF'


def shown(t, runs):
    """the lines of t as a cell grid shows Org's runs: a composition as its
    one-cell character, invisible text hidden, a raised or lowered script as
    its characters' Unicode forms, as typed when one has none"""
    if not SCRIPTS:
        SCRIPTS.update(u=script_table('<super>'), d=script_table('<sub>'))
    hid, comp = bytearray(len(t)), {}
    runs = [l for l in runs if l]
    for l in runs:
        k, a, z, *c = l.split(' ', 3)
        a, z = int(a), int(z)
        if k == 'h':
            hid[a:z] = b'\1' * (z - a)
        elif k == 'c' and one_cell(c[0]):
            comp[a] = (z, c[0])
    for l in runs:
        k, a, z = l.split(' ', 3)[:3]
        a, z = int(a), int(z)
        if k not in 'ud':
            continue
        q, new = a, {}
        while q < z:
            if hid[q]:
                q += 1
                continue
            k2, ch = comp.get(q) or (q + len(t[q:q + 4].decode('utf-8', 'replace')[0].encode()), t[q:q + 4].decode('utf-8', 'replace')[0])
            if ch not in SCRIPTS[k]:
                break
            new[q] = (k2, SCRIPTS[k][ch])
            q = k2
        if q < z:
            for x in (a - 1, a, z - 1):   # the ^ or _ and the braces
                hid[x] = 0
        else:
            comp.update(new)
    out, p = bytearray(), 0
    while p < len(t):
        if hid[p] and t[p] != 10:   # a hidden newline stays: a cell grid keeps its lines (known difference)
            p += 1
        elif p in comp:
            out += comp[p][1].encode()
            p = comp[p][0]
        else:
            out.append(t[p])
            p += 1
    return out.decode('utf-8', 'replace').split('\n')


def brace_re():
    """org-create-multibrace-regexp "{" "}" 3 inside org-match-substring-with-braces-regexp"""
    nothing = r'[^{}]*?'
    r, nxt = nothing, r'(?:' + nothing + '{' + nothing + '})+' + nothing
    for _ in range(2):
        r, nxt = r + '|' + nxt, r'(?:' + nothing + '{' + nxt + '})+' + nothing
    return re.compile(r'(\S)([_^])(\{(?:' + r + r')\})')


SCRIPT = brace_re()


def long_script(t, runs):
    """org-raise-scripts takes a script over two newlines or more; src/org
    stops its braces at the second (known difference).  Or Org shows part
    of a script over one newline raised, where a block's lines took the
    first part's off: src/org shows a script over lines as typed"""
    p = 0
    while m := SCRIPT.search(t, p):
        n = m.group(3).count('\n')
        if n > 1:
            return True
        if n:
            nl, z = len(t[:m.group(3).index('\n') + m.start(3)].encode()), len(t[:m.end()].encode())
            if any(l[0] in 'ud' and nl < int(l.split()[1]) < z for l in runs if l):
                return True
        p = m.end() - (t[m.end():m.end() + 1] in ('_', '^'))
    return False


MATH = [r'\leq', r'\geq', r'\wedge', r'\vee', r'\top', r'\bot', r'\sqsubseteq', r'\sqcup', r'\bigvee', r'\alpha', r'\Alpha',
        r'\there4', r'\sup2', r'\frac12', r'\frac34x', r'\zwj', r'\nbsp', r'\foo', r'\leqx', r'\le', r'\_ ', '\\', r'\iff', r'\mid',
        'x^{2}', 'a_{i}', 'a_{b}^{c}', 'x^{n+1}', r'x^{\alpha}', r'x^{\alpha{}}', 'x^{{a}{b}}', 'x^{a{b}}', 'x^{}', 'x^{*b*}', 'x^2',
        'x_{i', 'x^{{{a}}}', 'x^{a}_{b}', 'é^{é}', ' ^{2}', 'x^{ab c}', 'x^{(1)}', '{}', '*b*', '/i/', '_u_', '+s+', '=v=', '~c~', '*a =b* c=', '/x ~y/ z~',
        '*', '/', '_', '+', '=', '~', '^', '{', '}', '(', '-', '|', 'é', '²', '\t', ':', '[x]', '[', '<2026-01-01 x^{2}>'] + LINKS + FNS


def gen_show(r):
    out = []
    for _ in range(r.randint(1, 6)):
        body = ''.join(r.choice(MATH) + r.choice(['', '', ' ', ' ', '{}']) for _ in range(r.randint(0, 8)))
        out.append(r.choice(['', '', '', '* ', '** TODO ', '# ', '#+TITLE: ', '#+begin_src c\n', '| ', ':PROP: ', '- ', '  ',
                             'CLOCK: ', '-----\n', '|---+---|\n', '#+begin_quote\n', ':x^{2}: ', ':a\\alpha{}x_{i}: ', ':*b*^{c}: ', '#+caption: ', '#+name: ', '*', '/', '**', '/ ', '= ', '%%(x) ', '++\n', '++']) + body + r.choice(['', '', ' |', ' :a:']))
    return '\n'.join(out) + r.choice(['\n', ''])


def run(driver, files, w):
    el = subprocess.run(['emacs', '-Q', '--batch', '-l', os.path.join(HERE, {'t': 'org_todo.el', 's': 'org_show.el', 0: 'org_dump.el'}.get(w, 'org_faces.el'))] + files,
                        capture_output=True, env=dict(os.environ, TZ='UTC'))
    if el.returncode:
        sys.exit('emacs failed: ' + el.stderr[-2000:].decode('utf-8', 'replace'))
    c = subprocess.run([driver] + (['-' + w] if w in ('t', 's') else ['-f', str(w)] if w else []) + files, capture_output=True)
    if c.returncode:
        sys.exit('driver failed: ' + c.stderr[-2000:].decode('utf-8', 'replace'))

    names = {os.path.basename(f) for f in files}

    def split(out):
        d, cur = {}, None
        for l in out.split('\n'):
            if not l and cur is None:
                continue
            if l.startswith('== ') and l[3:] in names:   # a text's own line may start "== "
                cur = l[3:]
                d[cur] = []
            elif l or w in ('t', 's'):   # the texts org-todo leaves, blank lines and all
                d[cur].append(l)
        return d
    # bytes: a title may hold \r or \f, which text mode would take for line ends
    return split(el.stdout.decode('utf-8', 'replace')), split(c.stdout.decode('utf-8', 'replace'))


def cells(rows, fmt):
    """the C initialisers of rows, as many to a line as fit in 100 columns"""
    out, line = [], '\t'
    for r in rows:
        item = fmt(r) + ', '
        if len(line.expandtabs(8)) + len(item.rstrip()) > 100:
            out.append(line.rstrip())
            line = '\t'
        line += item
    return out + [line.rstrip()]


def tables():
    """src/org's two generated tables (-g)"""
    el = ('(progn (load (expand-file-name "tools/org_show.el")) (let (l) (dolist (e (append org-entities-user org-entities))'
          ' (when (and (consp e) (= (length (nth 6 e)) 1) (not (assoc (car e) l))) (push (cons (car e) (nth 6 e)) l)))'
          ' (dolist (x l) (princ (format "%s %s\n" (car x) (cdr x))))))')
    got = subprocess.run(['emacs', '-Q', '--batch', '--eval', el], capture_output=True, check=True).stdout.decode('utf-8')
    ents = sorted((l.split(' ', 1) for l in got.split('\n') if l), key=lambda r: r[0].encode())
    ents = [r for r in ents if re.fullmatch(r'[a-zA-Z]+|there4|sup[123]|frac[13][24]', r[0]) and len(r[1]) == 1]
    esc = lambda u: u.replace('\\', '\\\\').replace('"', '\\"')
    with open('src/org/entities.h', 'w') as g:
        g.write('\n'.join(['/* What org-pretty-entities shows for \\NAME: the entries of (append',
                            '   org-entities-user org-entities) (Org 9.8.7, with Davy\'s lattice symbols',
                            '   from tools/org_show.el) whose UTF-8 form is one character and whose name',
                            '   org-fontify-entities can match, the first of each name, sorted in byte',
                            '   order.  Generated by tools/org_conform.py -g. */',
                            'static const char *const entities[][2] = {']
                           + cells(ents, lambda r: '{ "%s", "%s" }' % (r[0], esc(r[1]))) + ['};', '']))
    with open('src/org/scripts.h', 'w') as g:
        g.write('\n'.join(['/* Unicode\'s superscript and subscript forms (decomposition <super> or',
                            '   <sub> of one character; SUPERSCRIPT/SUBSCRIPT names first, then',
                            '   MODIFIER LETTER, then the lowest code point), sorted by the character',
                            '   they stand for: how a cell grid shows what Org raises.  Generated by',
                            '   tools/org_conform.py -g, from the table its -s draws Org with. */',
                            'typedef struct { unsigned c; const char *s; } Script;']))
        for name, tag in (('sup', '<super>'), ('sub', '<sub>')):
            t = sorted(script_table(tag).items())
            g.write('\n'.join(['', 'static const Script %s[] = {' % name] + cells(t, lambda r: '{ 0x%X, "%s" }' % (ord(r[0]), r[1])) + ['};']))
        g.write('\n')


def main():
    if sys.argv[1] == '-g':
        return tables()
    faces = sys.argv[1] == '-f'
    todo = sys.argv[1] == '-t'
    show = sys.argv[1] == '-s'
    if faces or todo or show:
        del sys.argv[1]
    driver = sys.argv[1]
    os.makedirs('build', exist_ok=True)
    tmp = tempfile.mkdtemp()
    if len(sys.argv) == 4 and sys.argv[2].isdigit():
        n, seed = int(sys.argv[2]), int(sys.argv[3])
        r = random.Random(seed)
        files = []
        for i in range(n):
            p = os.path.join(tmp, 't%05d.org' % i)
            with open(p, 'w', encoding='utf-8', newline='') as g:
                t = (gen_faces if faces else gen_todo if todo else gen_show if show else gen)(r)
                g.write(t)
            if todo:   # org-todo at a random character, 1 to 4 times right or left
                with open(p + '.cmd', 'w') as g:
                    g.write('%d %s\n' % (len(t[:r.randint(0, len(t))].encode()), ''.join(r.choice('rrl') for _ in range(r.randint(1, 4)))))
            files.append(p)
        print('org_conform: %d texts, seed %d' % (n, seed))
    else:
        files = sys.argv[2:]
    bad = errors = 0
    for k in range(0, len(files), 400):
        part = files[k:k + 400]
        # the colours are read in windows of w bytes, as the editor draws
        el, c = run(driver, part, random.Random(k).choice([7, 64, 1024, 8192]) if faces else 't' if todo else 's' if show else 0)
        for p in part:
            name = os.path.basename(p)
            a, b = el.get(name), c.get(name)
            i = next((i for i, l in enumerate(a or []) if l.startswith('!error')), None)
            if i is not None and todo and any(re.search(r'[0-9]:(-|[0-9]{3})', l) for l in a[:i]):
                errors += 1   # Org's org-modify-ts-extra carries a range end's minutes once only, writes
                continue      # "18:-1395" for .+1h on a range with a delay, then fails to read it back
            if i is not None and todo:   # Org stopped: the text as it left it, then no more steps
                a, b = a[:i], (b or [])[:i]
            elif i is not None:
                errors += 1   # no answer from Org to compare with
                continue
            if faces or show:
                with open(p, 'rb') as t:
                    # Org stops dating the whole buffer at a date after "[";
                    # a live Emacs, which fontifies in chunks, does not
                    if re.search(rb'\[[<\[][0-9]{4}-[0-9]{2}-[0-9]{2}[] >]', t.read()):
                        errors += 1
                        continue
            if faces:
                a, b = runs(a or [], FACES), runs(b or [], None)
            if show:
                with open(p, 'rb') as t:
                    t = t.read()
                if long_script(t.decode('utf-8', 'replace'), a):
                    errors += 1
                    continue
                a = shown(t, a)
                while a and not a[-1]:
                    a.pop()
                while b and not b[-1]:
                    b.pop()
            if a == b:
                continue
            bad += 1
            if bad <= 5:
                keep = os.path.join('build', 'org-fail-' + name)
                for x in ['', '.cmd'] if todo else ['']:
                    with open(p + x, 'rb') as f, open(keep + x, 'wb') as g:
                        g.write(f.read())
                a, b = a or ['(no output)'], b or ['(no output)']
                j = next((j for j in range(max(len(a), len(b))) if j >= len(a) or j >= len(b) or a[j] != b[j]), 0)
                print('%s (kept as %s): line %d\n  org: %s\n  ours: %s' % (
                    name, keep, j + 1, a[j] if j < len(a) else '(end)', b[j] if j < len(b) else '(end)'))
    print('%d of %d differ (no answer from Org on %d)' % (bad, len(files), errors))
    shutil.rmtree(tmp)   # the failing texts are kept in build/
    sys.exit(1 if bad else 0)


main()
