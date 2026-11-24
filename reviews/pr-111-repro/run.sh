#!/bin/bash
# Review of PR 111: builds and runs the repro driver.  From the repo root:
#   reviews/pr-111-repro/run.sh <scenario>     (sev flood cursor small deep strgoals)
# Needs the objects `make test` builds (build/syntax etc.).
set -e
D=$(cd "$(dirname "$0")" && pwd)
cat > build/rv-lt.mk <<'MK'
NOLSP = $(filter-out src/lsp/lsp.c,$(SRCS))
build/rvobj/%.o: %.c | $(OBJS)
	@mkdir -p $(@D)
	@gcc $(WARN) $(CPPFLAGS) $(SAN) -c -o $@ $<
lt: $(NOLSP:%.c=build/rvobj/%.o) | build/san
	@gcc $(WARN) $(CPPFLAGS) $(SAN) -o $(O) $(T) src/lsp/lsp.c $(NOLSP:%.c=build/rvobj/%.o) $(OBJS) $(LIBS)
MK
make -s -j8 -f Makefile -f build/rv-lt.mk lt T=$D/drive.c O=build/san/drive
rm -rf build/rvstate
XDG_STATE_HOME=$PWD/build/rvstate ASAN_OPTIONS=detect_leaks=0 ./build/san/drive "$1" "$D"
