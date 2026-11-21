# Package 12: tree-sitter's runtime (one file, lib.c) and the grammars, from
# the sources src/syntax/sources.nix names, compiled with our compiler but
# outside our warnings, since they are generated or third-party code.  The
# sources are fetched once; after changing sources.nix, rm -rf build/syntax.
SYN = build/syntax
SYN_GRAMMARS = c/src/parser lua/src/parser lua/src/scanner \
  markdown/tree-sitter-markdown/src/parser markdown/tree-sitter-markdown/src/scanner \
  markdown/tree-sitter-markdown-inline/src/parser markdown/tree-sitter-markdown-inline/src/scanner \
  rust/parser rust/scanner glsl/parser nix/parser nix/scanner \
  cmake/parser cmake/scanner systemverilog/parser
CPPFLAGS += -I$(SYN)/src/tree-sitter/lib/include -I$(SYN)/src/tree-sitter/lib/src
OBJS += $(SYN)/tree-sitter.o $(SYN_GRAMMARS:%=$(SYN)/g/%.o) $(SYN)/queries.o

$(SYN)/src:
	@mkdir -p $(SYN)
	nix build -f src/syntax/sources.nix -o $@

$(SYN)/tree-sitter.o: | $(SYN)/src
	$(CC) -O2 -c -I$(SYN)/src/tree-sitter/lib/include -I$(SYN)/src/tree-sitter/lib/src \
	  $(SYN)/src/tree-sitter/lib/src/lib.c -o $@

$(SYN)/g/%.o: | $(SYN)/src
	@mkdir -p $(@D)
	$(CC) -O2 -c -I$(dir $(SYN)/src/$*) $(SYN)/src/$*.c -o $@

# Neovim's highlights, injections and folds queries, compiled in, so the
# editor needs no files at run time: syntax_queries[lang][0 highlights,
# 1 injections, 2 folds], the languages in syntax.c's order, each file as
# one string of octal escapes ("" for a file the language does not have).
# A file starting "; inherits: x" follows x's file, as query.lua reads it.
$(SYN)/queries.c: src/syntax/build.mk | $(SYN)/src
	{ echo 'const char *const syntax_queries[][3] = {'; \
	  for l in c lua markdown markdown_inline rust glsl nix cmake systemverilog; do for f in highlights injections folds; do \
	    q=$(SYN)/src/queries/$$l/$$f.scm; b=$$(sed -n '1s/^; inherits: *//p' $$q); \
	    printf '"'; [ ! -f $$q ] || cat $${b:+$(SYN)/src/queries/$$b/$$f.scm} $$q | od -An -v -to1 | tr -d '\n' | sed 's/ /\\/g'; echo '",'; \
	  done; done; echo '};'; } > $@

$(SYN)/queries.o: $(SYN)/queries.c
	$(CC) -O2 -c $< -o $@
