#!/usr/bin/env python3
"""Writes the probe's test files next to this script: far.lua, far.c and
far.md (dense in the constructs whose colour a predicate on another node
decides, in multi-line constructs and in injections), big.md and big.c
(300-line captures: a fenced block, a comment) for the worst case of the
recompute."""
import os
R = os.path.dirname(os.path.abspath(__file__)) + '/'
lua = ''
for i in range(40):
    lua += f'''local a{i} = s:find("%d+")
local b{i} = string.match(x, "%a+")
local c{i} = ("123"):gsub(
  "%s",

  "")
x = string.gmatch(
  y,
  "[%w_]+")
ffi.cdef[[
  int f{i}(char *s);
  #undef A{i}
]]
exec_lua([[ local q = s:find("%d") ]])
'''
open(R + 'far.lua', 'w').write(lua)
c = ''
for i in range(40):
    c += f'''void f{i}(struct S *s)
{{
  __label__ out{i};
#undef FOO{i}
#define BAR{i}(x) s->field = x; __label__ z
  s->a.b = FOO; s->call(1);
  /* comment
     over lines */
  char *p = "str\\n%d";
out{i}:
  return;
}}
struct T{i} {{ int field; void (*fn)(int arg); }};
'''
open(R + 'far.c', 'w').write(c)
md = ''
for i in range(30):
    md += f'''# Head {i}

Some *emph* and **strong** with `code` and [link](http://x "t") and ![img](y).

```lua
local a = s:find("%d+")
ffi.cdef[[ int x; ]]
```

> quote with `code`
> ```c
> __label__ l;
> #undef X
> ```

- item [ref][r{i}]
  - nested <b>html</b>

| a | b |
|---|---|
| `x` | *y* |

[r{i}]: http://example.com

'''
open(R + 'far.md', 'w').write(md)
md = ''
for i in range(8):
    md += f'# Section {i}\n\nSome text before the block.\n\n```\n' + ''.join(f'line {j} of a long listing with some words in it\n' for j in range(300)) + '```\n\n'
open(R + 'big.md', 'w').write(md)
c = ''
for i in range(8):
    c += f'int f{i}(int a)\n{{\n  return a;\n}}\n/*\n' + ''.join(f' * line {j} of a long comment with some words in it\n' for j in range(300)) + ' */\n'
open(R + 'big.c', 'w').write(c)
