// Lean's colours as VS Code paints them: vscode-lean4's TextMate grammar
// (pinned below) run by vscode-textmate under Catppuccin Mocha
// (@catppuccin/vscode), the reference src/syntax/lean.c copies.
//   npm install vscode-textmate@9.3.2 vscode-oniguruma@2.0.1 @catppuccin/vscode@3.18.1
//   NODE_PATH=node_modules node tests/test_syntax_lean.js record > tests/test_syntax_lean.txt
//   NODE_PATH=node_modules node tests/test_syntax_lean.js words > src/syntax/lean_words.h
// record: cases from Lean's own sources (lean --print-prefix, src/lean),
// random text from Lean's tokens and random Markdown in a doc comment
// (CASES of them, 600 by default, from SEED), each as T (hex) and C
// (colour runs over a palette P of fg/fontStyle, newlines left out), for
// test_syntax_lean.
// words: the grammar's word lists and Oniguruma's \w, for lean.c.
const fs = require('fs'), path = require('path'), cp = require('child_process');
const vt = require('vscode-textmate'), on = require('vscode-oniguruma');
const PIN = 'https://raw.githubusercontent.com/leanprover/vscode-lean4/9ac77ca57bf7b7ee5907269142a5bef979aa15bf/vscode-lean4/syntaxes/';
const wasm = fs.readFileSync(path.join(path.dirname(require.resolve('vscode-oniguruma')), 'onig.wasm')).buffer;
const onig = on.loadWASM(wasm).then(() => ({ createOnigScanner: p => new on.OnigScanner(p), createOnigString: s => new on.OnigString(s) }));
const get = f => fetch(PIN + f).then(r => r.text());

async function record() {
  const files = { 'source.lean4': await get('lean4.json'), 'source.lean4.markdown': await get('lean4-markdown.json') };
  const mocha = JSON.parse(fs.readFileSync(require.resolve('@catppuccin/vscode/themes/mocha.json')));
  const reg = new vt.Registry({ onigLib: onig, loadGrammar: async s => files[s] ? vt.parseRawGrammar(files[s], s + '.json') : null,
    theme: { name: 'mocha', settings: mocha.tokenColors } });
  const g = await reg.loadGrammar('source.lean4'), cmap = reg.getColorMap();
  // check.h's xorshift, seeded apart from it
  let s = BigInt(process.env.SEED || '0x2545F4914F6CDD1D');
  const rnd = n => { s ^= (s << 13n) & 0xffffffffffffffffn; s ^= s >> 7n; s ^= (s << 17n) & 0xffffffffffffffffn; return Number(s % BigInt(n)); };
  const src = path.join(cp.execSync('lean --print-prefix').toString().trim(), 'src/lean');
  const corpus = cp.execSync(`find ${src} -name '*.lean' | LC_ALL=C sort`).toString().trim().split('\n');
  const tok = ['\n', ' ', '  ', '--', '/-', '-/', '/--', '/-!', '"', '\\', '\\"', '\\n', '\\x4', '\\u00e9', "'", 'a', 'x1', '0', '12', '0x1F', '-', '.', '3.5e-2',
    'e', 'def', 'theorem', 'structure', 'instance', 'where', 'with', 'extends', ':', ':=', '(', ')', '{', '}', '[', ']', '⦃', '<', '>', '|', ',', '«', '»',
    'attribute', '@', '@[simp]', 'private', 'sorry', '#eval', '#check', 'deriving', 'instance', 'by', 'by?', 'fun', 'λ', '∀', '→', 'α', 'ℕ', '₁', '⟨', '⟩',
    's!', 'm!', 'throwError', 'panic!', 'trace[Meta.x]', 'trace_goal[', 'Prop', 'Type', 'Sort', 'true', 'false', 'termination_by', '?', '_', 'import all',
    '`', '*', '#', 'é', '𝔽'], md = ['\n', '\n', ' ', '  ', '    ', '\t', 'a', 'b c', 'é', '`', '``', '*', '**', '_', '__', '#', '###', '####### ',
    '> ', '- ', '* ', '+ ', '1. ', '```', '~~~', '```lean', '[', ']', '(', ')', '![', '<', '>', '<http://a.b/c>', '<mailto:a.b@c-d.ef>', '<x@y.z>',
    '\\', '\\*', '---', '***', '===', '-/', '/-', '/--', '/-!', '--', '"', "'", ':', '[a]: <u> "t"', '[t](u "x")', '(u (v))', '-', '.', '!'];
  const soup = (w, n) => { let t = ''; for (let k = rnd(n); k--; ) t += w[rnd(w.length)]; return t; };
  const cases = [];
  for (let c = 0; c < (+process.env.CASES || 600); c++) {
    let t;
    if (c % 3 == 1) {
      const ls = fs.readFileSync(corpus[rnd(corpus.length)], 'utf8').split('\n'), a = rnd(ls.length);
      t = ls.slice(a, a + 80).join('\n') + '\n';
    } else t = c % 3 ? soup(tok, 300) : '/-- ' + soup(md, 400);
    cases.push(t);
  }
  const pal = [], out = [];
  for (const t of cases) {
    let st = vt.INITIAL, runs = [], cur = -1, n = 0;
    for (const line of t.split('\n')) {
      const { tokens: tk, ruleStack } = g.tokenizeLine2(line, st);
      let k = 0;
      for (let i = 0; i < line.length; i++) {
        while (k + 2 < tk.length && tk[k + 2] <= i) k += 2;
        const m = tk[k + 1], key = cmap[(m >>> 15) & 511].slice(1) + '/' + ((m >>> 11) & 15);
        let id = pal.indexOf(key);
        if (id < 0) id = pal.push(key) - 1;
        const c = line.codePointAt(i), b = Buffer.byteLength(String.fromCodePoint(c));
        if (c > 0xffff) i++;
        if (id !== cur) { if (n) runs.push(String.fromCharCode(97 + cur) + n); cur = id, n = 0; }
        n += b;
      }
      st = ruleStack;
    }
    if (n) runs.push(String.fromCharCode(97 + cur) + n);
    out.push('T ' + Buffer.from(t).toString('hex'), 'C ' + runs.join(' '));
  }
  console.log('P ' + pal.map((p, i) => String.fromCharCode(97 + i) + '=' + p).join(' '));
  console.log(out.join('\n'));
}

