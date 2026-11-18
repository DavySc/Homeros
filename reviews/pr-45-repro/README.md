# Sources of the PR 45 review (reviews/pr-45.md)

B is a checkout of PR 45 (ae7aa25), C the corpus of reviews/r16 (63 files).

    F="-std=c17 -D_DEFAULT_SOURCE -O2 -g -IB/include -IB/src/org"
    gcc $F -o enum enum.c B/src/org/org.c B/src/uni/uni.c      # ./enum 4
    gcc $F -o edits edits.c B/src/org/org.c B/src/uni/uni.c    # ./edits SEED N RESET FILES...
    gcc $F -fsanitize=address,undefined -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o oom oom.c B/src/org/org.c B/src/uni/uni.c

- `enum.c`: every text of up to K lines over 25 line kinds, every line and byte edit. `edits.c`: chained random edits on real files, half of them at node boundaries. `oom.c`: every allocation failing in turn.
- `work.c`: nodes reparsed and time per typing edit. It links a copy of org.c with three counters: `org_stat_nodes += no; org_stat_parses++;` before parse()'s `*count = no;`, `org_stat_whole++;` at `whole:`, and their definition. `INS` and `POS` set the inserted text and the place. `timing.sh`, `timing.log`: the 5 alternating runs.
- `agenda12.org` is the synthetic agenda 12 times; `agenda12-one-top.org` is `(echo "* All"; sed 's/^\*/**/' agenda12.org)`; `agenda-demoted.org` and `agenda12-demoted.org` are `sed 's/^\*/**/'` of the agenda and of agenda12.
- `one-parse.diff`: finding 1's fix as tried by the reviewer (org.c and the plant).
- `xplants.txt`, `xplants.sh`, `xplants.log`: the reviewer's 13 plants in org_edit against `make fuzz FUZZERS=org FUZZ_SECONDS=30`; `prplants.log`: the PR's 7 org plants.
