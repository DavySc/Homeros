#!/bin/sh
# usage: build.sh REPO OUT SRC   (from anywhere)
gcc -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -D_DEFAULT_SOURCE -I"$1/include" -I"$1/src/lsp" -o "$2" "$3" "$1"/src/lsp/lsp.c "$1"/src/lsp/json.c "$1"/src/lsp/u16.c "$1"/src/text/text.c