async function words() {
  const g = JSON.parse(await get('lean4.json')), lib = await onig;
  const re = g.patterns.map(p => p.match || p.begin || '');
  // a group's alternatives, as written: \? is ?, \[[^\]\s]*\] is a bracket ("[")
  const alts = (r, i = 0) => {
    const m = /\((?!\?)((?:[^()\\]|\\.|\(\?:[^)]*\))*)\)/.exec(r.slice(r.indexOf('(', i)));
    return m[1].replace(/\(\?:([^)]*)\)\\\[\[\^\\\]\\s\]\*\\\]/, (_, a) => a.split('|').map(x => x + '[').join('|'))
      .replace(/\\\[\[\^\\\]\\s\]\*\\\]/g, '[').split('|').map(w => w.replace(/\\(.)/g, '$1'));
  };
  const list = (name, r) => `static const char *const ${name}[] = {\n\t` +
    alts(r).map(w => JSON.stringify(w)).join(', ').replace(/(.{70,}?), /g, '$1,\n\t') + ',\n};\n';
  const at = s => re.find(r => r.includes(s));
  let o = '/* Generated by tests/test_syntax_lean.js words from vscode-lean4\'s\n   syntaxes/lean4.json (9ac77ca): its word lists in its order (a word\n   ending in [ takes a bracket, \\[[^\\]\\s]*\\]), and Oniguruma\'s \\w as\n   code point ranges. */\n';
  o += list('lean_types', at('Prop|')) + list('lean_mods', at('local|')) + list('lean_cmds', at('#(print'));
  o += list('lean_defs', at('(inductive|')) + list('lean_kws', at('(theorem|show')) + list('lean_istr', at('(s!|')) + list('lean_bools', at('true|'));
  const sc = new on.OnigScanner(['\\w']), w = [];
  for (let c = 0, a = -1; c <= 0x110000; c++) {
    let y = false;
    if (c < 0x110000 && (c < 0xd800 || c >= 0xe000)) { const s = new on.OnigString(String.fromCodePoint(c)); y = !!sc.findNextMatchSync(s, 0); s.dispose(); }
    if (y && a < 0) a = c;
    if (!y && a >= 0) w.push(`{ 0x${a.toString(16)}, 0x${(c - 1).toString(16)} }`), a = -1;
  }
  o += 'static const uint32_t lean_word[][2] = {\n\t' + w.join(', ').replace(/(.{60,}?\}), /g, '$1,\n\t') + ',\n};\n';
  process.stdout.write(o);
}

(process.argv[2] === 'words' ? words : record)();
