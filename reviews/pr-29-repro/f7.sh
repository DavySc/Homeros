sh src/git/mkrepo.sh /tmp/rv/r1
printf 'z 1\no\ns\n$ ps -o stat=,args= -C sleep\nk cc\ns\nf\n$ sleep 0.3; ps -o stat=,args= -C sleep; true\n' | reviews/pr-29-repro/drive /tmp/rv/r1
