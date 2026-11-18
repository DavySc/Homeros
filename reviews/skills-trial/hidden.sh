#!/usr/bin/env bash
# hidden.sh ID TASK: PR 12's own checks against one attempt, in a scratch copy.
id=$1; t=$2; SP=$SCRATCH
R=/home/claude/Homeros; src=/home/claude/trial/runs/$id; w=$SP/hidden/$id
rm -rf $w; mkdir -p $SP/hidden; git -C $src archive HEAD | (mkdir -p $w && tar -x -C $w); ln -sfn $R/tools-env $w/tools-env
export PATH=$R/tools-env/bin:$PATH; cd $w; mkdir -p build
pr() { git -C $R show afa82ea:$1 > $1; }
log=$SP/hidden/$id.log; : > $log
run() { echo "== $*" >> $log; if eval "$@" >> $log 2>&1; then echo "PASS  $*"; else echo "FAIL  $*"; fi; }
run make compilers
case $t in
T1|T2) cp tests/fuzz_uni.c build/own_fuzz_uni.c; cp tests/test_uni_nvim.c build/own_test_uni_nvim.c
       pr tests/test_uni_nvim.c; pr tests/fuzz_uni.c
       run make test TESTS=uni_nvim; run make fuzz FUZZERS=uni FUZZ_SECONDS=60; run make frama
       git -C $src archive HEAD tests | tar -x -C $w ;;
T3) run make frama; run make test TESTS='"uni uni_nvim"' ;;
T4) run make test TESTS=uni_nvim; run make frama; wc -c src/uni/tab.h | tee -a $log ;;
T5) cp src/uni/uni.c build/uni.c.orig
    for k in 1 2 3; do cp build/uni.c.orig src/uni/uni.c
      case $k in
      1) python3 -c "import sys;p='src/uni/uni.c';s=open(p).read();o='uni_t2[uni_t1[c / UNI_B]][c % UNI_B] : 0;';assert s.count(o)==1;open(p,'w').write(s.replace(o,'uni_t2[uni_t1[c / UNI_B]][(c + 1) % UNI_B] : 0;'))";;
      2) python3 -c "import sys;p='src/uni/uni.c';s=open(p).read();o='!(p == 0x644 &&';assert s.count(o)==1;open(p,'w').write(s.replace(o,'!(p == 0x645 &&'))";;
      3) python3 -c "import sys;p='src/uni/uni.c';s=open(p).read();o='ic = ic == 1 && it == 3 ? 1 : it;';assert s.count(o)==1;open(p,'w').write(s.replace(o,'ic = it;'))";;
      esac
      echo "== PR12 plant $k" >> $log
      if make test TESTS=uni_nvim >> $log 2>&1; then echo "PR12 plant $k NOT CAUGHT"; else echo "PR12 plant $k caught"; fi
    done; cp build/uni.c.orig src/uni/uni.c ;;
esac
