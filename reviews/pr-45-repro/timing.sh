#!/usr/bin/env bash
# Timings for the PR 45 review on an idle machine: 5 alternating runs of the
# PR's org_edit (work) and the one-parse variant (work_once), 2,000 one-byte
# inserts each deleted again per file; then the new-headline case.
cd "$(dirname "$0")"
B=${B:-/home/claude/pr45}; C=${C:-/home/claude/corpus}
FILES="$B/design/org/synthetic-agenda.org $C/org-mode/etc/ORG-NEWS $C/org-mode/doc/org-manual.org agenda12.org agenda12-one-top.org"
for r in 1 2 3 4 5; do
	echo "run $r PR"; ./work $r 2000 $FILES
	echo "run $r variant"; ./work_once $r 2000 $FILES
done
for r in 1 2 3 4 5; do
	echo "run $r PR, '* n' typed above level-2 headlines"; INS=$'* n\n' POS=0 ./work 1 3 agenda-demoted.org
	echo "run $r variant, '* n' typed above level-2 headlines"; INS=$'* n\n' POS=0 ./work_once 1 3 agenda-demoted.org agenda12-demoted.org
done
echo "PR, 974 KB, once"; INS=$'* n\n' POS=0 ./work 1 1 agenda12-demoted.org
