Repro files for reviews/pr-44.md. A = the PR head with main's tools/planted.sh, B = the PR head.

- `plants-notools.txt`: one plant per tool-dependent target, run with tools-env off PATH (runs.log, A.out and B.out).
- `plants-frama.txt` with `fake-why3` (as `why3`) first on PATH and no frama-c: A reads the echoed grep line as a catch, B does not (A.frama.out, B.frama.out).
- `plants-undo-diff.txt`: the five undo-diff plants; five runs without SEED, one with SEED=44220282886.
- `nightly-step.sh`, `issue-step.sh`: the workflow's two steps, run with `bash --noprofile --norc -eo pipefail`; `fake-gh` (as `gh`) records the calls in /tmp/gh.calls, FAKE_N is the issue number its list returns.

To run a plant list: in a copy of the tree, replace tests/plants/*.txt with the list and run `bash tools/planted.sh`.
