#!/bin/bash
# planted.sh's loop with the built objects copied in; $1 plant file, $2 shard k/n, log to $3
export PATH=/home/claude/tools-env/bin:$PATH XDG_STATE_HOME=/home/claude/state
cd /home/claude/r147
shard=$2
while IFS='~' read -r name target prep file old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d /home/claude/plants/t.XXXXXX)
	cp -r Makefile assets include ref src tests tools design "$tmp"/
	mkdir "$tmp/build"; (cd build && tar cf - --exclude=san --exclude=gcc --exclude=clang --exclude=win --exclude=fuzz --exclude=state .) | (cd "$tmp/build" && tar xf -)
	if ! python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	then echo "BROKEN PLANT  $name"; rm -rf "$tmp"; continue; fi
	if (cd "$tmp" && eval "$prep" && eval make -s $target) </dev/null >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name"
	else
		cc='(\.[ch]|<stdin>):[0-9]+(:[0-9]+)?: (fatal )?(error|warning):|^cc1|undefined reference|ld returned|^[0-9]+ errors? generated\.$'
		why=$(grep -P 'CHECK\(|(?<!of )\b[1-9][0-9]* differ|\b[1-9][0-9]* of [0-9]+ [^.;]*?differ|WRONG|error|ERROR|Proved goals|sorry|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make|No rule|got error' | grep -v -E "$cc" | head -1)
		if [ -n "$why" ]; then echo "caught        $name: $why"
		else echo "FAILED OTHERWISE  $name: $(tail -1 "$tmp.log")"; fi
	fi
	rm -rf "$tmp" "$tmp.log"
done < <(grep -v -e "^#" -e "^$" "$1" | awk -v k="${shard%/*}" -v n="${shard#*/}" '(NR - 1) % n == k') >> "$3" 2>&1
echo "DONE shard $shard" >> "$3"
