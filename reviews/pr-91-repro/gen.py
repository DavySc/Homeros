# gen.py DIR: the files the review's probes read
import sys, os
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def w(n, s): open(os.path.join(d, n), 'w').write(s)
# finding 1: one Lua tree in two strings 3,000 lines apart
w('split.nix', "{\n  a = /* lua */ ''\n    x = [[\n  '';\n" + ''.join('  v%d = %d;\n' % (i, i) for i in range(3000))
  + "  b = /* lua */ ''\n    local y = 1 ]]\n    local z = 2\n  '';\n}\n")
# finding 2: one node with 10,000 children, in four languages
w('wide.c', "struct s { int a; const char *b; } t[] = {\n" + ''.join('\t{ %d, "x%d" },\n' % (i, i) for i in range(10000)) + "};\n")
w('wide.rs', "static T: &[(i32, &str)] = &[\n" + ''.join('    (%d, "x%d"),\n' % (i, i) for i in range(10000)) + "];\n")
w('wide.nix', "{\n" + ''.join('  a%d = f { x = %d; };\n' % (i, i) for i in range(10000)) + "}\n")
w('wide.lua', "local t = {\n" + ''.join('  a%d = f { x = %d },\n' % (i, i) for i in range(10000)) + "}\n")
# one Lua string with an interpolation on every line (n lines), and on every 30th of 600
for n in (250, 500, 1000, 2000, 12000):
    w('lualong%d.nix' % n, "{\n  cfg = /* lua */ ''\n" + ''.join('    local v%d = require("${p%d}").setup { a = %d }\n' % (i, i, i) for i in range(n)) + "  '';\n}\n")
w('luareal.nix', "{\n  cfg = /* lua */ ''\n" + ''.join(('    local v%d = require("${p%d}").setup { a = %d }\n' if i % 30 == 0 else '    local v%d = require("p%d").setup { a = %d }\n') % (i, i, i) for i in range(600)) + "  '';\n}\n")
w('luareal.lua', ''.join('local v%d = require("p%d").setup { a = %d }\n' % (i, i, i) for i in range(600)))
