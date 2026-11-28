"""PR 175 review: token soup for sql_vars under ASan/UBSan (the vars probe, -m).  soup.py N SEED | vars -m"""
import random, sys
rnd = random.Random(int(sys.argv[2]))
T = ['declare', 'exec', 'execute', 'select', 'create', 'alter', 'or', 'procedure', 'proc', 'function', 'trigger', 'table', 'case', 'when', 'else', 'end',
     'begin', 'set', 'as', 'int', 'go', '@a', '@b', '@@x', '@', '@@', '@' + 'n' * 127, '@' + 'n' * 128, '@' + 'n' * 129, '@' + 'x' * 400, '(', ')', ',', ';', '=', '+',
     "'", "''", "'@a'", '--', '/*', '*/', '[', ']', '"', '\n', '\t', ' ', '1', 'N', '.', '\x00', '\xc3\xa9', '\xff', '#t', '@a=', '=@a', 'cursor', 'for', 'output']
out = sys.stdout.buffer
for i in range(int(sys.argv[1])):
    s = ''.join(rnd.choice(T) + rnd.choice(['', ' ', ' ', '\n']) for _ in range(rnd.randint(0, 60))).encode('latin-1')
    out.write(b'#batch %d\n%s\n' % (len(s), s))
