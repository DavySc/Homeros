# Homeros.  `make` builds the editor; `make check` runs every check
# (CLAUDE.md: "Every merge runs the whole check"); `make planted` shows that
# each check fails on a planted bug.
#
# A module comes from src/<module>/ once its package exists there, else from
# the reference implementation in ref/.  Package 6 owns both grid and tty.
# Modules, tests, fuzzers and plants are found by name, so a package adds
# files in its own places and never edits this one.

SHELL = /bin/bash
# crash journals and in-place backups of test runs (file.h) stay out of $HOME,
# and one left by a crashed run never reaches the next: each target that
# opens files starts with none (override, and the rm names the path: a
# value from the command line or make -e never points it at a real one)
override export XDG_STATE_HOME := $(CURDIR)/build/state

BASE = text undo uni core keys grid tty loop file syntax theme tools git lsp infoview kitten
# src/win/ (package 9) is the Windows build's, cross-compiled on its own
MODULES = $(BASE) $(filter-out $(BASE) win,$(patsubst src/%/,%,$(wildcard src/*/)))
pick = $(if $(wildcard src/$(1)/*.c),$(wildcard src/$(1)/*.c),ref/$(1).c)
SRCS := $(foreach m,$(MODULES),$(call pick,$(m)))

# A package that needs a library or code outside our warnings (FreeType,
# Wayland, tree-sitter's runtime and grammars) says so in src/<module>/build.mk:
# it adds to CPPFLAGS and LIBS, and to OBJS objects built by its own rules
# (into build/<module>/).  The window backend (src/wl/) adds -DHOMEROS_WL.
CPPFLAGS =
LIBS =
OBJS =
.DEFAULT_GOAL := homeros
-include $(wildcard src/*/build.mk)

# the unit tests link everything (a test may include src/main.c, which
# names the window backend); the fuzzers and drivers everything but the
# terminal and the window (the tools and git spawn through the loop)
LIB_SRCS := $(filter-out $(call pick,tty) $(call pick,wl),$(SRCS))

CC ?= cc
CFLAGS ?= -O2
WARN = -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude
SAN = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
FUZZ_SECONDS ?= 30
CONFORM_N ?= 2000
UNDO_N ?= 5000

homeros: $(SRCS) $(OBJS) src/main.c include/*.h
	$(CC) $(WARN) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCS) src/main.c $(OBJS) $(LIBS)

build:
	@mkdir -p $@
build/%:
	@mkdir -p $@

# both compilers, warnings as errors
compilers: $(OBJS) | build/gcc build/clang
	gcc $(WARN) $(CPPFLAGS) -O2 -o build/gcc/homeros $(SRCS) src/main.c $(OBJS) $(LIBS)
	clang $(WARN) $(CPPFLAGS) -O2 -o build/clang/homeros $(SRCS) src/main.c $(OBJS) $(LIBS)

# unit tests and the recorded Neovim traces, under ASan and UBSan
TESTS = $(patsubst tests/test_%.c,%,$(wildcard tests/test_*.c))
test: $(OBJS) | build/san
	@rm -rf build/state
	@for t in $(TESTS); do \
	  gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/test_$$t tests/test_$$t.c $(SRCS) $(OBJS) $(LIBS) || exit 1; \
	  ./build/san/test_$$t || { echo "FAIL test_$$t"; exit 1; }; done
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)
	python3 tools/conform.py build/san/conform_drv --replay tests/conform-recorded.json
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/homeros $(SRCS) src/main.c $(OBJS) $(LIBS)
	@python3 tests/paste.py build/san/homeros -t 40000 || { echo "FAIL paste"; exit 1; }
	@python3 tests/hup.py build/san/homeros || { echo "FAIL hup"; exit 1; }
	@python3 tests/start.py build/san/homeros || { echo "FAIL start"; exit 1; }
	@python3 tests/watch.py build/san/homeros || { echo "FAIL watch"; exit 1; }

# libFuzzer, each target against a simple model, FUZZ_SECONDS each; an input
# that runs 20 s is a hang and fails (a planted bug once spun 20 min)
FUZZERS = $(patsubst tests/fuzz_%.c,%,$(wildcard tests/fuzz_*.c))
fuzz: $(OBJS) | build/fuzz
	@rm -rf build/state
	@for f in $(FUZZERS); do \
	  clang $(WARN) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
	    $(CPPFLAGS) -o build/fuzz/fuzz_$$f tests/fuzz_$$f.c $(LIB_SRCS) $(OBJS) $(LIBS) || exit 1; \
	  ./build/fuzz/fuzz_$$f -max_total_time=$(FUZZ_SECONDS) -max_len=256 -timeout=20 -artifact_prefix=build/fuzz/ 2>build/fuzz/$$f.log \
	    || { tail -40 build/fuzz/$$f.log; echo "FAIL fuzz_$$f"; exit 1; }; \
	  echo "fuzz_$$f: $$(grep -m1 -o 'Done [0-9]* runs in [0-9]* second(s)' build/fuzz/$$f.log)"; done

# Vim keys against nvim --headless on random sequences.  The random checks
# (conform, org-conform, undo-conform) take SEED, 1 by default, so a pull
# request's check is the same each run; CI's nightly run (nightly.yml) takes a
# fresh SEED, and what it finds is reduced, then fixed or listed
# (tests/conform-known.json)
conform: $(OBJS) | build/san
	@rm -rf build/state
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)
	python3 tools/conform.py build/san/conform_drv $(CONFORM_N) $${SEED:-1}

# Org's parser and colours against Org itself (package 16): fresh random org
# texts fed to src/org and to emacs -Q --batch (org-element in
# tools/org_dump.el, font-lock in tools/org_faces.el, org-todo in
# tools/org_todo.el, what each line shows in tools/org_show.el)
ORG_N ?= 2000
org-conform: $(OBJS) | build/san
ifneq ($(wildcard src/org/*.c),)
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/org_drv tests/org_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)
	python3 tools/org_conform.py build/san/org_drv $(ORG_N) $${SEED:-1}
	python3 tools/org_conform.py -f build/san/org_drv $(ORG_N) $${SEED:-1}
	python3 tools/org_conform.py -t build/san/org_drv $$(($(ORG_N) / 4)) $${SEED:-1}
	python3 tools/org_conform.py -s build/san/org_drv $(ORG_N) $${SEED:-1}
endif

# Neovim's undo tree against UndoTree.tla's rules (package 2's oracle);
# then the undo package against live nvim on one fake clock: short sequences
# with the real cap and with a cap of 3 that they reach, and long ones that
# reach the real cap (tools/undo_conform.py comes with package 2)
UNDO_CONFORM = python3 tools/undo_conform.py "gcc $(WARN) $(SAN)" $(call pick,text) $(call pick,undo) --
undo-diff: | build
	python3 design/ref/undo/undo_diff.py $(UNDO_N) 30 $${SEED:-2} | tee build/undo-diff.log
	grep -q ": 0 differ" build/undo-diff.log
ifneq ($(wildcard tools/undo_conform.py),)
	@seed=$${SEED:-1}; $(UNDO_CONFORM) $(UNDO_N) 60 1000 $$seed && \
	  $(UNDO_CONFORM) $(UNDO_N) 60 3 $$seed && $(UNDO_CONFORM) 10 4000 1000 $$seed
endif

# every TLA+ configuration; each .cfg's first line says pass or fail
specs:
	bash design/specs/check.sh

# a proof with sorry only warns, so look for it
lean: | build
	lean design/proofs/Undo.lean 2>&1 | tee build/lean.log; test $${PIPESTATUS[0]} -eq 0
	! grep -q "sorry" build/lean.log

# kernels with result contracts: every goal must prove
FRAMA_FILES = $(call pick,uni)
frama: | build
	why3 config detect >/dev/null
	frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 \
	  -cpp-extra-args="-Iinclude" $(FRAMA_FILES) | tee build/frama.log
	grep -qE "Proved goals: +([0-9]+) / \1$$" build/frama.log
	frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct nls \
	  -cpp-extra-args="-Iinclude -D_DEFAULT_SOURCE" src/text/text.c | tee build/frama-text.log
	grep -qE "Proved goals: +([0-9]+) / \1$$" build/frama-text.log
ifneq ($(wildcard src/lsp/u16.c),)
	frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct lsp_u16,lsp_u16_byte,step \
	  -cpp-extra-args="-Iinclude" src/lsp/u16.c | tee build/frama-lsp.log
	grep -qE "Proved goals: +([0-9]+) / \1$$" build/frama-lsp.log
endif

# the Windows builds (packages 9 and 11): each package's src/<module>/check.sh
# cross-builds its tests with zig cc and runs them under Wine (package 9's on
# a virtual X display); a plant names the one module whose check can catch it
# (make win WIN=file), so it does not run the others' slow tests too
WIN = $(patsubst src/%/check.sh,%,$(wildcard src/*/check.sh))
win: wincc
	@for m in $(WIN); do echo "win: src/$$m/check.sh"; bash src/$$m/check.sh || exit 1; done

