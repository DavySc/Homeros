#!/bin/bash
# The prototype's check. `./check.sh` builds with gcc and clang (-Werror), runs the tests under
# ASan+UBSan and ThreadSanitizer, checks the TLA+ models (needs TLA_JAR or tlc), compares the reader with RAD's dump of every file in rdi/, and runs each fuzzer for FUZZ_SECONDS (default 20). `./check.sh plants` then proves each check
# can fail: every line of tests/plants.txt (or of the file PLANTS names, to rerun a few) must make its check fail.
set -u
cd "$(dirname "$0")"
# a small queue, so the stress test runs with both queues full most of the time
FLAGS="-std=c17 -D_GNU_SOURCE -pthread -DQUEUE_BYTES=256 -g -O1 -Wall -Wextra -Werror"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
run() { # run <dir with the sources> <list|test|specs|oracle|x64|win|windows|engine|eval|front|fuzz|all>: 0 when the named checks pass, 2 when
        # something does not compile (a planted bug must be caught by a check, not by the compiler), 3 when the
        # test files cannot be made (nor by a full disk)
  local src=$1 what=$2 b t lib; b=$(mktemp -d)
  lib=$(ls "$src"/dbg/*.c | grep -v '/engine\.c$\|/session\.c$\|/eval\.c$\|/view\.c$\|/ask\.c$')   # the engine, its thread and the evaluator need a target: linked in the engine and eval steps; here compiled, and the thread run on a made-up target (tests/test_session.c)
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
    for build in gcc clang; do for t in engine session eval view ask; do $build $FLAGS -c "$src"/dbg/$t.c -o "$b/$t.o" || return 2; done; done
    # only the engine and its thread call the target or the engine: what the editor links beside them shares nothing with a debugged program
    for t in $lib; do gcc $FLAGS -c "$t" -o "$b/o.o" || return 2; if nm -u "$b/o.o" | grep ' \(target\|engine\)_'; then return 1; fi; done
    # each tests/test_*.c is its own program: ASan+UBSan with both compilers, then ThreadSanitizer
    for t in "$src"/tests/${TESTS:-test_*}.c; do   # TESTS=test_eval: that one alone (for the plants of one module)
      for build in "gcc $SAN" "clang $SAN" "clang -fsanitize=thread"; do
        # a test that includes a module's .c file, to reach its static functions, is not linked with it again
        $build $FLAGS $(grep -vFx "$src/dbg/$(sed -n 's|^#include "\.\./dbg/\(.*\.c\)"|\1|p' "$t")" <<<"$lib") "$t" -o "$b/test" || return 2
        timeout 300 "$b/test" "$b/seed.rdi" >"$b/out" 2>&1 || { tail -5 "$b/out"; return 1; }
      done
    done
  fi
  if [ "$what" = specs ] || [ "$what" = all ]; then "$src/specs/check.sh" || return 1; fi
  if [ "$what" = oracle ] || [ "$what" = all ]; then
    gcc $FLAGS $SAN $lib "$src"/tests/rdiq.c -o "$b/rdiq" || return 2
    for f in "$src"/rdi/*.rdi; do python3 -I "$src/tests/oracle_rdi.py" "$b/rdiq" "$f" "$f.dump.txt.gz" || return 1; done
    gcc $FLAGS $SAN $lib "$src"/tests/msfq.c -o "$b/msfq" || return 2
    "$src/tests/oracle_msf.sh" "$b/msfq" "$b"/pdb/*.pdb $(ls "$src"/rdi/*.pdb 2>/dev/null) || return 1
    gcc $FLAGS $SAN $lib "$src"/tests/pdbq.c -o "$b/pdbq" || return 2
    python3 -I "$src/tests/oracle_pdb.py" "$b/pdbq" "$b"/pdb/*.pdb $(ls "$src"/rdi/*.pdb 2>/dev/null) || return 1
    gcc $FLAGS $SAN $lib "$src"/cmd/pdb2rdi.c -o "$b/pdb2rdi" || return 2
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
    for f in tests/spin.c "dbg/win/target.c tests/stress_target.c" "dbg/win/target.c dbg/unwind.c tests/stress_unwind.c"; do
      ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror $(printf "$src/%s " $f) -o "$b/$(basename "${f##* }" .c).exe" -ldbghelp || return 2
    done
    for mode in "" attach; do
      (cd "$b" && WINEDEBUG=-all timeout 600 "${WINE:-wine}" ./stress_target.exe spin.exe "${STRESS_ROUNDS:-2000}" "${SEED:-1}" $mode 2>/dev/null | grep "^stress_target" | tee stress.out; grep -q "rounds:" stress.out) || return 1
    done
    # the unwinder against the system's stack walker, single-stepping three recorded programs: in the loader, from the entry point, from main
    for p in steps deep frames_o2; do
      (cd "$b" && WINEDEBUG=-all timeout 900 "${WINE:-wine}" ./stress_unwind.exe "$OLDPWD/rdi/$p.exe" "${UNWIND_STEPS:-2000}" "$(sed -n '1s/.* +0x\([0-9a-f]*\) .*/\1/p' "$OLDPWD/traces/$p.over.txt")" 2>/dev/null | grep "^stress_unwind" | tee unwind.out; grep -q "frames agree" unwind.out) || return 1
    done
  fi
  if [ "$what" = engine ] || [ "$what" = all ]; then
    local win="$(grep -v '/queue\.c$' <<<"$lib") $src/dbg/engine.c $src/dbg/win/target.c" at
    # the engine's stepping against RAD's recorded traces (oracle/rad-trace.ps1): every step of each must end where RAD's did
    ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror $win "$src"/tests/trace.c -o rdi/trace_$$.exe || return 2
    WINEDEBUG=-all python3 -I "$src/tests/oracle_trace.py" ./trace_$$.exe traces rdi $([ "${WINE:-wine}" = env ] || echo "${WINE:-wine}") 2>/dev/null | tail -n 12; t=${PIPESTATUS[0]}; rm -f rdi/trace_$$.exe; [ "$t" = 0 ] || return 1
    # and where the traces do not reach: threads in the same code, breakpoints, exceptions
    ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror "$src"/tests/scene.c -o "$b/scene.exe" || return 2
    ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror $win "$src"/tests/stress_engine.c -o "$b/stress_engine.exe" || return 2
    at=$(for s in caller shared handled go; do nm "$b/scene.exe" | sed -n "s/^\([0-9a-f]*\) [TtBbCcDd] $s\$/\1/p"; done)
    (cd "$b" && WINEDEBUG=-all timeout 600 "${WINE:-wine}" ./stress_engine.exe scene.exe "${ENGINE_ROUNDS:-300}" $at 2>/dev/null | grep "^stress_engine" | tee engine.out; grep -q "as they should be" engine.out) || return 1
  fi
  if [ "$what" = eval ] || [ "$what" = all ]; then
    # names, where their variables are and expressions of them (dbg/eval.c), on a program that says what each is itself: built here with clang, plain and optimised
    # (each with the number of texts the program shows, the least that must be right, the least variables in a register, and the least rows of the tables of variables: dbg/view.c), and as MSVC built it on the recorder
    local link="lld-link -nologo -debug -nodefaultlib -entry:main -subsystem:console -dynamicbase:no -stack:1048576,1048576" o   # with dashes: the shell on Windows takes /nologo for a path
    ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror $(grep -v '/queue\.c$' <<<"$lib") "$src"/dbg/engine.c "$src"/dbg/eval.c "$src"/dbg/ask.c "$src"/dbg/win/target.c "$src"/tests/zoo_check.c -o "$b/zoo_check.exe" || return 2
    echo 'int _fltused;' > "$b/flt.c" && clang --target=x86_64-pc-windows-msvc -ffreestanding -c "$b/flt.c" -o "$b/flt.obj" || return 3
    for o in O0:400:400:0:5000:4 O2:400:250:15:3000:0; do
      clang --target=x86_64-pc-windows-msvc -g -gcodeview -${o%%:*} -funwind-tables -ffreestanding -mno-stack-arg-probe -c "$src/subjects/zoo.c" -o "$b/zoo.obj" && $link "$b/zoo.obj" "$b/flt.obj" -out:"$b/zoo-${o%%:*}.exe" -pdb:"$b/zoo-${o%%:*}.pdb" || return 3
      for seed in 1 2 3; do   # of the rows opened and closed in the tables
        (cd "$b" && WINEDEBUG=-all timeout 300 "${WINE:-wine}" ./zoo_check.exe zoo-${o%%:*}.exe $(tr : ' ' <<<"${o#*:}") $seed 2>/dev/null | grep "^zoo_check" | tee zoo.out; grep -q "what the program says" zoo.out) || return 1
      done
    done
  fi
  if [ "$what" = front ] || [ "$what" = all ]; then
    # the whole of it as a person meets it: the console debugger (cmd/hdbg.c: the engine thread, its queues, the asks) taken through tests/hdbg.txt on tests/tour.c
    # must write tests/hdbg.expect, but for the addresses, where the files are, and the frames outside the program
    ${WINCC:-x86_64-w64-mingw32-gcc} -std=c17 -O1 -g -Wall -Wextra -Werror -static -pthread $lib "$src"/dbg/engine.c "$src"/dbg/session.c "$src"/dbg/eval.c "$src"/dbg/view.c "$src"/dbg/ask.c "$src"/dbg/win/target.c "$src"/cmd/hdbg.c -o "$b/hdbg.exe" || return 2
    echo 'int _fltused;' > "$b/flt.c" && clang --target=x86_64-pc-windows-msvc -ffreestanding -c "$b/flt.c" -o "$b/flt.obj" || return 3
    clang --target=x86_64-pc-windows-msvc -g -gcodeview -O0 -funwind-tables -ffreestanding -mno-stack-arg-probe -c "$src/tests/tour.c" -o "$b/tour.obj" && lld-link -nologo -debug -nodefaultlib -entry:main -subsystem:console -dynamicbase:no "$b/tour.obj" "$b/flt.obj" -out:"$b/tour.exe" -pdb:"$b/tour.pdb" || return 3
    local here; here=$(cd "$src" && pwd)
    (cd "$b" && WINEDEBUG=-all timeout 120 "${WINE:-wine}" ./hdbg.exe tour.exe < "$here/tests/hdbg.txt" 2>/dev/null | tr -d '\r' | sed -E 's/0x[0-9a-f]+/0x_/g; s/ [^ ]*[\\/]([^\\/ ]*:[0-9]+)$/ \1/; /^#/{/tour\.c:/!d}' > front.out; diff "$here/tests/hdbg.expect" front.out | head -70; cmp -s "$here/tests/hdbg.expect" front.out) || return 1
    (cd "$b" && ! WINEDEBUG=-all timeout 120 "${WINE:-wine}" ./hdbg.exe nothing.exe < /dev/null >/dev/null 2>&1) || return 1
  fi
  if [ "$what" = fuzz ] || [ "$what" = all ]; then
    for f in "$src"/tests/fuzz_*.c; do
      clang $FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all $lib "$f" -o "$b/fuzz" || return 2
      mkdir -p "$b/corpus" && cp "$b/seed.rdi" "$b"/pdb/steps-*.pdb "$b"/pdb/gap-O2.pdb "$b"/pdb/kinds.pdb "$b"/pdb/frames-O0.pdb "$b"/pdb/bad/* "$b/corpus/"
      # for tests/fuzz_eval.c: expressions alone, and after debug information that has their names
      printf '%s' '*(char *)0x7000 + sizeof(int *) ? 1.5f : -~!3 % 2 << 1 >= 4 && 5 || (unsigned long long)6 / 7u' > "$b/corpus/text1" && printf '%s' "((short *)0x7010)[-2] == 'a' ^ 0x1fULL | sizeof 2.5e3 > 1 != .5" > "$b/corpus/text2" || return 3
      gcc $FLAGS $lib "$src"/cmd/pdb2rdi.c -o "$b/pdb2rdi" && "$b/pdb2rdi" "$b/pdb/kinds.pdb" "$b/kinds.rdi" || return 3
      for t in 'sizeof(Later) + ((Later *)0x7000)->next->x' '((Bits *)0x7010)->c * ((Box *)0x7020)->vp[1] - (Plain)3' '&((Either *)0x7000)->f == (float *)0x7000 ? (Colour)1 : 2.5' '(Later *)0x7000' '*(Box *)0x7020'; do
        cat "$b/kinds.rdi" > "$b/corpus/kinds-${#t}" && printf '%s' "$t" >> "$b/corpus/kinds-${#t}" || return 3
      done
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
