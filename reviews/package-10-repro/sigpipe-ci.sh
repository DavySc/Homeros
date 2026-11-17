#!/usr/bin/env bash
# The plant "SIGPIPE kills the editor" is caught from a shell and NOT CAUGHT
# in CI: GitHub's runner starts steps with SIGPIPE ignored, the test inherits
# that, and the editor survives without loop.c's signal(SIGPIPE, SIG_IGN).
# $1 = test_loop built with that line removed.
echo "SIGPIPE at its default:"; IDLE_SECONDS=1 "$1" 2>&1 | tail -1; echo "  exit ${PIPESTATUS[0]}"
echo "SIGPIPE ignored by the parent (as in CI):"; (trap '' PIPE; IDLE_SECONDS=1 "$1" 2>&1 | tail -1; echo "  exit ${PIPESTATUS[0]}")
