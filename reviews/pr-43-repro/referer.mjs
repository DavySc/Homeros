// What a site the page loads from learns of the token:
// node referer.mjs CHILD ROOT.  Starts the child, opens the page in
// Chromium, has it fetch an image from another origin and follow a link
// there, and prints the Referer each request carried.
import { createRequire } from 'node:module'
const { chromium } = createRequire(import.meta.url)('playwright')
import { spawn } from 'node:child_process'
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs'
import { createServer } from 'node:http'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
const [child, root] = process.argv.slice(2)
const dir = mkdtempSync(join(tmpdir(), 'iv-ref-'))
const opener = join(dir, 'open.sh')
writeFileSync(opener, '#!/bin/sh\n', { mode: 0o755 })
const ch = spawn(child, [root, opener], { env: { ...process.env, XDG_RUNTIME_DIR: dir }, stdio: ['pipe', 'pipe', 'inherit'] })
const first = await new Promise(r => ch.stdout.once('data', d => r(String(d).trim())))
const url = readFileSync(first, 'utf8').match(/url=([^"]+)"/)[1]
const seen = []
const other = createServer((q, s) => { seen.push(`${q.url}: Referer ${q.headers.referer ?? '(none)'}; Origin ${q.headers.origin ?? '(none)'}`); s.end('x') })
await new Promise(r => other.listen(0, '127.0.0.1', r))
const o = `http://localhost:${other.address().port}`      // another origin (another host name and port)
const browser = await chromium.launch()
const page = await browser.newPage()
await page.goto(url)
await page.evaluate(o => new Promise(r => { const i = new Image(); i.onload = i.onerror = r; i.src = o + '/image' }), o)
await page.evaluate(o => fetch(o + '/fetch', { mode: 'no-cors' }).catch(() => 0), o)
await page.evaluate(o => { const a = document.createElement('a'); a.href = o + '/link'; a.textContent = 'x'; document.body.append(a); a.click() }, o)
await page.waitForURL(o + '/link').catch(() => 0)
console.log(`the page's address has a ${url.split('/')[3].length}-character token`)
for (const s of seen) console.log('  ' + s)
await browser.close()
ch.stdin.end()
other.close()
