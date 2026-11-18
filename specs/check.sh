#!/bin/bash
# Runs every model. Each .cfg says on its first line whether TLC must pass or fail it; a fail only
# counts when TLC reports a violated property (the end of a session is a state with no next step, so
# deadlock is not checked). Needs TLA_JAR (tla2tools.jar) or tlc on PATH.
cd "$(dirname "$0")"; rc=0
tlc() { if [ -n "${TLA_JAR:-}" ]; then java -XX:+UseParallelGC -cp "$TLA_JAR" tlc2.TLC "$@"; else command tlc "$@"; fi; }
for cfg in *.cfg; do
  d=$(mktemp -d)
  out=$(tlc -workers 4 -deadlock -metadir "$d" -config "$cfg" "${cfg%%_*}.tla" 2>&1); rm -rf "$d"
  if grep -q "No error has been found" <<<"$out"; then got=pass
  elif grep -qE "is violated|(was|were) violated" <<<"$out"; then got=fail
  else got=error; fi
  want=$(sed -n '1s/^\\\* expect: //p' "$cfg")
  [ "$got" = "$want" ] && s=OK || { s=WRONG; rc=1; }
  printf '%-26s want %-4s got %-5s %s  %s  %s\n' "$cfg" "$want" "$got" "$s" "$(grep -o '[0-9]* distinct states found' <<<"$out" | tail -1)" "$(grep -oE 'Invariant [A-Za-z]+ is violated|Temporal properties were violated' <<<"$out" | head -1)"
done
exit $rc
