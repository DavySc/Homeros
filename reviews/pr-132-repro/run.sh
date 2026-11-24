#!/usr/bin/env bash
# Runs each config here N times (default 2) with the tlc on PATH; prints what check.sh would conclude.
# FramesOwn extends the PR's Frames.tla, taken from design/specs for the run.
cd "$(dirname "$0")"; n=${1:-2}
[ -f Frames.tla ] || { cp ../../design/specs/Frames.tla . && trap 'rm -f Frames.tla' EXIT; }
for cfg in *.cfg; do for i in $(seq $n); do
  out=$(tlc -workers 4 -deadlock -metadir "${TMPDIR:-/tmp}/pr132-states/${cfg%.cfg}-$i-$$" -config "$cfg" "${cfg%%_*}.tla" 2>&1)
  if grep -q "No error has been found" <<<"$out"; then got=pass
  elif grep -qE "is violated|properties were violated" <<<"$out"; then got=fail
  else got=error; fi
  printf '%-26s want %-4s got %-5s %s | %s\n' "$cfg" "$(sed -n 's/^\\\* expect: //p' "$cfg")" "$got" \
    "$(grep -oE '[0-9]+ distinct states found' <<<"$out" | tail -1)" "$(grep -E 'violated' <<<"$out" | head -1)"
done; done
