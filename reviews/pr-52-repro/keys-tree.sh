#!/usr/bin/env bash
# Review of PR 52: a tree with package 5's keys (PR 26, not on main) on top
# of a core, and the review's drivers built in it.
# usage: keys-tree.sh COMMIT DIR      (COMMIT: the PR head, or main for the base)
# then:  python3 reviews/pr-52-repro/marks.py DIR/build/san/marks_drv --grid
#        DIR/build/san/lists; DIR/build/san/twobuf
set -e
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../.."
# usage: keys-tree.sh COMMIT DIR [KEYS]   (KEYS: a commit of PR 26, its head by default)
git fetch -q origin pull/26/head
keys=${3:-$(git rev-parse FETCH_HEAD)}
git worktree add -q --detach "$2" "$1"
cd "$2"
# a PR 26 head from before the core left ref/ conflicts there: both sides'
# deletions stand (f9ff678 did; cc5d0cb merges cleanly)
if ! git -c user.name=review -c user.email=review@localhost merge -q --no-edit "$keys" >/dev/null 2>&1; then
	git rm -q --ignore-unmatch ref/core.c ref/keys.c
	git checkout -q --theirs tests/plants/keys.txt
	git add -A
	git -c user.name=review -c user.email=review@localhost commit -qm 'keys of PR 26 on this core'
fi
for d in marks_drv lists twobuf; do
	make -s -f Makefile -f "$here/drv.mk" DRV="$here/$d.c" build/san/$d
done
ls -l build/san/marks_drv build/san/lists build/san/twobuf
