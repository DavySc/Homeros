cd /tmp/claude-0/r109p
run() { # name file sed-expr
  git checkout -q -- src; sed -i "$3" "$2"; git diff --stat | tail -1
  make test TESTS=infoview >/tmp/claude-0/own-$1.build 2>&1
  for i in 1 2 3 4 5; do ./build/san/test_infoview >/tmp/claude-0/own-$1.$i 2>&1 && echo "$1 $i MISSED" || echo "$1 $i caught: $(grep -m1 failed /tmp/claude-0/own-$1.$i)"; done
}
run A src/infoview/infoview.c 's/(void \*)++children/(void *)children/'
run B src/infoview/serve/serve.c 's/^\tunlink(redirect);/\t(void)0;/'
run C src/infoview/infoview.c 's/if (proc >= 0) proc_close(proc);/;/'
git checkout -q -- src
