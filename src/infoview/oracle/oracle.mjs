// The oracle (oracle/README.md): VS Code's real infoview page in Chromium,
// served by the bridge to a real Lean (lake serve), against Lean's own
// answer.  For each cursor position the goal the page shows must be the
// one $/lean/plainGoal gives there; then the page's "Try this" must edit
// the buffer.  node oracle.mjs DRIVE FILE.lean ROOT [N random positions]
import { createRequire } from 'node:module'
const { chromium } = createRequire(import.meta.url)('playwright')   // NODE_PATH: the global one
import { spawn } from 'node:child_process'
import { readFileSync, writeFileSync, existsSync, mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const [drive, file, root, nrand = '20', seed = '1'] = process.argv.slice(2)
const dir = mkdtempSync(join(tmpdir(), 'iv-oracle-'))
const opened = join(dir, 'opened'), opener = join(dir, 'open.sh')
writeFileSync(opener, '#!/bin/sh\necho "$1" >> ' + opened + '\n', { mode: 0o755 })
// a state directory of its own: the crash journal of an earlier run (whose
// buffer the page edited) would otherwise come back as the text
const ed = spawn(drive, [file], { env: { ...process.env, HOMEROS_INFOVIEW: root, HOMEROS_OPEN: opener, XDG_STATE_HOME: dir },
    stdio: ['pipe', 'pipe', 'inherit'] })
let out = ''
ed.stdout.on('data', d => { out += d })
const sleep = ms => new Promise(r => setTimeout(r, ms))
async function until(f, ms = 60000) {
    for (const end = Date.now() + ms; Date.now() < end; await sleep(50)) { const v = await f(); if (v) return v }
    throw new Error('timeout: ' + f)
}
async function ask(cmd) {
    out = ''
    ed.stdin.write(cmd + '\n')
    const r = await until(() => out.includes('\x04\n') && out)
    return r.slice(0, r.indexOf('\x04\n'))
}

const src = readFileSync(file, 'utf8').split('\n')
await until(() => existsSync(opened))
const html = readFileSync(readFileSync(opened, 'utf8').trim().replace('file://', ''), 'utf8')
const url = html.match(/url=([^"]+)"/)[1]
const browser = await chromium.launch()
const page = await browser.newPage()
const errors = []
page.on('pageerror', e => errors.push(String(e)))
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()) })
const t0 = Date.now()
await page.goto(url)
await until(() => page.evaluate(() => !!window.homerosInfoview))
console.log(`page up, infoview ready: ${Date.now() - t0} ms`)

// what the page shows as the tactic state: hypotheses and goals, as text
// (the text between "Tactic state" and the next section's title)
const shown = () => page.evaluate(() => {
    const t = document.body.innerText, a = t.indexOf('Tactic state\n')
    if (a < 0) return ''
    const rest = t.slice(a + 13), z = rest.search(/\n(All Messages|Expected type|Messages|Restart File)/)
    return z < 0 ? rest : rest.slice(0, z)
})
// both without what only one of them says: case names, goal counts, "no goals"
const norm = s => s.replace(/\u00a0/g, ' ').replace(/```lean\n?|```/g, '').split('\n').map(l => l.trim())
    .filter(l => l && !/^case /.test(l) && l !== '---' && !/^\d+ goals?$/.test(l) && !/^no goals$/i.test(l) &&
        !/^Goals accomplished/.test(l)).join('\n')

// the cursor positions: every line start + 2 inside each proof, and random ones
let rng = Number(seed)
const rnd = n => { rng = (rng * 1103515245 + 12345) % 2147483648; return rng % n }
const pos = []
src.forEach((l, i) => { if (l.startsWith('  ')) pos.push([i, Math.min(l.length, 2 + rnd(Math.max(1, l.length - 1)))]) })
for (let k = 0; k < Number(nrand); k++) { const i = rnd(src.length); pos.push([i, rnd(Buffer.byteLength(src[i]) + 1)]) }
// a byte column must not split a character
const fix = ([l, c]) => { const b = Buffer.from(src[l]); while (c > 0 && c < b.length && (b[c] & 0xC0) === 0x80) c--; return [l, c] }

let agree = 0, differ = 0
const slow = []
for (const p of pos.map(fix)) {
    const lean = norm((await ask(`g ${p[0]} ${p[1]}`)).replace(/^goal\n/, ''))
    const t = Date.now()
    ed.stdin.write(`c ${p[0]} ${p[1]}\n`)
    let got
    // the page has caught up when it shows Lean's goal; give it 8 s
    got = (await until(async () => { const s = norm(await shown()); return s === lean || Date.now() - t > 8000 ? { s } : null })).s
    slow.push(Date.now() - t)
    if (got === lean) agree++
    else { differ++; console.log(`DIFFER at ${p}:\n  lean: ${JSON.stringify(lean)}\n  page: ${JSON.stringify(got)}`) }
}
slow.sort((a, b) => a - b)
console.log(`goals: ${agree} agree, ${differ} differ of ${pos.length}; page caught up in median ${slow[slow.length >> 1]} ms (${slow[0]} to ${slow[slow.length - 1]})`)

// Try this: the suggestion's link in the page edits the buffer
const ex = src.findIndex(l => l.includes('exact?'))
let tried = 'no exact? in the file'
if (ex >= 0) {
    ed.stdin.write(`c ${ex} ${src[ex].indexOf('exact?')}\n`)
    try {
        await until(() => page.evaluate(() => document.body.innerText.includes('Try this')), 60000)
        // Lean 4.35's TryThis widget: "[apply] exact ...", [apply] the link
        await page.getByText('[apply]').first().click()
        const buf = await until(async () => { const b = await ask('p'); return !b.includes('exact?') && b }, 10000)
        tried = 'applied: ' + buf.split('\n')[ex].trim()
    } catch (e) { tried = 'FAILED: ' + e.message + '\npage:\n' + await page.evaluate(() => document.body.innerText) }
}
console.log('try this: ' + tried)
console.log('page errors: ' + (errors.length ? errors.join(' | ') : 'none'))
await browser.close()
ed.kill()
process.exit(differ || tried.startsWith('FAILED') || errors.length ? 1 : 0)
