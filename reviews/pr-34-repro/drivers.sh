#!/usr/bin/env bash
# Stand-in drivers under a rule that matches every case: which failures can
# still count as known?  Run from the repo root with nvim on PATH.
t=$(mktemp -d); cp -r tools tests "$t"/; cd "$t"
printf '%s' '[{"why": "all", "keys": ".", "example": {"text": "ab\n", "keys": ["x", "x", "x"]}}]' > tests/conform-known.json
mk(){ printf '#!/bin/sh\n%s\n' "$2" > $1; chmod +x $1; }
mk d_sig 'kill -SEGV $$'; mk d_garbage 'echo not json'; mk d_empty 'echo {}'; mk d_hang 'sleep 60'
for d in d_sig d_garbage d_empty d_hang; do python3 tools/conform.py ./$d 3 1 2>&1 | tail -1 | cut -c1-160; echo "$d: exit ${PIPESTATUS[0]}"; done
