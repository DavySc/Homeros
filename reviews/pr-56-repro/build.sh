#!/usr/bin/env bash
# Builds one of this directory's programs against a tree with the tree's own
# Makefile variables (so it links what the tree's tests link).
# usage: build.sh TREE SRC OUT [extra cc flags]   (after `make homeros` in TREE)
set -e
here=$(cd "$(dirname "$0")" && pwd)
tree=$1 src=$2 out=$3; shift 3
mk=$(mktemp)
printf 'reviewbin: $(OBJS)\n\tgcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Iinclude -Itests $(CPPFLAGS) $(XF) -o $(OUT) $(SRC) $(LIB_SRCS) $(OBJS) $(LIBS)\n' > "$mk"
make -s -C "$tree" -f Makefile -f "$mk" reviewbin SRC="$here/$src" OUT="$out" XF="$*"
rm -f "$mk"
