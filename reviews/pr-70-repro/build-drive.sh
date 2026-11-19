# Builds PR 29's review driver (reviews/pr-29-repro/drive.c) as the tests are
# built, into $1.  From the repo root, after `make test TESTS=git` built build/.
cmd=$(make -n test TESTS=git 2>/dev/null | grep 'tests/test_$t.c' | head -1 | sed 's/|| exit 1; \\//; s/-Werror//')
cmd=${cmd//build\/san\/test_\$t/$1}
cmd=${cmd//tests\/test_\$t.c/reviews\/pr-29-repro\/drive.c}
eval "$cmd"
