# Sourced by the Windows checks (src/*/check.sh, src/undo/win.sh).
# pass SECONDS EXE [RUNNER...]: runs EXE (under RUNNER, e.g. wine) and
# passes only when it exits 0 AND its last line is its own summary
# ("test_x: ...").  Wine has reported status 0 for a test that trapped
# (package 17 measured 1 of 6 runs of an alignment plant, and PR 95's CI
# missed that plant): the summary is printed only after every check, and
# a line printed before it does not start with the test's name and colon.
pass() {
	local s=$1 exe=$2
	shift 2
	timeout "$s" "$@" "$exe" 2>&1 | tee "${exe%.exe}.out"
	[ "${PIPESTATUS[0]}" = 0 ] && [ "$(tail -n 1 "${exe%.exe}.out" | cut -d: -f1)" = "$(basename "$exe" .exe)" ]
}
