# PR 95 review probes

Each `.c` builds with the command `make test` uses for a unit test (`make -n test TESTS=core_status`), with the probe in place of `tests/test_core_status.c` and `-Itests` added; `misc.c` with `-O2` instead of the sanitizers for its timing.

- `more.lua`: the PR's `tests/test_core_status.lua` set-up with 100 more states; run as that file says, output in `more.txt`.
- `more.c <more.txt>`: the core's status line against `more.txt`, cell by cell (text, face, background). Prints the states that differ.
- `misc.c <dir>`: buffers after `:e ./a`, a symlink, a hard link, `../x/a`; the mode pill after `d`. `misc.c <dir> bench <bytes> <frames>`: microseconds a frame with the cursor at the end of one line of that many bytes.
- `long.c`: a 3,818-byte branch and a 244-byte file name at every width, under the sanitizers.
- Finding 1's plant: in `src/core/core.c`, `undo_seq(b->undo) != b->saved_seq` to `undo_seq(b->undo) > b->saved_seq`, then `make test TESTS=core_screens`.
