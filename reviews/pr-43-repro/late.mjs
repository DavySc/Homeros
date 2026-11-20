// Finding 1 in the real infoview: node late.mjs DRIVE FILE.lean ROOT
// (as oracle.mjs; DRIVE is drive.c with R43_LATE_MS read before
// bridge_open, see run-late.sh).  Prints what the page shows 10 s after
// it is up, the cursor having moved onto a tactic line.
import { createRequire } from 'node:module'
const { chromium } = createRequire(import.meta.url)('playwright')
import { spawn } from 'node:child_process'
import { readFileSync, writeFileSync, existsSync, mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
const [drive, file, root] = process.argv.slice(2)
const dir = mkdtempSync(join(tmpdir(), 'iv-late-'))
const opened = join(dir, 'opened'), opener = join(dir, 'open.sh')
writeFileSync(opener, '#!/bin/sh\necho "$1" >> ' + opened + '\n', { mode: 0o755 })
const ed = spawn(drive, [file], { env: { ...process.env, HOMEROS_INFOVIEW: root, HOMEROS_OPEN: opener, XDG_STATE_HOME: dir },
    stdio: ['pipe', 'pipe', 'inherit'] })
const sleep = ms => new Promise(r => setTimeout(r, ms))
while (!existsSync(opened)) await sleep(50)
const html = readFileSync(readFileSync(opened, 'utf8').trim().replace('file://', ''), 'utf8')
const browser = await chromium.launch()
const page = await browser.newPage()
await page.goto(html.match(/url=([^"]+)"/)[1])
await sleep(3000)
ed.stdin.write('c 1 2\n')
await sleep(7000)
console.log(`R43_LATE_MS=${process.env.R43_LATE_MS || 0}: the page shows ${JSON.stringify(await page.evaluate(() => document.body.innerText))}`)
await browser.close()
ed.kill()
process.exit(0)
