# The SQL refactor prototype (KB: /mnt/project-files/sql-refactor-kb/).
# Never merged to main.  Builds main's lexer and text from a checkout of
# main, copying none of it: make HOMEROS=<path to main>.
HOMEROS ?= ../main
CC ?= cc
CFLAGS ?= -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
CPPFLAGS += -Isqlref -I$(HOMEROS)/include -I$(HOMEROS)/src/syntax
MAIN = $(HOMEROS)/src/text/text.c $(HOMEROS)/src/syntax/tsql.c
LIB = $(wildcard sqlref/*.c)

all: build/snapq

build/%: cmd/%.c $(LIB) $(MAIN) sqlref/*.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(MAIN)

clean:
	rm -rf build
.PHONY: all clean
