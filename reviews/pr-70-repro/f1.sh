# Finding 1: discarding staged changes while another file is unmerged.
# usage (repo root): sh reviews/pr-70-repro/f1.sh DRIVE REPO   (REPO is made fresh; . reviews/pr-29-repro/env.sh first)
set -e
d=$1; r=$2
rm -rf $r $r.remote.git; sh src/git/mkrepo.sh $r >/dev/null 2>&1
( cd $r && git reset -q --hard && rm -f c.txt && git checkout -q -b side && sed -i s/one/ONE/ a.txt && git commit -qam side \
  && git checkout -q main && sed -i s/one/uno/ a.txt && git commit -qam m2 && { git merge side >/dev/null 2>&1 || true; } \
  && echo delta >> b.txt && git add b.txt )
printf 'o\ns\nc 11\nk x\nk y\ns\np\n' | $d $r | grep '^\[msg\]'
git -C $r status --short
