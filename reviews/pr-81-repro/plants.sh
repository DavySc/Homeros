#!/bin/bash
# Review of PR 81: the extra plants this review tried against git_attach
# (run from the repo root; each restores src/git/keys.c).  At b49bf24:
# scnr NOT CAUGHT (fixed in 3dae6f8, now caught at test_git_signs.c:316),
# nosched caught, nosweep NOT CAUGHT (pre-existing, memory only).
plant() {
	cp src/git/keys.c /tmp/keys.c.orig
	python3 - "$2" "$3" <<'P'
import sys; p='src/git/keys.c'; s=open(p).read(); o,n=sys.argv[1],sys.argv[2]
assert s.count(o)==1, o; open(p,'w').write(s.replace(o,n))
P
	if make test TESTS=git_signs >/tmp/$1.log 2>&1; then echo "$1: NOT CAUGHT"; else echo "$1: caught"; fi
	cp /tmp/keys.c.orig src/git/keys.c
}
plant scnr $'\tscnr = 0;\n' ''
plant nosched $'strdup(abs + tl + (abs[tl] == \'/\'));\n\tgit_schedule(o->r);' $'strdup(abs + tl + (abs[tl] == \'/\'));'
plant nosweep $'\t\tif (!git_buf(ofs[i].nr)) { free(ofs[i].rel); ofs[i--] = ofs[--nofs]; }   /* deleted */' $'\t\t;'
