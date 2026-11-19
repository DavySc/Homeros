#!/bin/bash
# The prototype's check. `./check.sh` builds with gcc and clang (-Werror), runs the tests under
# ASan+UBSan and ThreadSanitizer, checks the TLA+ models (needs TLA_JAR or tlc), compares the reader with RAD's dump of every file in rdi/, and runs each fuzzer for FUZZ_SECONDS (default 20). `./check.sh plants` then proves each check
# can fail: every line of tests/plants.txt (or of the file PLANTS names, to rerun a few) must make its check fail.
set -u
cd "$(dirname "$0")"
# a small queue, so the stress test runs with both queues full most of the time
FLAGS="-std=c17 -D_GNU_SOURCE -pthread -DQUEUE_BYTES=256 -g -O1 -Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
run() { # run <dir with the sources> <list|test|specs|oracle|x64|win|windows|fuzz|all>: 0 when the named checks pass, 2 when
        # something does not compile (a planted bug must be caught by a check, not by the compiler), 3 when the
        # test files cannot be made (nor by a full disk)
  local src=$1 what=$2 b t; b=$(mktemp -d)
  case $what in test|oracle|fuzz|all)
    python3 -I "$src/tests/seed_rdi.py" "$b/seed.rdi" || return 3
    "$src/tests/make_pdbs.sh" "$b/pdb" || return 3;;
  esac
  if [ "$what" = list ] || [ "$what" = all ]; then
    # every plant still has its place: its text occurs once in the file it names (not asked of a planted tree)
    python3 -I -c 'import sys
