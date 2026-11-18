# Sources of the package 16 comparison (reviews/package-16.md)

Put `tools-env/bin` on PATH (Emacs 31.1, Org 9.8.7). A and B are checkouts of PR 21 and PR 22.

    F="-std=c17 -D_DEFAULT_SOURCE -O2 -g"
    gcc $F -IA/include -IA/src/org -o dump_a dump_a.c A/src/org/org.c A/src/text/*.c A/ref/uni.c
    gcc $F -IB/src/org -o dump_b dump_b.c B/src/org/org.c
    python3 cmp.py out A=./dump_a B=./dump_b -- FILES...      # or -- @list
    python3 classify.py out/A.diff out/B.diff

- `canon.el`: the reviewer's oracle (Org's parse by position). `dump_a.c`, `dump_b.c`: each build in the same format. `cmp.py`, `classify.py`, `show.sh`: the comparison.
- Texts: `A/src/org/oracle/gen.py SEED N DIR [FILES]`, `gen_b.py B/tools/org_conform.py SEED N DIR`, `gen_r.py SEED N DIR` (the reviewer's; `NOCALL=1` leaves babel calls out), `enumgen.py DIR K wide|core`, `sweep.py DIR`, `patho.py DIR`. The corpus is every `*.org` of bzg/org-mode c0ed324 and nvim-orgmode/orgmode 08c3ea6, `etc/ORG-NEWS` and `design/org/synthetic-agenda.org`.
- `bench.c` (`-DBUILD_A`, `-DBUILD_B`, `-DBUILD_B -DSUBTREE`), `timing.py`, `timing.log`: speed.
- `x.h`, `xa.c`, `xb.c`, `xnames.c`, `xfuzz.c`: both builds in one libFuzzer process (`-DRAW` for raw bytes); set `XDIR` to keep disagreeing texts.
- `oom.c` (link with `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc`), `edits_a.c`: failing allocations; A's incremental reparse against fresh parses.
- `xplants.sh`, `xplants-a.txt`, `xplants-b.txt`, `xplants-*.log`: the same 12 planted bugs in each build.
- `repro/`: one small text per difference or crash named in the report.
