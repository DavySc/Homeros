# Homeros.  `make` builds the editor; `make check` runs every check
# (CLAUDE.md: "Every merge runs the whole check"); `make planted` shows that
# each check fails on a planted bug.
#
# A module comes from src/<module>/ once its package exists there, else from
# the reference implementation in ref/.  Package 6 owns both grid and tty.

SHELL = /bin/bash

MODULES = text undo uni core keys grid tty loop file
pick = $(if $(wildcard src/$(1)/*.c),$(wildcard src/$(1)/*.c),ref/$(1).c)
SRCS := $(foreach m,$(MODULES),$(call pick,$(m)))
LIB_SRCS := $(filter-out $(call pick,tty) $(call pick,loop),$(SRCS))

CC ?= cc
CFLAGS ?= -O2
WARN = -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude
SAN = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
FUZZ_SECONDS ?= 30
CONFORM_N ?= 2000
UNDO_N ?= 5000

homeros: $(SRCS) src/main.c include/*.h
	$(CC) $(WARN) $(CFLAGS) -o $@ $(SRCS) src/main.c

build:
	@mkdir -p $@
build/%:
	@mkdir -p $@

# both compilers, warnings as errors
compilers: | build/gcc build/clang
	gcc $(WARN) -O2 -o build/gcc/homeros $(SRCS) src/main.c
	clang $(WARN) -O2 -o build/clang/homeros $(SRCS) src/main.c

# unit tests and the recorded Neovim traces, under ASan and UBSan
TESTS = text undo uni file grid
test: | build/san
	@for t in $(TESTS); do \
	  gcc $(WARN) $(SAN) -o build/san/test_$$t tests/test_$$t.c $(SRCS) || exit 1; \
	  ./build/san/test_$$t || { echo "FAIL test_$$t"; exit 1; }; done
	gcc $(WARN) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS)
	python3 tools/conform.py build/san/conform_drv --replay tests/conform-recorded.json

# libFuzzer, each target against a simple model, FUZZ_SECONDS each
FUZZERS = text keys
fuzz: | build/fuzz
	@for f in $(FUZZERS); do \
	  clang $(WARN) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
	    -o build/fuzz/fuzz_$$f tests/fuzz_$$f.c $(LIB_SRCS) || exit 1; \
	  ./build/fuzz/fuzz_$$f -max_total_time=$(FUZZ_SECONDS) -artifact_prefix=build/fuzz/ 2>build/fuzz/$$f.log \
	    || { tail -40 build/fuzz/$$f.log; echo "FAIL fuzz_$$f"; exit 1; }; \
	  echo "fuzz_$$f: $$(grep -m1 -o 'Done [0-9]* runs in [0-9]* second(s)' build/fuzz/$$f.log)"; done

# Vim keys against nvim --headless, fresh random sequences
conform: | build/san
	gcc $(WARN) $(SAN) -o build/san/conform_drv tests/conform_drv.c $(LIB_SRCS)
	python3 tools/conform.py build/san/conform_drv $(CONFORM_N) $${SEED:-$$(date +%s)}

# Neovim's undo tree against UndoTree.tla's rules (package 2's oracle)
undo-diff: | build
	python3 design/ref/undo/undo_diff.py $(UNDO_N) 30 $${SEED:-2} | tee build/undo-diff.log
	grep -q ": 0 differ" build/undo-diff.log

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
	frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct uni_len,uni_prev \
	  -cpp-extra-args="-Iinclude" $(FRAMA_FILES) | tee build/frama.log
	grep -qE "Proved goals: +([0-9]+) / \1$$" build/frama.log

check: compilers test fuzz conform undo-diff lean frama specs
	@echo "check: all passed"

planted:
	bash tools/planted.sh

clean:
	rm -rf build homeros

.PHONY: compilers test fuzz conform undo-diff specs lean frama check planted clean
