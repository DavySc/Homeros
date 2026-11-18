# The infoview oracle

The real `@leanprover/infoview` 0.14.0 (the one VS Code runs) in Chromium,
against Lean's own answers, through the whole bridge: `drive.c` (an editor
with a Lean file, its server through lsp, the bridge open) and the child
`homeros-infoview`, with `oracle.mjs` driving the page with Playwright.

For N random cursor positions it moves the cursor, waits for the page's
"Tactic state" to settle, and compares it with `$/lean/plainGoal` at the
same place (both with case lines, goal counts and "no goals" dropped, and
non-breaking spaces as spaces). Then it clicks a "Try this" suggestion's
`[apply]` and checks the buffer changed to it.

Not run by `make check` (it needs Lean, a Lake project, Chromium and the
infoview files). To run it:

    src/infoview/install.sh ROOT          # the infoview's files, sha256 checked
    gcc ... -o drive src/infoview/oracle/drive.c (the editor's sources)
    gcc ... -o homeros-infoview src/infoview/serve/serve.c   # beside drive
    cd LAKE_PROJECT && XDG_STATE_HOME=$(mktemp -d) \
      NODE_PATH=$(npm root -g) node oracle.mjs ./drive Demo.lean ROOT 20

`XDG_STATE_HOME` must be fresh: an old crash journal replays its edits into
the file. Kill stray `lake serve` processes afterwards.

## Results (measured, Lean 4.35, Demo.lean with `exact?`)

| run | goals agree | page caught up after a move: median (range) | Try this applied | page errors |
|---|---|---|---|---|
| 1 | 18 of 18 | | yes | none |
| 2 | 18 of 18 | | yes | none |
| 3 | 28 of 28 | 108 ms (2 to 146) | yes | none |
| 4 (after the hello fix) | 28 of 28 | 112 ms (2 to 165) | yes | none |

The page is up and the infoview ready in about 0.4 s (measured, 2 runs:
396 ms in run 4).
