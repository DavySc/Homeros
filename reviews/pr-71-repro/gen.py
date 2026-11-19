# gen.py DIR: my cases for the comparison with Neovim, as DIR/<i>.json, names in DIR/names.txt
import json, random, sys, os
os.chdir(sys.argv[1])
cases = {
 'num': '[0, -0, 1e5, 1E+5, 1e-5, 1.5e+10, 01, 1., .5, +1, -, 0x10, 1e, 1e+, -1.0E-2, 12abc, 1.2.3, Infinity, NaN]',
 'num_valid': '[0, -0, 1e5, 1E+5, 1e-5, 1.5e+10, -1.0E-2, 1., 1.e3]',
 'esc': '{"a\\u00e9\\n\\\\": "x\\"y\\\\", "\\\\\\\\": "\\uD83D\\uDE00", "": "", "k\\/": "\\q\\x41"}',
 'nest': '{"a": {"b": [{"c": null}, [], {}, [[1, "s"], true, false]]}, "d": "e" , "f" :1}',
 'keys': '{"a"\n:\n1, "b" /* c */ : 2, "c" // x\n : "v", "d": "k:" , "e": ["x:", "y"]}',
 'top_num': '42', 'top_arr': '["a", "b"]', 'top_true': 'true', 'top_null': ' null ',
 'trail': '{"a": 1,}', 'bare': '{a: 1, b: tru, c: nul, d: falsey, True: NULL}',
 'single': "{'a': 'b'}", 'dup': '{"a": 1} {"b": 2}\n{"c": "d"}', 'ndjson': '{"a": "b"}\n{"c": "d"}\n"x"\n["y"]\n',
 'strnl': '["abc\n", "d"]', 'unclosed': '{"a": "b', 'missing_colon': '{"a" 1, "b" "c"}', 'colon_in_arr': '["a": 1]',
 'utf8': '{"h\u00e9llo": "w\u00f6rld \u4e16\u754c", "\U0001F600": 1}', 'crlf': '{\r\n  "a": 1,\r\n  "b": "c"\r\n}\r\n',
 'cmt': '// top\n{ /* a */ "a": 1, // b\n "b": 2 /* c */ }\n/* end */', 'slash': '{"a": 1 / 2, "b": /x/}',
 'deep': '['*200 + '"x"' + ']'*200, 'emptystr_key_val': '{"":""}', 'tabs': '{\t"a"\t:\t"b"\t}',
 'ctl': '["a\tb", "c\x01d"]', 'esc_end': '["a\\\\", "b\\\\\\"", "\\\\"]', 'words': '[true,false,null,truefalse,nulltrue, true1, 1true]',
 'key_num_val': '{"1": 1, "true": true, "null": null}', 'nested_key_str': '{"a": {"b": "c"}, "d": ["e", {"f": "g"}]}',
 'colon_far': '{"a"' + ' \n'*3000 + ': "b"}',
}
random.seed(7)
def rv(d):
    r = random.random()
    if d > 6 or r < .25: return random.choice([None, True, False, 0, -1.5e-7, 12345678901234567890, 3.14, "", "str", "a\"b\\c\n\t\u00e9\u4e16", "http://x/y?z=1&q=//", "/* not */", "k: v"])
    if r < .6: return {random.choice(["id","name","a b","\u00e9","k\"q","","x:y","//", "nested"]) + str(i): rv(d+1) for i in range(random.randint(0,6))}
    return [rv(d+1) for _ in range(random.randint(0,6))]
for i in range(60):
    cases['py%d' % i] = json.dumps(rv(0), indent=random.choice([None, 1, 2, 4, '\t']), ensure_ascii=random.random() < .5, separators=random.choice([None, (',', ':'), (', ', ' : ')]))
cases['big'] = json.dumps([rv(0) for _ in range(150)], indent=2)
cases['bigmin'] = json.dumps([rv(0) for _ in range(150)], separators=(',', ':'))
cases['longstr'] = '{"data": "' + 'QUJD\\n' * 20000 + '", "k": 1}'
names = list(cases)
for i, k in enumerate(names):
    open('%d.json' % i, 'wb').write(cases[k].encode('utf-8'))
open('names.txt', 'w').write('\n'.join(names))
