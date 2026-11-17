#!/bin/bash
# Rebuilds the 35-file corpus (it is not stored in native-editor/org/): 32,336 lines, 1,111,887 bytes on 2026-11-17.
git clone --depth 1 -q https://github.com/bzg/org-mode.git orgsrc; git clone --depth 1 -q https://github.com/nvim-orgmode/orgmode.git nvo
mkdir -p corpus; cp orgsrc/testing/examples/*.org orgsrc/doc/org-manual.org orgsrc/doc/org-guide.org corpus/
for n in refile todo; do cp nvo/tests/plenary/fixtures/$n.org corpus/nvim-$n.org; done
cp nvo/tests/plenary/fixtures/hyperlink/testfile.org corpus/nvim-testfile.org
cp nvo/tests/plenary/fixtures/hyperlink/subdir/subtest.org corpus/nvim-subtest.org; cp nvo/tests/plenary/fixtures/hyperlink/subdir2/subtest2.org corpus/nvim-subtest2.org
cp ../../../native-editor/org/synthetic-agenda.org corpus/
# then keep only the 35 names listed in native-editor/org/el.json
