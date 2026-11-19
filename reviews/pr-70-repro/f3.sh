# Finding 3: the work tree diff (d w) of a staged rename.  magit runs
# git diff HEAD, which shows a rename (diff.renames is on by default);
# git diff-index -p HEAD shows a deleted and a new file.
# usage (repo root): sh reviews/pr-70-repro/f3.sh DRIVE REPO   (REPO is made fresh; . reviews/pr-29-repro/env.sh first)
d=$1; r=$2
rm -rf $r $r.remote.git; sh src/git/mkrepo.sh $r >/dev/null 2>&1
( cd $r && git reset -q --hard && git mv a.txt z.txt )
printf 'o\ns\nk dw\ns\np\n' | $d $r | head -12
