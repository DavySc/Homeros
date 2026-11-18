# The bridge's child (bridge.h, src/infoview/serve/): a program of its own
# beside the editor, never linked into it.  The editor looks for it next to
# its own binary, so each build of homeros gets one in the same directory:
# the unit tests' (build/san/) under the sanitizers too.
IV_SERVE = src/infoview/serve/serve.c
homeros: homeros-infoview
homeros-infoview: $(IV_SERVE)
	$(CC) $(WARN) $(CFLAGS) -o $@ $<
compilers: build/gcc/homeros-infoview build/clang/homeros-infoview
build/gcc/homeros-infoview: $(IV_SERVE) | build/gcc
	gcc $(WARN) -O2 -o $@ $<
build/clang/homeros-infoview: $(IV_SERVE) | build/clang
	clang $(WARN) -O2 -o $@ $<
test: build/san/homeros-infoview
build/san/homeros-infoview: $(IV_SERVE) | build/san
	gcc $(WARN) $(SAN) -DPAGE_TIMEOUT=1 -o $@ $<
