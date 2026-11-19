sh src/git/mkrepo.sh /tmp/rv/r3
(cd /tmp/rv/r3 && git stash -q && git tag v1 && rm -rf ../wt && git worktree add -q ../wt -b side && echo x >> ../wt/a.txt)
printf 'o\ns\np\n' | reviews/pr-29-repro/drive /tmp/rv/r3
printf 'o\ns\np\n' | reviews/pr-29-repro/drive /tmp/rv/wt
