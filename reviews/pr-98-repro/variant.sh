#!/bin/bash
# PR 98 review: tests/org_drv.c built with another org.c (the check's own
# compile line, from make -n).  variant.sh ORG.C OUT   (run in the repo,
# after one `make org-conform` so the objects exist)
mkdir -p build/v && cp "$1" build/v/org.c
eval "$(make -n org-conform 2>/dev/null | grep -m1 'org_drv tests' | sed -e "s#-o build/san/org_drv#-o $2#" -e 's#src/org/org.c#build/v/org.c#')"
