# The acceptance's symbol list: every non-ASCII character in Mathlib's
# notation, meaning its files named *otation*.lean and its notation, infix,
# prefix, postfix, syntax and macro declarations anywhere.  Prints one code
# point per line.  Run on a mathlib4 checkout: python3 mathlib_symbols.py DIR
import os, re, sys
decl = re.compile(r'^\s*(?:@\[[^\]]*\]\s*)?(?:(?:scoped|local)\s+)?'
                  r'(?:notation\d?|infix[lr]?|prefix|postfix|syntax|macro|notation3)\b')
found = set()
for d, _, fs in os.walk(os.path.join(sys.argv[1], 'Mathlib')):
    for f in fs:
        if not f.endswith('.lean'): continue
        t = open(os.path.join(d, f), encoding='utf-8').read()
        if 'otation' in f: found |= {c for c in t if ord(c) > 127}
        for line in t.splitlines():
            if decl.match(line): found |= {c for c in line if ord(c) > 127}
for c in sorted(found): print('%04X' % ord(c))
