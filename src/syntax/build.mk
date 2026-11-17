# Package 12: tree-sitter's runtime (one file, lib.c) and the grammars, from
# the sources src/syntax/sources.nix names, compiled with our compiler but
# outside our warnings, since they are generated or third-party code.  The
# sources are fetched once; after changing sources.nix, rm -rf build/syntax.
SYN = build/syntax
SYN_GRAMMARS = c/src/parser lua/src/parser lua/src/scanner \
  markdown/tree-sitter-markdown/src/parser markdown/tree-sitter-markdown/src/scanner \
  markdown/tree-sitter-markdown-inline/src/parser markdown/tree-sitter-markdown-inline/src/scanner
CPPFLAGS += -I$(SYN)/src/tree-sitter/lib/include -DSYNTAX_QUERIES='"$(CURDIR)/$(SYN)/src/queries"'
OBJS += $(SYN)/tree-sitter.o $(SYN_GRAMMARS:%=$(SYN)/g/%.o)

$(SYN)/src:
	@mkdir -p $(SYN)
	nix build -f src/syntax/sources.nix -o $@

$(SYN)/tree-sitter.o: | $(SYN)/src
	$(CC) -O2 -c -I$(SYN)/src/tree-sitter/lib/include -I$(SYN)/src/tree-sitter/lib/src \
	  $(SYN)/src/tree-sitter/lib/src/lib.c -o $@

$(SYN)/g/%.o: | $(SYN)/src
	@mkdir -p $(@D)
	$(CC) -O2 -c -I$(dir $(SYN)/src/$*) $(SYN)/src/$*.c -o $@
