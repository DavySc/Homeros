"""Random Zig programs whose own std.fmt output is the truth a debugger must reproduce.

gen.py SEED > prog.zig   writes the program to stdout and, on stderr, one line per probe:
    PROBE <line> <name> <name> ...
At <line> every listed local holds its final value; the program then prints each with
"{any}" on its own line, in the listed order. A debugger stopped at <line> must print the
same text for each name.
"""
import random, sys

R = random.Random(int(sys.argv[1]))
MAXD = 3
decls = []          # type declarations, emitted at file scope
counter = [0]

def fresh(p):
    counter[0] += 1
    return f"{p}{counter[0]}"

def ident():
    return R.choice("abcdefghjkmnpqrstuvwxyz") + R.choice(["", "x", "_y", "zz", "w2"])

def names(n):
    out = []
    while len(out) < n:
        s = ident()
        if s not in out and s not in ("x", "u", "f", "i"):
            out.append(s)
    return out

# A type is (zig_spelling, value_generator, kind). value_generator() returns a Zig literal.
def t_int():
    signed = R.random() < 0.4
    w = R.choice([1, 2, 3, 7, 8, 13, 16, 31, 32, 33, 63, 64])
    lo, hi = (-(1 << (w - 1)), (1 << (w - 1)) - 1) if signed else (0, (1 << w) - 1)
    def v():
        r = R.random()
        if r < 0.15: return str(lo)
        if r < 0.3: return str(hi)
        return str(R.randint(lo, hi))
    return (("i" if signed else "u") + str(w), v, "int")

def t_float():
    ty = R.choice(["f32", "f64", "f16"])
    def v():
        r = R.random()
        if r < 0.5: return repr(R.randint(-4000, 4000) / 8)
        if ty == "f64": return repr(R.uniform(-1e6, 1e6))
        return repr(R.randint(-64, 64) / 4)
    return (ty, v, "float")

def t_bool():
    return ("bool", lambda: R.choice(["true", "false"]), "bool")

def t_enum():
    n = fresh("En")
    fs = names(R.randint(1, 5))
    tag = R.choice(["", "(u8)", "(u16)", "(i8)"])
    decls.append(f"const {n} = enum{tag} {{ {', '.join(fs)} }};")
    return (n, lambda: "." + R.choice(fs), "enum")

def t_struct(d):
    n = fresh("St")
    fs = names(R.randint(1, 4))
    tys = [t_any(d + 1) for _ in fs]
    decls.append(f"const {n} = struct {{ {', '.join(f'{f}: {t[0]}' for f, t in zip(fs, tys))} }};")
    return (n, lambda: ".{ " + ", ".join(f".{f} = {t[1]()}" for f, t in zip(fs, tys)) + " }", "struct")

def t_packed():
    n = fresh("Pk")
    fs = names(R.randint(1, 4))
    tys = [R.choice([t_int, t_bool])() for _ in fs]
    tys = [t if t[2] != "int" or int(t[0][1:]) <= 16 else t_bool() for t in tys]
    decls.append(f"const {n} = packed struct {{ {', '.join(f'{f}: {t[0]}' for f, t in zip(fs, tys))} }};")
    return (n, lambda: ".{ " + ", ".join(f".{f} = {t[1]()}" for f, t in zip(fs, tys)) + " }", "struct")

def t_union(d):
    n = fresh("Un")
    fs = names(R.randint(1, 4))
    tys = [t_any(d + 1) if R.random() < 0.8 else ("void", lambda: "{}", "void") for _ in fs]
    decls.append(f"const {n} = union(enum) {{ {', '.join(f'{f}: {t[0]}' for f, t in zip(fs, tys))} }};")
    def v():
        i = R.randrange(len(fs))
        return ".{ ." + fs[i] + " = " + tys[i][1]() + " }"
    return (n, v, "union")

def t_opt(d):
    c = t_any(d + 1)
    return ("?" + c[0], lambda: "null" if R.random() < 0.3 else c[1](), "opt")

def t_errset():
    n = fresh("Er")
    es = [s.capitalize() for s in names(R.randint(1, 4))]
    decls.append(f"const {n} = error{{ {', '.join(es)} }};")
    return (n, lambda: "error." + R.choice(es), "err")

def t_eu(d):
    e = t_errset()
    c = t_any(d + 1)
    while c[2] in ("err", "eu"):
        c = t_any(d + 1)
    return (e[0] + "!" + c[0], lambda: e[1]() if R.random() < 0.4 else c[1](), "eu")

def t_array(d):
    c = t_any(d + 1)
    n = R.randint(0, 4)
    return (f"[{n}]{c[0]}", lambda: ".{ " + ", ".join(c[1]() for _ in range(n)) + " }", "array")

def t_vector():
    c = t_int()
    n = R.randint(1, 4)
    return (f"@Vector({n}, {c[0]})", lambda: ".{ " + ", ".join(c[1]() for _ in range(n)) + " }", "vector")

def t_any(d=0):
    leaf = [t_int, t_int, t_float, t_bool, t_enum]
    inner = [lambda: t_struct(d), lambda: t_packed(), lambda: t_union(d), lambda: t_opt(d),
             lambda: t_eu(d), lambda: t_array(d), t_errset, t_vector]
    return R.choice(leaf + (inner if d < MAXD else []))()

lines = ['const std = @import("std");']
body = []
probes = []
nfun = R.randint(1, 4)
for fi in range(nfun):
    locs = []
    fl = [f"fn probe{fi}() void {{"]
    for nm in names(R.randint(1, 6)):
        t = t_any()
        fl.append(f"    var {nm}: {t[0]} = {t[1]()};")
        locs.append(nm)
        if R.random() < 0.25 and t[2] in ("int", "struct", "array"):   # slices and pointers into it
            if t[2] == "array":
                s = nm + "_s"
                fl.append(f"    var {s}: []{t[0][t[0].index(']') + 1:]} = &{nm};")
                locs.append(s)
            else:
                s = nm + "_p"
                fl.append(f"    var {s}: *{t[0]} = &{nm};")
                locs.append(s)
    fl.append("    _ = .{ " + ", ".join("&" + n for n in locs) + " };")
    probe_line = len(lines) + len(body) + len(fl) + 1
    for n in locs:
        fl.append(f'    std.debug.print("{{any}}\\n", .{{{n}}});')
    fl.append("}")
    body += fl
    probes.append((probe_line, locs))
# decls go before the functions: line numbers shift by their count
out = lines + decls + body + ["pub fn main() void {"] + [f"    probe{i}();" for i in range(nfun)] + ["}"]
print("\n".join(out))
for ln, locs in probes:
    print("PROBE", ln + len(decls), *locs, file=sys.stderr)
