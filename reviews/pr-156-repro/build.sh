#!/bin/sh
# build a probe the way `make test` builds a unit test: build.sh name.c out
cd "$(dirname "$0")/../.." || exit 1
SRCS=$(make -s -pn 2>/dev/null | sed -n 's/^SRCS := //p')
OBJS=$(ls build/image/stb.o build/syntax/tree-sitter.o build/syntax/queries.o build/ghostty/libghostty-vt.a build/wl/protocols.o; find build/syntax/g -name '*.o')
CPP="$(pkg-config --cflags freetype2 harfbuzz fontconfig wayland-client xkbcommon) -I/usr/include/stb -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src -Ibuild/ghostty/include -DGHOSTTY_STATIC -DHOMEROS_WL -Ibuild/wl"
LIBS="$(pkg-config --libs freetype2 harfbuzz fontconfig) -lm $(pkg-config --libs wayland-client xkbcommon)"
cp "$1" tests/zprobe_tmp.c
gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude $CPP -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -o "$2" tests/zprobe_tmp.c $SRCS $OBJS $LIBS
r=$?; rm -f tests/zprobe_tmp.c; exit $r
