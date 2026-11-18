#!/usr/bin/env bash
# Builds the probes into build/rv (from the repo root): toks, bench, words, longid.
#   printf 'SELECT 1e-3' | build/rv/toks            every token and each byte's face
#   build/rv/bench reviews/pr-51-repro/tsql.sql 200 open     frame costs (modes in bench.c)
#   build/rv/words                                   the word table's order and classes
#   build/rv/longid FILE                             a long name before ".y"
mkdir -p build/rv
for p in toks bench words longid; do
	gcc -std=c17 -D_DEFAULT_SOURCE -O2 -Iinclude -o build/rv/$p reviews/pr-58-repro/$p.c src/syntax/tsql.c src/text/text.c || exit 1
done
