# planted.sh's body for one plant file, P at a time (review runner); the
# copy gets build/syntax so nix is not asked again
cd /home/claude/Homeros
export PATH=$PWD/tools-env/bin:$PATH
export APPLY=$(dirname "$(readlink -f "$0")")/apply.py
one() {
	IFS='~' read -r name target prep file old new <<<"$1"
	tmp=$(mktemp -d)
	cp -r Makefile assets include ref src tests tools design "$tmp"/
	mkdir -p "$tmp/build"; cp -r build/syntax "$tmp/build/" 2>/dev/null
	if ! python3 "$APPLY" "$tmp/$file" "$old" "$new"
	then echo "BROKEN PLANT  $name"; rm -rf "$tmp"; return; fi
	if (cd "$tmp" && eval "$prep" && eval make -s $target) </dev/null >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name   (make $target passed)"
	else
		cc='\.[ch]:[0-9]+(:[0-9]+)?: (fatal )?(error|warning):|^cc1|undefined reference|ld returned|^[0-9]+ errors? generated\.$'
		why=$(grep -P 'CHECK\(|(?<!of )\b[1-9][0-9]* differ|\b[1-9][0-9]* of [0-9]+ [^.;]*?differ|WRONG|error|ERROR|Proved goals|sorry|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make|No rule|got error' | grep -v -E "$cc" | head -1)
		if [ -n "$why" ]; then echo "caught        $name   (make $target): $why"
		else echo "FAILED OTHERWISE  $name   (make $target): $(tail -1 "$tmp.log")"; cp "$tmp.log" "$(dirname "$APPLY")/fail-$$.log"; fi
	fi
	rm -rf "$tmp" "$tmp.log"
}
export -f one
grep -v -e "^#" -e "^$" "$1" | tr '\n' '\0' | xargs -0 -P "${P:-3}" -I{} bash -c 'one "$@"' _ {}
echo DONE
