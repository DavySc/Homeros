# Finding 2: an event on the work tree's own directory (touch .) makes
# git_counts read past the end of the path: what it then tests is whatever
# the previous event left in the buffer.
# usage (repo root): sh reviews/pr-70-repro/f2.sh DRIVE REPO   (REPO is made fresh; . reviews/pr-29-repro/env.sh first)
d=$1; r=$2
rm -rf $r $r.remote.git; sh src/git/mkrepo.sh $r >/dev/null 2>&1
( cd $r && echo '*.o' > .gitignore && git add .gitignore && git commit -qm ign && touch x.o )
echo "touch . alone:"
printf 'o\ns\np\n$ touch .\nl 100\nl 100\ns\np\n' | $d $r | grep -o 'ver [0-9]*'
echo "touch x.o (ignored), then touch . :"
printf 'o\ns\np\n$ touch x.o; touch .\nl 100\nl 100\ns\np\n' | $d $r | grep -o 'ver [0-9]*'