for l in open(sys.argv[1] + "/tests/plants.txt"):
    if l.strip() and l[0] != "#":
        what, file, find, repl = l.rstrip("\n").split("|")
        find = find[1:-1] if find.endswith(" ") else find[1:]
        if open(sys.argv[1] + "/" + file.strip()).read().count(find.replace("\\n", "\n").replace("\\p", "|")) != 1: sys.exit("plants.txt: not found once: " + find)' "$src" || return 1
  fi
  if [ "$what" = test ] || [ "$what" = all ]; then
    # each tests/test_*.c is its own program: ASan+UBSan with both compilers, then ThreadSanitizer
    for t in "$src"/tests/test_*.c; do
      for build in "gcc $SAN" "clang $SAN" "clang -fsanitize=thread"; do
        # a test that includes a module's .c file, to reach its static functions, is not linked with it again
        $build $FLAGS $(ls "$src"/dbg/*.c | grep -vFx "$src/dbg/$(sed -n 's|^#include "\.\./dbg/\(.*\.c\)"|\1|p' "$t")") "$t" -o "$b/test" || return 2
        timeout 300 "$b/test" "$b/seed.rdi" >"$b/out" 2>&1 || { tail -5 "$b/out"; return 1; }
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
    gcc $FLAGS $SAN "$src"/dbg/*.c "$src"/cmd/pdb2rdi.c -o "$b/pdb2rdi" || return 2
    python3 -I "$src/tests/oracle_conv.py" "$b/pdb2rdi" "$b/rdiq" "$b"/pdb/*.pdb $(ls "$src"/rdi/*.pdb 2>/dev/null) || return 1
    # two files llvm-pdbutil cannot read: a pointer to a later record is a pointer to no type, and type ids that do not start at 0x1000 are no types (what is left is "none", the `int` of the locals and the file's two typedefs, names on no type)
    "$b/pdb2rdi" "$b/pdb/bad/later.pdb" "$b/odd.rdi" && [ "$(echo T 8 | "$b/rdiq" "$b/odd.rdi")" = "1001.0[0]:8 none" ] || return 1
    "$b/pdb2rdi" "$b/pdb/bad/firstid.pdb" "$b/odd.rdi" && [ "$(printf 'N 19 T 1 T 2 T 3 T 4' | "$b/rdiq" "$b/odd.rdi" | tr '\n' ' ')" = "5 S32:4 2004 int:4 on S32:4 2004 Pair:0 2004 Op:0 " ] || return 1
  fi
  if [ "$what" = x64 ] || [ "$what" = all ]; then
    # the instruction decoder against llvm-objdump: the test programs, every opcode once, and random bytes
    gcc $FLAGS $SAN "$src"/dbg/x64.c "$src"/tests/x64q.c -o "$b/x64q" || return 2
    python3 -I "$src/tests/oracle_x64.py" "$b/x64q" rdi/*.exe sweep random:1:100 || return 1
  fi
  if [ "$what" = win ] || [ "$what" = windows ] || [ "$what" = all ]; then
    # process control on Windows, here under Wine (WINE names it). On Windows itself (.github/workflows/win-check.yml): WINE=env WINCC=gcc,
    # and there also as `windows`, the step of the plants that Wine cannot catch (its kernel differs: knowledge base, lesson L43)
    command -v "${WINE:-wine}" >/dev/null || { echo "check: no Wine (set WINE): the process-control stress test cannot run"; return 3; }
    for f in tests/spin.c "dbg/win/target.c tests/stress_target.c"; do
      ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror $(printf "$src/%s " $f) -o "$b/$(basename "${f##* }" .c).exe" || return 2
    done
    for mode in "" attach; do
      (cd "$b" && WINEDEBUG=-all timeout 600 "${WINE:-wine}" ./stress_target.exe spin.exe "${STRESS_ROUNDS:-2000}" "${SEED:-1}" $mode 2>/dev/null | grep "^stress_target" | tee stress.out; grep -q "rounds:" stress.out) || return 1
    done
  fi
  if [ "$what" = fuzz ] || [ "$what" = all ]; then
    for f in "$src"/tests/fuzz_*.c; do
      clang $FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all "$src"/dbg/*.c "$f" -o "$b/fuzz" || return 2
      mkdir -p "$b/corpus" && cp "$b/seed.rdi" "$b"/pdb/steps-*.pdb "$b"/pdb/gap-O2.pdb "$b"/pdb/kinds.pdb "$b"/pdb/frames-O0.pdb "$b"/pdb/bad/* "$b/corpus/"
      (cd "$b" && ./fuzz corpus -max_total_time="${FUZZ_SECONDS:-20}" -max_len=131072 -timeout=20 >fuzz.log 2>&1) || { grep -m2 "ERROR\|SUMMARY" "$b/fuzz.log"; return 1; }
    done
  fi
  rm -rf "$b"
}
if [ "${1:-}" != plants ]; then run . "${1:-all}" && echo "check ok"; exit; fi   # or one step by name
# `./check.sh plants [step]`: every plant, or those of one step. A plant only shows something if the unplanted tree passes.
run . "${2:-all}" >/dev/null 2>&1 || { echo "plants: the check fails without any plant"; exit 1; }
missed=0; total=0
while IFS='|' read -r what file find repl; do
  what=$(echo $what); file=$(echo $file); find=${find# }; find=${find% }; repl=${repl# }
  case "$what" in ''|'#'*) continue;; "${2:-$what}") ;; *) continue;; esac
  if [ "$what" = windows ] && [ "${WINE:-}" != env ]; then continue; fi   # only real Windows catches these
  t=$(mktemp -d); cp -r dbg tests specs cmd subjects "$t/" || { echo "plants: cannot copy the tree"; exit 1; }
  if [ "$what" = oracle ]; then ln -s "$PWD/rdi" "$t/rdi"; fi   # 260 MB that only the oracle reads and no plant changes
  FIND="$find" REPL="$repl" python3 -I -c 'import os,sys;p=sys.argv[1];s=open(p).read();f,r=(os.environ[k].replace("\\n","\n").replace("\\p","|") for k in ("FIND","REPL"));assert s.count(f)==1,("plant text not found once: "+f);open(p,"w").write(s.replace(f,r))' "$t/$file" || { echo "BAD PLANT: $find"; missed=$((missed+1)); continue; }
  total=$((total+1))
  TMPDIR=$t run "$t" "$what" >/dev/null 2>&1   # a failed run leaves its build directory: keep it inside $t
  case $? in 0) echo "NOT CAUGHT ($what): $find"; missed=$((missed+1));;
             2) echo "BAD PLANT, does not compile: $find"; missed=$((missed+1));;
             3) echo "plants: cannot make the test files"; exit 1;; esac
  rm -rf "$t"
done < "${PLANTS:-tests/plants.txt}"
echo "plants: $((total-missed)) of $total caught"; [ $missed = 0 ]
