/* snacks.picker's smart order on the test tree, by src/tools/conform/pick.sh */
expect("", "src/core/core.c|Makefile|README.md|picker.lua|docs/READ.md|src/core/cored.h|src/tools/pick.c|tests/test_pick.c|src/tools/match.hh|src/tools/matchx.ccc|");
expect("pick", "picker.lua|src/tools/pick.c|tests/test_pick.c|");
expect("core", "src/core/core.c|src/core/cored.h|");
expect("c", "src/core/core.c|src/core/cored.h|src/tools/pick.c|tests/test_pick.c|src/tools/matchx.ccc|picker.lua|src/tools/match.hh|docs/READ.md|");
expect("PICK", "");
expect("s/t", "src/tools/pick.c|src/tools/match.hh|src/tools/matchx.ccc|tests/test_pick.c|");
expect("'core", "src/core/core.c|src/core/cored.h|");
expect("^src", "src/core/cored.h|src/tools/pick.c|src/tools/match.hh|src/tools/matchx.ccc|");
expect("md$", "README.md|docs/READ.md|");
expect("!src", "Makefile|README.md|picker.lua|docs/READ.md|tests/test_pick.c|");
expect("tools | docs", "src/tools/pick.c|src/tools/match.hh|src/tools/matchx.ccc|docs/READ.md|");
expect("src pick", "src/tools/pick.c|");
expect("core.c:2", "src/core/core.c|");
expect("zzz", "");
