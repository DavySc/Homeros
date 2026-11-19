#!/usr/bin/env bash
# Writes pick-expect.inc: snacks.picker's orders for tests/test_tools_pick.c.
# pick.sh SNACKS_DIR (Neovim 0.12 and rg on PATH; snacks at my-nvim's 882c996)
set -e
here=$(cd "$(dirname "$0")" && pwd)
tree=/tmp/hp   # where the test makes it (tests/test_tools_pick.c)
rm -rf "$tree"; mkdir "$tree"
cd "$tree"
# the test's tree (tests/test_tools_pick.c, tree[])
for f in Makefile README.md picker.lua docs/READ.md src/core/core.c src/core/coredd.hpp \
         src/tools/pick.c tests/test_pick.c src/tools/match.hh src/tools/matchx.ccc .hidden/x.c; do
	mkdir -p "$(dirname "$f")"; printf 'line one\nint pick(void) { return 1; }\nlast line\n' > "$f"
done
# the recent files (pick.lua's v:oldfiles) a day ahead: their frecency seed
# (M:seed, from the mtime) is then above 1, never level with the buffer's
touch -d '+1 day' README.md src/tools/pick.c tests/test_pick.c
pats=("" "pick" "core" "c" "PICK" "s/t" "'core" "^src" "md$" "!src" "tools | docs" "src pick" "core.c:2" "zzz")
nvim --clean --headless -l "$here/pick.lua" "$1" smart "${pats[@]}" 2>/dev/null > "$tree.out"
{
	echo "/* snacks.picker's smart order on the test tree, by src/tools/conform/pick.sh */"
	i=0
	while IFS= read -r line; do
		printf 'expect("%s", "%s");\n' "${pats[$i]}" "$line"
		i=$((i + 1))
	done < "$tree.out"
} > "$here/pick-expect.inc"
# files with no pattern is rg's own order (it lists in parallel): not compared
for src in recent files; do
	spats=("pick" "md" "zzz" "c")
	[ $src = recent ] && spats=("" "${spats[@]}")
	nvim --clean --headless -l "$here/pick.lua" "$1" $src "${spats[@]}" 2>/dev/null > "$tree.out"
	i=0
	while IFS= read -r line; do
		printf 'expect_src(PICK_%s, "%s", "%s");\n' "${src^^}" "${spats[$i]}" "$line"
		i=$((i + 1))
	done < "$tree.out" >> "$here/pick-expect.inc"
done
gpats=("pick" "line" "PICK" "return 1" "l.ne o" "" "pick -- -g*.md" "line   --   -g  *.c" "return -- -g*.lua -g!tests")
nvim --clean --headless -l "$here/pick.lua" "$1" grep "${gpats[@]}" 2>/dev/null > "$tree.out"
i=0
while IFS= read -r line; do
	printf 'expect_grep("%s", "%s");\n' "${gpats[$i]}" "$line"
	i=$((i + 1))
done < "$tree.out" >> "$here/pick-expect.inc"
rm -rf "$tree" "$tree.out"
cat "$here/pick-expect.inc"
