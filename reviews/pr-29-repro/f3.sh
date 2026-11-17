sh src/git/mkrepo.sh /tmp/rv/r1
printf 'o\ns\nk ll\n$ sleep 0.3\nk lb\ns\nl 200\nl 200\nl 200\np\n' | reviews/pr-29-repro/drive /tmp/rv/r1
