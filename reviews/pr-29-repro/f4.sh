sh src/git/mkrepo.sh /tmp/rv/r1
printf 'z 31\no\ns\np\n' | reviews/pr-29-repro/drive /tmp/rv/r1; pkill sleep
