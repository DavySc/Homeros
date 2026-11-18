#!/bin/bash
# The prototype's check. `./check.sh` builds with gcc and clang (-Werror), runs the tests under
# ASan+UBSan and ThreadSanitizer, checks the TLA+ models (needs TLA_JAR or tlc), compares the reader with RAD's dump of every file in rdi/, and runs each fuzzer for FUZZ_SECONDS (default 20). `./check.sh plants` then proves each check
# can fail: every line of tests/plants.txt must make its check fail.
set -u
cd "$(dirname "$0")"
# a small queue, so the stress test runs with both queues full most of the time
FLAGS="-std=c17 -D_GNU_SOURCE -pthread -DQUEUE_BYTES=256 -g -O1 -Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
run() { # run <dir with the sources> <test|specs|oracle|fuzz|all>: 0 when the named checks pass, 2 when
        # something does not compile (a planted bug must be caught by a check, not by the compiler)
  local src=$1 what=$2 b; b=$(mktemp -d)
  python3 -I "$src/tests/seed_rdi.py" "$b/seed.rdi" || return 1
  "$src/tests/make_pdbs.sh" "$b/pdb" || return 1
  if [ "$what" = test ] || [ "$what" = all ]; then
    # each tests/test_*.c is its own program: ASan+UBSan with both compilers, then ThreadSanitizer
    for t in "$src"/tests/test_*.c; do
      for build in "gcc $SAN" "clang $SAN" "clang -fsanitize=thread"; do
        $build $FLAGS "$src"/dbg/*.c "$t" -o "$b/test" || return 2
        "$b/test" "$b/seed.rdi" >"$b/out" 2>&1 || { tail -5 "$b/out"; return 1; }
      done
    done
  fi
  if [ "$what" = specs ] || [ "$what" = all ]; then "$src/specs/check.sh" || return 1; fi
  if [ "$what" = oracle ] || [ "$what" = all ]; then
    gcc $FLAGS $SAN "$src"/dbg/*.c "$src"/tests/rdiq.c -o "$b/rdiq" || return 2
    for f in "$src"/rdi/*.rdi; do python3 -I "$src/tests/oracle_rdi.py" "$b/rdiq" "$f" "$f.dump.txt.gz" || return 1; done
    gcc $FLAGS $SAN "$src"/dbg/*.c "$src"/tests/msfq.c -o "$b/msfq" || return 2
    "$src/tests/oracle_msf.sh" "$b/msfq" "$b"/pdb/*.pdb $(ls "$src"/rdi/*.pdb 2>/dev/null) || return 1
    gcc $FLAGS $SAN "$src"/dbg/*.c "$src"/tests/pdbq.c -o "$b/pdbq" || return 2
    python3 -I "$src/tests/oracle_pdb.py" "$b/pdbq" "$b"/pdb/*.pdb $(ls "$src"/rdi/*.pdb 2>/dev/null) || return 1
  fi
  if [ "$what" = fuzz ] || [ "$what" = all ]; then
    for f in "$src"/tests/fuzz_*.c; do
      clang $FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all "$src"/dbg/*.c "$f" -o "$b/fuzz" || return 2
      mkdir -p "$b/corpus" && cp "$b/seed.rdi" "$b"/pdb/steps-*.pdb "$b/corpus/"
      (cd "$b" && ./fuzz corpus -max_total_time="${FUZZ_SECONDS:-20}" -max_len=131072 >fuzz.log 2>&1) || { grep -m2 "ERROR\|SUMMARY" "$b/fuzz.log"; return 1; }
    done
  fi
  rm -rf "$b"
}
if [ "${1:-}" != plants ]; then run . all && echo "check ok"; exit; fi
# a plant only shows something if the unplanted tree passes
run . all >/dev/null 2>&1 || { echo "plants: the check fails without any plant"; exit 1; }
missed=0; total=0
while IFS='|' read -r what file find repl; do
  case "$what" in ''|'#'*) continue;; esac
  what=$(echo $what); file=$(echo $file); find=${find# }; find=${find% }; repl=${repl# }
  t=$(mktemp -d); cp -r dbg tests specs "$t/"; [ -d rdi ] && cp -r rdi "$t/"
  FIND="$find" REPL="$repl" python3 -I -c 'import os,sys;p=sys.argv[1];s=open(p).read();f,r=(os.environ[k].replace("\\n","\n").replace("\\p","|") for k in ("FIND","REPL"));assert s.count(f)==1,("plant text not found once: "+f);open(p,"w").write(s.replace(f,r))' "$t/$file" || { echo "BAD PLANT: $find"; missed=$((missed+1)); continue; }
  total=$((total+1))
  run "$t" "$what" >/dev/null 2>&1
  case $? in 0) echo "NOT CAUGHT ($what): $find"; missed=$((missed+1));;
             2) echo "BAD PLANT, does not compile: $find"; missed=$((missed+1));; esac
  rm -rf "$t"
done < tests/plants.txt
echo "plants: $((total-missed)) of $total caught"; [ $missed = 0 ]
