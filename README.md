# Debugger prototype

A first native debugger for Homeros, built from the RAD Debugger knowledge base
(`/mnt/project-files/raddbg-kb/` in the project files). Davy, 2026-11-18: "You are free to build
the full debugger, but don't merge it to main, instead this will be a first prototype we can learn
from for when the editor is more fleshed out. Ensure that it is thoroughly stress tested each step
of the way".

**This branch is never merged to main.** It has no shared history with main on purpose, so the
Homeros checks do not run on it and a merge cannot happen by accident.

Rules: Windows first; one debug-engine thread under the six rules in the knowledge base's
`09-homeros-fit.md`; Windows work runs only on GitHub Actions runners; each slice is stress tested
before the next starts; no duplicated code.

| Path | What |
|---|---|
| `subjects/` | Small programs with known behaviour that the debugger is tested on |
| `oracle/` | Scripts that record what the real RAD Debugger (pinned commit) does on the subjects |
| `traces/` | Those recordings, committed by the runner; the engine's stepping is compared with them |
| `rdi/` | RAD's conversion of each subject's debug info and its text dump, committed by the runner |
| `dbg/` | The debugger: `rdi` (debug info reader), `queue` (the two queues between editor and engine thread) |
| `specs/` | TLA+ models, each with broken variants that must fail (`specs/check.sh`) |
| `tests/` | Known-answer tests, the stress test, fuzzers, the comparison with RAD's dump, `plants.txt` |

`./check.sh` runs everything (set `TLA_JAR` to `tla2tools.jar`); `./check.sh plants` shows every check
fails on its planted bugs.
