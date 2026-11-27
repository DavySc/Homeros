import subprocess,shutil,os
P='.'
src=open(P+'/src/syntax/lean.c').read()
plants=[
 ("is_sp without U+00A0","c == 0x85 || c == 0xa0 ||","c == 0x85 ||"),
 ("code span closer may follow a backtick","if (at(m, q) != '`' || at(m, q - 1) == '`') continue;","if (at(m, q) != '`') continue;"),
 ("link text brackets not balanced","else if (c == ']' && !d--) return p;","else if (c == ']') return p;"),
 ("a title may be empty","for (size_t j = adv(m->x->r, p + 1); j < m->eol; j++)","for (size_t j = p + 1; j < m->eol; j++)"),
 ("seven # is a heading","if (!n || n > 6 ||","if (!n || n > 7 ||"),
 ("a separator of two marks","if (k < 2 || sp_tab","if (k < 1 || sp_tab"),
 ("a character after ]","|| b == ']' || is_w(b)) return 0;","|| is_w(b)) return 0;"),
 ("head does not stop at U+2983","if (in(c, \":|([{<>\") || c == 0x2983) break;","if (in(c, \":|([{<>\")) break;"),
 ("bold/italic stack 2 deep","m->nin == (int)sizeof m->in","m->nin == 2"),
 ("closer after a blank closes","if (!is_sp(before(r, q)) && c == dc && (!bold || at(m, q + 1) == dc)) break;","if (c == dc && (!bold || at(m, q + 1) == dc)) break;"),
 ("list continuation of 5 spaces","for (i = q; i < q + 4 && at(m, i) == ' '; i++) ;","for (i = q; i < q + 5 && at(m, i) == ' '; i++) ;"),
 ("no ftp autolink","|| lit(r, q, \"ftp://\"))",")"),
 ("URL items past 64 dropped","else b[63] = q;","else (void)0;"),
 ("exponent without sign","if (tsql_byte(r, e) == '+' || tsql_byte(r, e) == '-') e++;","if (tsql_byte(r, e) == '-') e++;"),
 ("escape set without >","\"-`*_#+.!(){}[]\\\\>\"","\"-`*_#+.!(){}[]\\\\\""),
 ("deriving instance across a line","is_sp(cp(r, e)) && cp(r, e) != '\\n' && (e = lit(r, blank(r, e), \"instance\"))","is_sp(cp(r, e)) && (e = lit(r, blank(r, e) + (cp(r, blank(r, e)) == '\\n'), \"instance\"))"),
 ("guillemet content face in a head","guillemets(x, q, b, FACE_FUNCTION_DEFINITION)","guillemets(x, q, b, b)"),
 ("setext of two =","if (e < q + 3 || sp_tab(m, e) < m->eol) return 0;","if (e < q + 2 || sp_tab(m, e) < m->eol) return 0;"),
 ("quote marker without its space","size_t k = e + 1 + (at(m, e + 1) == ' ');\n\t\tpc(m, q, e, COL(PI, IT))","size_t k = e + 1;\n\t\tpc(m, q, e, COL(PI, IT))"),
 ("#eval's ! variants: cmds need no word end","if (c == '#' && (e = alt(r, p + 1, lean_cmds, N(lean_cmds), 0)))","if (c == '#' && (e = lit(r, p + 1, \"eval\")))"),
]
os.makedirs('build/pl',exist_ok=True)
for name,a,b in plants:
    if src.count(a)!=1: print("NOT APPLIED (%d):"%src.count(a),name); continue
    open('build/pl/lean.c','w').write(src.replace(a,b))
    for h in ('lean.h','lean_words.h'): shutil.copy(P+'/src/syntax/'+h,'build/pl/')
    c=subprocess.run(f"gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -std=c17 -D_DEFAULT_SOURCE -I{P}/include -Ibuild/pl -I{P}/tests -o build/pl/replay reviews/pr-144-repro/replay.c {P}/src/syntax/tsql.c build/pl/lean.c {P}/src/text/*.c {P}/src/uni/*.c {P}/ref/theme.c",shell=True,capture_output=True,text=True)
    if c.returncode: print("BUILD FAIL",name,c.stderr[-300:]); continue
    r=subprocess.run(['build/pl/replay',P+'/tests/test_syntax_lean.txt'],capture_output=True,text=True)
    line=[l for l in (r.stdout+r.stderr).split('\n') if 'bytes in' in l]
    print(("caught" if r.returncode else "MISSED"),'|',name,'|',line[0][18:] if line else (r.stderr[-200:]))
