#!/usr/bin/env bash
# A check never seen to fail proves nothing (CLAUDE.md).  For each check this
# plants one bug in a scratch copy of the repo and runs that check, which must
# fail.  Prints one line per plant; exits 1 if any check passed with its bug.
#
# The plants are the lines of tests/plants/*.txt, one file per package
# (# starts a comment).  A plant is: name ~ make target (with arguments) ~
# shell command run first in the copy ~ file ~ text ~ replacement (¶ for a
# newline).  SHARD=k/n runs only every n-th plant from the k-th (k from 0),
# so CI can spread them over n runners.
cd "$(dirname "$0")/.."
shard=${SHARD:-0/1}
# a shard that selects no plant would pass having checked nothing
[[ $shard =~ ^([0-9]+)/([1-9][0-9]*)$ ]] && (( BASH_REMATCH[1] < BASH_REMATCH[2] )) || { echo "SHARD=$shard is not k/n with k < n"; exit 1; }
rc=0
while IFS='~' read -r name target prep file old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d)
	cp -r Makefile include ref src tests tools design "$tmp"/
	if ! python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	then echo "BROKEN PLANT  $name"; rc=1; rm -rf "$tmp"; continue; fi
	# make -s: an echoed recipe line that names FAIL or error is not a catch
	if (cd "$tmp" && eval "$prep" && eval make -s $target) </dev/null >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name   (make $target passed)"; rc=1
	else
		# a check that fails for another reason (a missing tool, a make error)
		# has not caught the plant: require the failure the check reports
		# (a C compile or link error catches only for the compilers check:
		# elsewhere it means the plant broke the build)
		cc='^$'
		case $target in compilers*) ;; *) cc='\.[ch]:[0-9]+(:[0-9]+)?: (fatal )?(error|warning):|^cc1|undefined reference|ld returned';; esac
		# "N differ" or "N of M ... differ" with N > 0, not "0 of 2000 differ"
		why=$(grep -P 'CHECK\(|(?<!of )\b[1-9][0-9]* differ|\b[1-9][0-9]* of [0-9]+ [^.;]*?differ|WRONG|error|ERROR|Proved goals|sorry|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make|No rule' | grep -v -E "$cc" | head -1)
		if [ -n "$why" ]; then echo "caught        $name   (make $target): $why"
		else echo "FAILED OTHERWISE  $name   (make $target): $(tail -1 "$tmp.log")"; rc=1; fi
	fi
	rm -rf "$tmp" "$tmp.log"
done < <(cat tests/plants/*.txt | grep -v -e "^#" -e "^$" | awk -v k="${shard%/*}" -v n="${shard#*/}" '(NR - 1) % n == k')
exit $rc
