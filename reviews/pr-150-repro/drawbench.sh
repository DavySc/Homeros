#!/bin/sh
# builds drawbench against a tree (its root as $1, $2 the DRAW macro) -O2
cd "$1" || exit 1
SRCS=$(make -s -pn 2>/dev/null | sed -n 's/^SRCS := //p')
OBJS="build/image/stb.o build/syntax/tree-sitter.o build/syntax/queries.o build/ghostty/libghostty-vt.a build/wl/protocols.o $(find build/syntax/g -name '*.o')"
CPP="$(pkg-config --cflags freetype2 harfbuzz fontconfig wayland-client xkbcommon) -I/usr/include/stb -Ibuild/syntax/src/tree-sitter/lib/include -Ibuild/syntax/src/tree-sitter/lib/src -Ibuild/ghostty/include -DGHOSTTY_STATIC -DHOMEROS_WL -Ibuild/wl"
LIBS="$(pkg-config --libs freetype2 harfbuzz fontconfig) -lm $(pkg-config --libs wayland-client xkbcommon)"
gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude $CPP -O2 "-DDRAW(e,g)=$2" -o "$3" "$4" $SRCS $OBJS $LIBS
