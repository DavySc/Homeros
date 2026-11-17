#!/usr/bin/env bash
# A check never seen to fail proves nothing (CLAUDE.md).  For each check this
# plants one bug in a scratch copy of the repo and runs that check, which must
# fail.  Prints one line per plant; exits 1 if any check passed with its bug.
#
# A plant is: name ~ make target (with arguments) ~ shell command run first
# in the copy ~ file ~ text ~ replacement (¶ for a newline)
cd "$(dirname "$0")/.."
PLANTS=$(cat <<'EOF'
warning (gcc and clang -Werror)~compilers~~ref/core.c~int core_modified(const Editor *e) {~int core_modified(const Editor *e) { int unused;
text: line lookup off by one~test TESTS=text~~ref/text.c~for (size_t i = 0; i < pos && i < t->n; i++) k += t->b[i] == '\n';~for (size_t i = 0; i <= pos && i < t->n; i++) k += t->b[i] == '\n';
undo: a new edit keeps redo (Undo_keepredo)~test TESTS=undo~~ref/undo.c~		while (u->n > u->done) group_free(&u->g[--u->n]);~
undo: undo leaves the group open (Undo_noclose)~test TESTS=undo~~ref/undo.c~	u->open = 0;¶	if (!u->done) return 0;~	if (!u->done) return 0;
uni: 0xF8 read as a 4-byte lead~test TESTS=uni~~ref/uni.c~c < 0xF8 ? 4 : 0;~c < 0xFC ? 4 : 0;
file: no check before saving (Save_nocheck)~test TESTS=file~~ref/file.c~	if (!force && id->exists && !same(id, &now)) return SAVE_CHANGED;~
tty: every cell sent every frame~test TESTS=grid~~ref/tty.c~			if (grid_cell_eq(x, y)) continue;~
keys: j and k forget the wanted column (recorded traces)~test TESTS=~~ref/keys.c~	e->col = core_col_at(e, l, e->want, e->mode == MODE_INSERT);~	e->col = core_col_at(e, l, 0, e->mode == MODE_INSERT);
text: insert drops a byte (fuzz_text)~fuzz FUZZERS=text FUZZ_SECONDS=60~~ref/text.c~	memcpy(t->b + pos, s, n);¶	t->n += n;~	memcpy(t->b + pos, s, n);¶	t->n += n - (n > 3);
core: undo loses deleted text (fuzz_keys)~fuzz FUZZERS=keys FUZZ_SECONDS=60~~ref/core.c~undo_record(e->undo, pos, del, ndel, ins, nins, c, after)~undo_record(e->undo, pos, del, ndel ? ndel - 1 : 0, ins, nins, c, after)
keys: w does not stop on an empty line (live nvim)~conform CONFORM_N=2000 SEED=1~~ref/keys.c~		while (cls(e, p) == 0 && !empty_line(e, p))~		while (cls(e, p) == 0)
undo tree oracle: Ctrl-R to the first child~undo-diff UNDO_N=2000~~Makefile~python3 design~MUT=firstchild python3 design
lean: inv_inv states something false~lean~~design/proofs/Undo.lean~theorem Edit.inv_inv (e : Edit α) : e.inv.inv = e~theorem Edit.inv_inv (e : Edit α) : e.inv = e
lean: a proof left as sorry~lean~~design/proofs/Undo.lean~theorem Edit.inv_inv (e : Edit α) : e.inv.inv = e := by cases e <;> rfl~theorem Edit.inv_inv (e : Edit α) : e.inv.inv = e := by sorry
frama-c: the UTF-8 kernel returns 1 (weak contract trap)~frama~~ref/uni.c~		if (s[i + j] < 0x80 || s[i + j] >= 0xC0) return 1;¶	return k;~		if (s[i + j] < 0x80 || s[i + j] >= 0xC0) return 1;¶	return 1;
specs: save renames without fsync (Save2)~specs~find design/specs -name '*.cfg' ! -name Save2_fsync.cfg -delete~design/specs/Save2_fsync.cfg~Variant = "fsync"~Variant = "nofsync"
EOF
)
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
done <<<"$PLANTS"
exit $rc
