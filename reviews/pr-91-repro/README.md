Probes for the review of PR 91 (reviews/pr-91.md). Build each against the tree
with the Makefile's sources and objects (LIB_SRCS, OBJS, CPPFLAGS, LIBS), -O2.

- gen.py DIR: writes the test files (split.nix, wide.*, lualong*.nix, luareal.*).
- onestring.nix, returns.nix: cases that equal Neovim from every window tried.
- probe.c: `probe FILE w LINE N [e POS DEL STR] [w LINE N]...` shows windows and
  edits in order and prints the last window's faces as tests/test_syntax.lua
  renders them. Neovim's side: an empty FILE.ops next to the file, then
  `nvim --headless --clean -l tests/test_syntax.lua OUT DIR FILE`.
- bench.c: `bench FILE LINE [KEYS]` types KEYS keys at LINE with 50 lines
  visible; per key the frame's syntax_faces time before and after the parse,
  and the longest stretch of syntax_update between two calls of stop.
- measure.sh, measure.log: the 5 alternating runs quoted in the report
  (bench is this PR, bench_main is main 2d40092; ws is reviews/pr-87-repro/ws.c).