# every file of the Windows editor compiles for Windows under our warnings,
# so a POSIX call in a shared module fails here, not first in a trial build.
# Not the terminal (no Windows half yet), the fonts (the window draws with
# GDI) or the Wayland window (POSIX only): the trial build stubs them.  It
# reads tree-sitter's headers only (not $(CPPFLAGS), which holds Linux paths
# and -DHOMEROS_WL), so it needs the sources, not $(OBJS)
WIN_SRCS = $(foreach m,$(filter-out tty font wl,$(MODULES)),$(call pick,$(m))) $(wildcard src/win/*.c) src/main.c
wincc: | $(SYN)/src build/win
	@for f in $(WIN_SRCS); do zig cc -target x86_64-windows-gnu $(WARN) $(CPPFLAGS) -c -o build/win/cc.o $$f || exit 1; done
	zig rc /fo build/win/homeros.res assets/homeros.rc
	@echo "wincc: $(words $(WIN_SRCS)) files and the icon resource compile for Windows"
build/win:
	mkdir -p $@

check: compilers test fuzz conform org-conform undo-diff lean frama specs win dupes
	@echo "check: all passed"

# copied code fails (CLAUDE.md: no duplication)
dupes:
	@python3 tools/dupes.py

# every function defined, file:line and signature: grep it before writing one
index:
	@grep -rnE '^(static )?[a-zA-Z_][a-zA-Z0-9_ *]*[ *][a-zA-Z_][a-zA-Z0-9_]*\([^;{]*(\{.*)?$$' --include=*.c src ref

planted:
	SHARD=$(SHARD) bash tools/planted.sh

# the Linux window's icon and launcher entry: wl.c's app_id "homeros"
# names homeros.desktop, whose Icon=homeros the hicolor theme resolves
PREFIX ?= $(HOME)/.local
install: homeros
	install -Dm755 homeros $(DESTDIR)$(PREFIX)/bin/homeros
	install -Dm644 assets/homeros.desktop $(DESTDIR)$(PREFIX)/share/applications/homeros.desktop
	install -Dm644 assets/homeros.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/homeros.svg
	for n in 16 24 32 48; do install -Dm644 assets/hicolor/$${n}x$$n/apps/homeros.png \
	  $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${n}x$$n/apps/homeros.png; done

clean:
	rm -rf build homeros

.PHONY: dupes index compilers test fuzz conform org-conform undo-diff specs lean frama win wincc check planted install clean
