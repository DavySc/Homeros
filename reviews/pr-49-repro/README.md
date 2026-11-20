# PR 49 review drivers

`org_ts` compared with `org-element-timestamp-parser` at every position of random texts (case-fold-search t, as in a paragraph).

    export PATH=$PWD/tools-env/bin:$PATH        # Emacs 31.1, Org 9.8.7
    gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -O1 -g -fsanitize=address,undefined \
        -o ts_drv reviews/pr-49-repro/ts_drv.c src/org/org.c src/uni/uni.c
    python3 -I reviews/pr-49-repro/gen_ts.py 4000 SEED dir
    python3 -I reviews/pr-49-repro/cmp.py ./ts_drv dir

`gen_ts.py` joins 1 to 40 tokens from a pool of timestamp pieces (brackets, dates, times with hours 0 to 39, diary sexps, ranges, repeaters and warnings in both cases, `\r`, newlines, non-ASCII). `cmp.py` prints the first 8 differing files and a count.
