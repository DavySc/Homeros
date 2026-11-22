# The SQL refactor prototype (KB: /mnt/project-files/sql-refactor-kb/).
# Never merged to main.  Builds main's lexer and text from a checkout of
# main, copying none of it: make HOMEROS=<path to main>.
HOMEROS ?= ../main
CC ?= cc
CFLAGS ?= -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CPPFLAGS += -Isqlref -I$(HOMEROS)/include -I$(HOMEROS)/src/syntax
MAIN = $(HOMEROS)/src/text/text.c $(HOMEROS)/src/syntax/tsql.c
LIB = $(wildcard sqlref/*.c)

all: build/snapq build/resq build/refq build/tdsq build/pgq build/fuzz_tds build/fuzz_pg

build/%: cmd/%.c $(LIB) $(MAIN) sqlref/*.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(MAIN)

# the SQL client bake-off: the own TDS client needs nothing of main's
build/tdsq: cmd/sqlq.c client/tds.c client/tds.h client/net.c client/net.h
	@mkdir -p build
	$(CC) -Iclient $(CFLAGS) -o $@ cmd/sqlq.c client/tds.c client/net.c -lssl -lcrypto

build/pgq: cmd/sqlq.c client/pg.c client/pg.h client/net.c client/net.h
	@mkdir -p build
	$(CC) -Iclient -DPG $(CFLAGS) -o $@ cmd/sqlq.c client/pg.c client/net.c -lssl -lcrypto

clean:
	rm -rf build
.PHONY: all clean

build/fuzz_tds: fuzz/fuzz_tds.c client/tds.c client/tds.h client/net.c client/net.h
	@mkdir -p build
	clang -Iclient -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $@ fuzz/fuzz_tds.c -lssl -lcrypto

build/fuzz_pg: fuzz/fuzz_pg.c client/pg.c client/pg.h client/net.c client/net.h
	@mkdir -p build
	clang -Iclient -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $@ fuzz/fuzz_pg.c -lssl -lcrypto
