#!/usr/bin/env bash
# mkrev.sh ID TASK: a review repo = the task's base (no skills) + the attempt's change (without .claude)
id=$1; t=$2; SP=$SCRATCH
src=/home/claude/trial/runs/$id; d=/home/claude/trial/rev/$id
rm -rf $d; mkdir -p /home/claude/trial/rev; cp -r $SP/base/$t $d
cd $d && git init -q -b main && git add -A && git -c user.name=trial -c user.email=trial@localhost commit -q -m base
b=$(git -C $src rev-list --max-parents=0 HEAD)
git -C $src diff --binary $b HEAD -- . ':!.claude' > $SP/$id.diff
git apply --index $SP/$id.diff && git -c user.name=author -c user.email=author@localhost commit -q -m "The change under review"
ln -sfn /home/claude/Homeros/tools-env tools-env
git diff --stat HEAD~1 HEAD | tail -1
