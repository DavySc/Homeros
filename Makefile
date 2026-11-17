# Homeros.  `make` builds the editor; `make check` runs every check
# (CLAUDE.md: "Every merge runs the whole check"); `make planted` shows that
# each check fails on a planted bug.
#
# A module comes from src/<module>/ once its package exists there, else from
# the reference implementation in ref/.  Package 6 owns both grid and tty.
# Modules, tests, fuzzers and plants are found by name, so a package adds
# files in its own places and never edits this one.

SHELL = /bin/bash

BASE = text undo uni core keys grid tty loop file syntax
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

# what the tests, fuzzers and drivers link: everything but the terminal,
# the window and the loop
TEST_SRCS := $(filter-out $(call pick,wl),$(SRCS))
LIB_SRCS := $(filter-out $(call pick,tty) $(call pick,loop),$(TEST_SRCS))

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
	@for t in $(TESTS); do \
	  gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/test_$$t tests/test_$$t.c $(TEST_SRCS) $(OBJS) $(LIBS) || exit 1; \
	  ./build/san/test_$$t || { echo "FAIL test_$$t"; exit 1; }; done
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)
	python3 tools/conform.py build/san/conform_drv --replay tests/conform-recorded.json
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/homeros $(TEST_SRCS) src/main.c $(OBJS) $(LIBS)
	@python3 tests/paste.py build/san/homeros -t 40000 || { echo "FAIL paste"; exit 1; }

# libFuzzer, each target against a simple model, FUZZ_SECONDS each; an input
# that runs 20 s is a hang and fails (a planted bug once spun 20 min)
FUZZERS = $(patsubst tests/fuzz_%.c,%,$(wildcard tests/fuzz_*.c))
fuzz: $(OBJS) | build/fuzz
	@for f in $(FUZZERS); do \
	  clang $(WARN) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
	    $(CPPFLAGS) -o build/fuzz/fuzz_$$f tests/fuzz_$$f.c $(LIB_SRCS) $(OBJS) $(LIBS) || exit 1; \
	  ./build/fuzz/fuzz_$$f -max_total_time=$(FUZZ_SECONDS) -max_len=256 -timeout=20 -artifact_prefix=build/fuzz/ 2>build/fuzz/$$f.log \
	    || { tail -40 build/fuzz/$$f.log; echo "FAIL fuzz_$$f"; exit 1; }; \
	  echo "fuzz_$$f: $$(grep -m1 -o 'Done [0-9]* runs in [0-9]* second(s)' build/fuzz/$$f.log)"; done

# Vim keys against nvim --headless, fresh random sequences
conform: $(OBJS) | build/san
	gcc $(WARN) $(CPPFLAGS) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS) $(OBJS) $(LIBS)
	python3 tools/conform.py build/san/conform_drv $(CONFORM_N) $${SEED:-$$(date +%s)}

# Neovim's undo tree against UndoTree.tla's rules (package 2's oracle);
# then the undo package against live nvim on one fake clock: short sequences
# with the real cap and with a cap of 3 that they reach, and long ones that
# reach the real cap (tools/undo_conform.py comes with package 2)
UNDO_CONFORM = python3 tools/undo_conform.py "gcc $(WARN) $(SAN)" $(call pick,text) $(call pick,undo) --
undo-diff: | build
	python3 design/ref/undo/undo_diff.py $(UNDO_N) 30 $${SEED:-2} | tee build/undo-diff.log
	grep -q ": 0 differ" build/undo-diff.log
ifneq ($(wildcard tools/undo_conform.py),)
	@seed=$${SEED:-$$(date +%s)}; $(UNDO_CONFORM) $(UNDO_N) 60 1000 $$seed && \
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
	frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct uni_len,uni_prev,uni_cp \
	  -cpp-extra-args="-Iinclude" $(FRAMA_FILES) | tee build/frama.log
	grep -qE "Proved goals: +([0-9]+) / \1$$" build/frama.log

# the Windows build (package 9): cross-built with zig cc, run under Wine on
# a virtual X display; src/win/check.sh says how
win:
	@if [ -f src/win/check.sh ]; then bash src/win/check.sh; else echo "win: no src/win yet"; fi

check: compilers test fuzz conform undo-diff lean frama specs win
	@echo "check: all passed"

planted:
	bash tools/planted.sh

clean:
	rm -rf build homeros

.PHONY: compilers test fuzz conform undo-diff specs lean frama win check planted clean
