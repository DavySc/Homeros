# Repro files for the review of PR 29 (package 17, git)

Headless driver (as tests/test_git.c drives the views): from the repo root

    sh reviews/pr-29-repro/build.sh drive
    . reviews/pr-29-repro/env.sh; mkdir -p /tmp/rv; sh src/git/mkrepo.sh /tmp/rv/r1
    sh reviews/pr-29-repro/f1.sh        # and f1b f2 f3 f4 f5 f7

`drive REPO` reads steps on stdin: `o` open the status, `k keys`, `s` settle,
`$ cmd` a shell command in the repository, `p` print the view (`>` marks the
cursor), `l ms` one loop turn, `c n` cursor to visible line n, `z n` n other
child processes, `f` git_free.

- f1.sh   "Discard hunk?" answered after an outside change discards another hunk
- f1b.sh  "Discard unstaged changes in a.txt?" answered after an outside change loses the new line
- f2.sh   a buried log buffer is read again after every write (git shim log)
- f3.sh   the log opened twice before a loop turn shows both logs
- f4.sh   a nearly full process table: sections missing, no message
- f5.sh   linked work tree: no Tag header, no Stashes
- f7.sh   git_free with git busy kills the process in loop slot 0
- f6: needs a core with several buffers.  Stand-in: this PR merged with PR 20
  (core) at 30ae225, `int core_changed;` added, src/main.c patched with
  standin-main.diff (Ctrl-\ g g = SPC g g, Ctrl-\ x = :bd!).  Then
  `python3 reviews/pr-29-repro/term.py /tmp/rv/r4/a.txt '\x1cgg' cc '~0.5' '\x1cx' '@cd /tmp/rv/r4 && git add a.txt' '~1' gr '~1' =`
  shows a.txt still under Unstaged changes; git commit waits for ever.
- extra-plants.py: 18 more plants for `make test TESTS=git`.
