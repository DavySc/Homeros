#!/usr/bin/env bash
# A check never seen to fail proves nothing (CLAUDE.md).  For each check this
# plants one bug in a scratch copy of the repo and runs that check, which must
# fail.  Prints one line per plant; exits 1 if any check passed with its bug.
#
# The plants are the lines of tests/plants/*.txt, one file per package
# (# starts a comment).  A plant is: name ~ make target (with arguments) ~
# shell command run first in the copy ~ file ~ text ~ replacement (¶ for a
# newline).
cd "$(dirname "$0")/.."
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
	if (cd "$tmp" && eval "$prep" && eval make $target) >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name   (make $target passed)"; rc=1
	else
		# a check that fails for another reason (a missing tool, a make error)
		# has not caught the plant: require the failure the check reports
		why=$(grep -E 'CHECK\(|[1-9][0-9]* differ|WRONG|error|ERROR|Proved goals|sorry|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make|No rule' | head -1)
		if [ -n "$why" ]; then echo "caught        $name   (make $target): $why"
		else echo "FAILED OTHERWISE  $name   (make $target): $(tail -1 "$tmp.log")"; rc=1; fi
	fi
	rm -rf "$tmp" "$tmp.log"
done < <(cat tests/plants/*.txt | grep -v -e "^#" -e "^$")
exit $rc
