# the long lines and long regions of reviews/pr-144.md: gen.py DIR
import sys, os
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
w = lambda n, s: open(os.path.join(d, n), 'w').write(s)
prose = "The lemma `foo_bar` states that for all *x* in **S**, we have [a link](http://x.y/z) and more text here.\n"
w('doc300.lean', "theorem a : True := trivial\n/--\n" + prose * 300 + "-/\n" + "theorem t : True := trivial\n" * 10)
w('doc2000.lean', "/-!\n" + prose * 2000)
w('c_def.lean', "def " + "a b " * 5000 + "\n" + "x y z\n" * 5000)
w('c_plain.lean', "x" * 20000 + "\ndef x := 1\n")
for name, unit in [('stars', '*a '), ('brackets', '[a'), ('bangs', '![a]('), ('tickrun', '`'), ('parens', '[a](((('), ('ticks2', '`a')]:
    for n in (100, 200, 400, 800, 1600, 20000):
        w('s_%s_%d.lean' % (name, n), "/-- " + unit * (n // len(unit)) + "\n-/\ndef x := 1\n")
