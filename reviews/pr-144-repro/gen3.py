# third pass: 1,500 lines of 19,900 bytes, each 1 to 4 random tokens repeated in order or at random, bare, in a doc
# comment, in a comment's list, quote or fence, in a string, an interpolated string, a def head: gen3.py DIR [SEED]
import sys, os, random
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
toks = ['/-', '-/', '/--', '/-!', '--', '`', '``', '```', '~~~', '[', ']', '(', ')', '{', '}', '*', '**', '_', '__', '!', '![', '<', '>', ' ', '  ', '\t', 'a', ':', '"', "'", '\\',
        '#', '.', '=', '-', '1', 'e', '+', 'http://a', '<http://a', '[a]:', '](', '"t"', '@[', 'attribute ', 'attribute [', 'trace[', 'def ', 'def {', 'theorem ', 'instance ',
        'deriving ', 's!"', 'm!"', '«', '»', '⦃', 'é', '𝔽', ' ', 'with', 'where', '0x', '1.', '1e', '#eval', 'x.', '> ', '- ', '1. ', '# ', '===', '---', '\\*', '|', ',']
ctx = [('', ''), ('/--\n', '\n-/\n'), ('/-- ', '\n-/\n'), ('/--\n- item\n  ', '\n-/\n'), ('/--\n> q\n> ', '\n-/\n'), ('/--\n```\n', '\n```\n-/\n'), ('/--\n- a\n\n    ', '\n-/\n'),
       ('x := "', '"\n'), ('x := s!"{', '}"\n'), ('def ', '\n  := 1\n'), ('-- ', '\n'), ('/- /- ', '\n-/ -/\n'), ('«', '»\n')]
N = 19900
for i in range(1500):
    sub = random.sample(toks, random.randint(1, 4))
    s = ''
    if random.random() < .5:
        u = ''.join(sub)
        s = u * (N // len(u.encode()) )
    else:
        while len(s.encode()) < N: s += random.choice(sub)
    b = s.encode()[:N].decode('utf8', 'ignore')
    pre, post = random.choice(ctx)
    open(os.path.join(d, 'r%04d.lean' % i), 'w').write('def a := 1\n' + pre + b + post + 'def x := 1\n')
