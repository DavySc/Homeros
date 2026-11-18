#!/bin/bash
# The prototype's check. `./check.sh` builds with gcc and clang (-Werror), runs the tests under
# ASan+UBSan and each fuzzer for FUZZ_SECONDS (default 20). `./check.sh plants` then proves each check
# can fail: every line of tests/plants.txt must make its check fail.
set -u
cd "$(dirname "$0")"
FLAGS="-std=c17 -g -O1 -Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
run() { # run <dir with the sources> <test|fuzz|all>: 0 when the named checks pass
  local src=$1 what=$2 b; b=$(mktemp -d)
  python3 -I "$src/tests/seed_rdi.py" "$b/seed.rdi" || return 1
  if [ "$what" != fuzz ]; then
    for cc in gcc clang; do
      $cc $FLAGS $SAN "$src"/dbg/*.c "$src"/tests/test_*.c -o "$b/test-$cc" || return 1
      "$b/test-$cc" "$b/seed.rdi" >"$b/out" 2>&1 || { tail -5 "$b/out"; return 1; }
    done
  fi
  if [ "$what" != test ]; then
    for f in "$src"/tests/fuzz_*.c; do
      clang $FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all "$src"/dbg/*.c "$f" -o "$b/fuzz" || return 1
      mkdir -p "$b/corpus" && cp "$b/seed.rdi" "$b/corpus/" && cp "$src"/rdi/*.rdi "$b/corpus/" 2>/dev/null
      (cd "$b" && ./fuzz corpus -max_total_time="${FUZZ_SECONDS:-20}" -max_len=65536 >fuzz.log 2>&1) || { grep -m2 "ERROR\|SUMMARY" "$b/fuzz.log"; return 1; }
    done
  fi
  rm -rf "$b"
}
if [ "${1:-}" != plants ]; then run . all && echo "check ok"; exit; fi
missed=0; total=0
while IFS='|' read -r what file find repl; do
  case "$what" in ''|'#'*) continue;; esac
  what=$(echo $what); file=$(echo $file); find=${find# }; find=${find% }; repl=${repl# }
  t=$(mktemp -d); cp -r dbg tests "$t/"; [ -d rdi ] && cp -r rdi "$t/"
  FIND="$find" REPL="$repl" python3 -I -c 'import os,sys;p=sys.argv[1];s=open(p).read();f=os.environ["FIND"];assert s.count(f)==1,("plant text not found once: "+f);open(p,"w").write(s.replace(f,os.environ["REPL"]))' "$t/$file" || { echo "BAD PLANT: $find"; missed=$((missed+1)); continue; }
  total=$((total+1))
  if run "$t" "$what" >/dev/null 2>&1; then echo "NOT CAUGHT ($what): $find"; missed=$((missed+1)); fi
  rm -rf "$t"
done < tests/plants.txt
echo "plants: $((total-missed)) of $total caught"; [ $missed = 0 ]
