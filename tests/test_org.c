/* The org parser (src/org) on fixed texts, against Org's own answer
   recorded with tools/org_dump.el (Org 9.8.7, Emacs 31.1): the reduced cases
   that disqualified orgize and tree-sitter-org (design/reviews/r2/org/
   reductions), the start of design/org/synthetic-agenda.org, and the cases
   tools/org_conform.py once found differing, and Emacs's [[:word:]] and
   [[:alnum:]] above ASCII in drawer names, footnote labels and tags, and
   the timestamps of planning and clock lines.  Each text also passes the
   fuzzer's checks (outline model, invariants, subtree reparse). */
#define _GNU_SOURCE
#define ORG_DRV_LIB
#include "check.h"
#include "org_drv.c"
#define LLVMFuzzerTestOneInput fuzz_org_unused
#include "fuzz_org.c"

static const struct { const char *name, *text, *want; } cases[] = {
	{ "a.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun .+1d>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 75 75\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 75 75\n"
	  "planning 9 9 - - 42\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day - - - - - 20 41\n"
	  "property-drawer 42 42 55 69 75\n"
	  "node-property 55 55 - - 69\n" },
	{ "aff.org",
	  "#+NAME: a\n"
	  "\n"
	  "#+CAPTION[s]: c\n"
	  "# comment\n"
	  "#+RESULTS:\n"
	  ": 1\n",
	  "section 0 0 0 52 52\n"
	  "keyword 0 0 - - 11\n"
	  "keyword 11 11 - - 27\n"
	  "comment 27 27 - - 37\n"
	  "fixed-width 37 48 - - 52\n" },
	{ "b.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun +1d>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 74 74\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 74 74\n"
	  "planning 9 9 - - 41\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - cumulate 1 day - - - - - 20 40\n"
	  "property-drawer 41 41 54 68 74\n"
	  "node-property 54 54 - - 68\n" },
	{ "blocks.org",
	  "#+begin_src c -n 3 :results output\n"
	  "x\n"
	  "#+end_src\n"
	  "#+BEGIN_QUOTE\n"
	  "\n"
	  "#+END_QUOTE\n"
	  "#+begin_verse\n"
	  "#+end_verse\n"
	  "#+begin_foo bar\n"
	  "* not\n"
	  "#+end_foo\n",
	  "section 0 0 0 116 116\n"
	  "src-block 0 0 - - 47\n"
	  "quote-block 47 47 61 62 74\n"
	  "paragraph 61 61 61 62 62\n"
	  "verse-block 74 74 88 88 100\n"
	  "paragraph 100 100 100 116 116\n"
	  "headline 116 116 122 132 132\n"
	  "+ 1 - - - \"not\" \n"
	  "section 122 122 122 132 132\n"
	  "paragraph 122 122 122 132 132\n" },
	{ "c.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 70 70\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 70 70\n"
	  "planning 9 9 - - 37\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - - - - - - - - - 20 36\n"
	  "property-drawer 37 37 50 64 70\n"
	  "node-property 50 50 - - 64\n" },
	{ "call.org",
	  "#+CALL: x[a\n"
	  "* h\n"
	  "]  (b)\n"
	  "after\n",
	  "section 0 0 0 12 12\n"
	  "babel-call 0 0 - - 23\n"
	  "headline 12 12 16 29 29\n"
	  "+ 1 - - - \"h\" \n"
	  "section 16 16 16 29 29\n"
	  "paragraph 16 16 16 29 29\n" },
	{ "call2.org",
	  "#+CALL: \n"
	  "   a b\n"
	  "\n"
	  "c\n",
	  "section 0 0 0 19 19\n"
	  "babel-call 0 0 - - 17\n"
	  "paragraph 17 17 17 19 19\n" },
	{ "d.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Mon .+1d>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 75 75\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 75 75\n"
	  "planning 9 9 - - 42\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day - - - - - 20 41\n"
	  "property-drawer 42 42 55 69 75\n"
	  "node-property 55 55 - - 69\n" },
	{ "drawer.org",
	  "* h\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-01 Sun 09:00]--[2026-11-01 Sun 10:30] =>  1:30\n"
	  "- State \"DONE\" from \"TODO\" [2026-11-02 Mon 10:31]\n"
	  ":END:\n"
	  "\n"
	  ":x:\n"
	  ":END:\n",
	  "headline 0 0 4 144 144\n"
	  "+ 1 - - - \"h\" \n"
	  "section 4 4 4 144 144\n"
	  "drawer 4 4 14 127 134\n"
	  "clock 14 14 - - 77\n"
	  "@ clock inactive-range daterange 2026 11 1 9 0 2026 11 1 10 30 - - - - - - - - 21 68\n"
	  "= 1:30\n"
	  "plain-list 77 77 77 127 127\n"
	  "item 77 77 79 127 127\n"
	  "paragraph 79 79 79 127 127\n"
	  "drawer 134 134 - - 144\n" },
	{ "dynblank.org",
	  "#+BEGIN: clocktable :scope file\n"
	  "\n"
	  "  \\begin{equation}\n"
	  " x\n"
	  "#+END:\n",
	  "section 0 0 0 62 62\n"
	  "dynamic-block 0 0 32 55 62\n"
	  "paragraph 32 32 32 33 33\n"
	  "paragraph 33 33 33 55 55\n" },
	{ "e.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun ++1w>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 75 75\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 75 75\n"
	  "planning 9 9 - - 42\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - catch-up 1 week - - - - - 20 41\n"
	  "property-drawer 42 42 55 69 75\n"
	  "node-property 55 55 - - 69\n" },
	{ "empty.org",
	  "",
	  "" },
	{ "f.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun .+1d/3d>\n"
	  ":PROPERTIES:\n"
	  ":STYLE: habit\n"
	  ":END:\n",
	  "headline 0 0 9 78 78\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 78 78\n"
	  "planning 9 9 - - 45\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day 3 day - - - 20 44\n"
	  "property-drawer 45 45 58 72 78\n"
	  "node-property 58 58 - - 72\n" },
	{ "foot.org",
	  "[fn:1] a\n"
	  "\n"
	  "\n"
	  "b\n"
	  "[fn:2]\n"
	  "\n"
	  " c\n"
	  "#+NAME: x\n"
	  "[fn:3] d\n",
	  "section 0 0 0 43 43\n"
	  "footnote-definition 0 0 7 9 11\n"
	  "paragraph 7 7 7 9 9\n"
	  "paragraph 11 11 11 13 13\n"
	  "footnote-definition 13 13 21 24 24\n"
	  "paragraph 21 21 21 24 24\n"
	  "footnote-definition 24 34 41 43 43\n"
	  "paragraph 41 41 41 43 43\n" },
	{ "g.org",
	  "* TODO x\n"
	  "SCHEDULED: <2026-11-22 Sun 09:00 +1w -2d>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n",
	  "headline 0 0 9 84 84\n"
	  "+ 1 TODO - - \"x\" \n"
	  "section 9 9 9 84 84\n"
	  "planning 9 9 - - 51\n"
	  "@ scheduled active - 2026 11 22 9 0 2026 11 22 9 0 cumulate 1 week - - all 2 day 20 50\n"
	  "property-drawer 51 51 64 78 84\n"
	  "node-property 64 64 - - 78\n" },
	{ "items.org",
	  "- [ ] a\n"
	  "- [X] b :: c\n"
	  "  1. [@3] d\n"
	  "  2) e :: f\n"
	  "\n"
	  "\n"
	  "- g\n"
	  "\t* h\n",
	  "section 0 0 0 56 56\n"
	  "plain-list 0 0 0 45 47\n"
	  "item 0 0 6 8 8\n"
	  "paragraph 6 6 6 8 8\n"
	  "item 8 8 19 45 45\n"
	  "paragraph 19 19 19 21 21\n"
	  "plain-list 21 21 21 45 45\n"
	  "item 21 21 31 33 33\n"
	  "paragraph 31 31 31 33 33\n"
	  "item 33 33 38 45 45\n"
	  "paragraph 38 38 38 45 45\n"
	  "plain-list 47 47 47 56 56\n"
	  "item 47 47 49 56 56\n"
	  "paragraph 49 49 49 51 51\n"
	  "plain-list 51 51 51 56 56\n"
	  "item 51 51 54 56 56\n"
	  "paragraph 54 54 54 56 56\n" },
	{ "latex.org",
	  "\\begin{align*}\n"
	  "x\n"
	  "\\end{align*}\n"
	  "para\n"
	  "\\begin{x}\\end{x}\n",
	  "section 0 0 0 52 52\n"
	  "latex-environment 0 0 - - 30\n"
	  "paragraph 30 30 30 35 35\n"
	  "latex-environment 35 35 - - 52\n" },
	{ "listdyn.org",
	  "- a\n"
	  "  #+BEGIN:\n"
	  "  b\n"
	  "  #+END\n"
	  "- c\n",
	  "section 0 0 0 31 31\n"
	  "plain-list 0 0 0 31 31\n"
	  "item 0 0 2 27 27\n"
	  "paragraph 2 2 2 4 4\n"
	  "keyword 4 4 - - 15\n"
	  "paragraph 15 15 15 27 27\n"
	  "item 27 27 29 31 31\n"
	  "paragraph 29 29 29 31 31\n" },
	{ "outline.org",
	  "* a\n"
	  "\n"
	  "** b\n"
	  "*** c\n"
	  "* d",
	  "headline 0 0 5 16 16\n"
	  "+ 1 - - - \"a\" \n"
	  "headline 5 5 10 16 16\n"
	  "+ 2 - - - \"b\" \n"
	  "headline 10 10 - - 16\n"
	  "+ 3 - - - \"c\" \n"
	  "headline 16 16 - - 19\n"
	  "+ 1 - - - \"d\" \n" },
	{ "para.org",
	  "a\n"
	  "#+BEGIN_SRC\n"
	  "b\n"
	  "----- \n"
	  "-----x\n"
	  "%%(diary)\n",
	  "section 0 0 0 40 40\n"
	  "paragraph 0 0 0 16 16\n"
	  "horizontal-rule 16 16 - - 23\n"
	  "paragraph 23 23 23 30 30\n"
	  "diary-sexp 30 30 - - 40\n" },
	{ "prio.org",
	  "* TODO [#a] x\n"
	  "* foo [#B] bar :t:\n"
	  "** NEXT [#12] COMMENT y :a:b:\n"
	  "*** [#65] z\n",
	  "headline 0 0 - - 14\n"
	  "+ 1 TODO 97 - \"x\" \n"
	  "headline 14 14 33 75 75\n"
	  "+ 1 - 66 - \"bar\" t\n"
	  "headline 33 33 63 75 75\n"
	  "+ 2 NEXT 12 c \"y\" a:b\n"
	  "headline 63 63 - - 75\n"
	  "+ 3 - - - \"[#65] z\" \n" },
	{ "synth.org",
	  "#+title: Synthetic agenda file\n"
	  "\n"
	  "** NEXT Item 0\n"
	  "SCHEDULED: <2026-11-18 Mon .+1d> DEADLINE: <2026-12-19 Tue>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:12\n"
	  ":END:\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-14 Mon 09:00]--[2026-11-03 Mon 10:30] =>  1:30\n"
	  "- State \"DONE\"       from \"TODO\"       [2026-11-02 Mon 10:31]\n"
	  ":END:\n"
	  "See [[file:notes.org::*Item 0][item]] and https://example.com/0 and <2026-11-19 Wed 10:00>.\n"
	  "- one\n"
	  "- [ ] two\n"
	  "  1. nested\n"
	  "* Item 1\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-28 Mon 09:00]--[2026-11-05 Mon 10:30] =>  1:30\n"
	  "- State \"DONE\"       from \"TODO\"       [2026-11-02 Mon 10:31]\n"
	  ":END:\n"
	  "See [[file:notes.org::*Item 1][item]] and https://example.com/1 and <2026-11-10 Wed 10:00>.\n"
	  "**** NEXT Item 2\n"
	  "#+begin_src C\n"
	  "x = 1\n"
	  "#+end_src\n"
	  "** DONE Item 3\n"
	  "SCHEDULED: <2026-11-06 Mon> DEADLINE: <2026-12-08 Tue>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:41\n"
	  ":END:\n"
	  "- one\n"
	  "- [ ] two\n"
	  "  1. nested\n"
	  "** NEXT [#B] Item 4 :home:lean:\n"
	  "SCHEDULED: <2026-11-22 Mon .+1d> DEADLINE: <2026-12-18 Tue>\n"
	  "** TODO Item 5\n"
	  "SCHEDULED: <2026-11-22 Mon .+1d> DEADLINE: <2026-12-24 Tue>\n"
	  "See [[file:notes.org::*Item 5][item]] and https://example.com/5 and <2026-11-13 Wed 10:00>.\n"
	  "** TODO Item 6\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-10 Mon 09:00]--[2026-11-05 Mon 10:30] =>  1:30\n"
	  "- State \"DONE\"       from \"TODO\"       [2026-11-02 Mon 10:31]\n"
	  ":END:\n"
	  "* DONE Item 7\n"
	  "* NEXT [#A] Item 8\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:21\n"
	  ":END:\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-01 Mon 09:00]--[2026-11-05 Mon 10:30] =>  1:30\n"
	  "- State \"DONE\"       from \"TODO\"       [2026-11-02 Mon 10:31]\n"
	  ":END:\n"
	  "**** Item 9\n"
	  "** TODO [#A] Item 10 :work:\n"
	  "SCHEDULED: <2026-11-04 Mon> DEADLINE: <2026-12-02 Tue>\n"
	  ":PROPERTIES:\n"
	  ":EFFORT: 0:44\n"
	  ":END:\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-12 Mon 09:00]--[2026-11-20 Mon 10:30] =>  1:30\n",
	  "section 0 0 0 32 32\n"
	  "keyword 0 0 - - 32\n"
	  "headline 32 32 47 401 401\n"
	  "+ 2 NEXT - - \"Item 0\" \n"
	  "section 47 47 47 401 401\n"
	  "planning 47 47 - - 107\n"
	  "@ deadline active - 2026 12 19 - - 2026 12 19 - - - - - - - - - - 90 106\n"
	  "@ scheduled active - 2026 11 18 - - 2026 11 18 - - restart 1 day - - - - - 58 80\n"
	  "property-drawer 107 107 120 134 140\n"
	  "node-property 120 120 - - 134\n"
	  "drawer 140 140 150 275 281\n"
	  "clock 150 150 - - 213\n"
	  "@ clock inactive-range daterange 2026 11 14 9 0 2026 11 3 10 30 - - - - - - - - 157 204\n"
	  "= 1:30\n"
	  "plain-list 213 213 213 275 275\n"
	  "item 213 213 215 275 275\n"
	  "paragraph 215 215 215 275 275\n"
	  "paragraph 281 281 281 373 373\n"
	  "plain-list 373 373 373 401 401\n"
	  "item 373 373 375 379 379\n"
	  "paragraph 375 375 375 379 379\n"
	  "item 379 379 385 401 401\n"
	  "paragraph 385 385 385 389 389\n"
	  "plain-list 389 389 389 401 401\n"
	  "item 389 389 394 401 401\n"
	  "paragraph 394 394 394 401 401\n"
	  "headline 401 401 410 1236 1236\n"
	  "+ 1 - - - \"Item 1\" \n"
	  "section 410 410 410 643 643\n"
	  "drawer 410 410 420 545 551\n"
	  "clock 420 420 - - 483\n"
	  "@ clock inactive-range daterange 2026 11 28 9 0 2026 11 5 10 30 - - - - - - - - 427 474\n"
	  "= 1:30\n"
	  "plain-list 483 483 483 545 545\n"
	  "item 483 483 485 545 545\n"
	  "paragraph 485 485 485 545 545\n"
	  "paragraph 551 551 551 643 643\n"
	  "headline 643 643 660 690 690\n"
	  "+ 4 NEXT - - \"Item 2\" \n"
	  "section 660 660 660 690 690\n"
	  "src-block 660 660 - - 690\n"
	  "headline 690 690 705 821 821\n"
	  "+ 2 DONE - - \"Item 3\" \n"
	  "section 705 705 705 821 821\n"
	  "planning 705 705 - - 760\n"
	  "@ deadline active - 2026 12 8 - - 2026 12 8 - - - - - - - - - - 743 759\n"
	  "@ scheduled active - 2026 11 6 - - 2026 11 6 - - - - - - - - - - 716 733\n"
	  "property-drawer 760 760 773 787 793\n"
	  "node-property 773 773 - - 787\n"
	  "plain-list 793 793 793 821 821\n"
	  "item 793 793 795 799 799\n"
	  "paragraph 795 795 795 799 799\n"
	  "item 799 799 805 821 821\n"
	  "paragraph 805 805 805 809 809\n"
	  "plain-list 809 809 809 821 821\n"
	  "item 809 809 814 821 821\n"
	  "paragraph 814 814 814 821 821\n"
	  "headline 821 821 853 913 913\n"
	  "+ 2 NEXT 66 - \"Item 4\" home:lean\n"
	  "section 853 853 853 913 913\n"
	  "planning 853 853 - - 913\n"
	  "@ deadline active - 2026 12 18 - - 2026 12 18 - - - - - - - - - - 896 912\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day - - - - - 864 886\n"
	  "headline 913 913 928 1080 1080\n"
	  "+ 2 TODO - - \"Item 5\" \n"
	  "section 928 928 928 1080 1080\n"
	  "planning 928 928 - - 988\n"
	  "@ deadline active - 2026 12 24 - - 2026 12 24 - - - - - - - - - - 971 987\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day - - - - - 939 961\n"
	  "paragraph 988 988 988 1080 1080\n"
	  "headline 1080 1080 1095 1236 1236\n"
	  "+ 2 TODO - - \"Item 6\" \n"
	  "section 1095 1095 1095 1236 1236\n"
	  "drawer 1095 1095 1105 1230 1236\n"
	  "clock 1105 1105 - - 1168\n"
	  "@ clock inactive-range daterange 2026 11 10 9 0 2026 11 5 10 30 - - - - - - - - 1112 1159\n"
	  "= 1:30\n"
	  "plain-list 1168 1168 1168 1230 1230\n"
	  "item 1168 1168 1170 1230 1230\n"
	  "paragraph 1170 1170 1170 1230 1230\n"
	  "headline 1236 1236 - - 1250\n"
	  "+ 1 DONE - - \"Item 7\" \n"
	  "headline 1250 1250 1269 1644 1644\n"
	  "+ 1 NEXT 65 - \"Item 8\" \n"
	  "section 1269 1269 1269 1443 1443\n"
	  "property-drawer 1269 1269 1282 1296 1302\n"
	  "node-property 1282 1282 - - 1296\n"
	  "drawer 1302 1302 1312 1437 1443\n"
	  "clock 1312 1312 - - 1375\n"
	  "@ clock inactive-range daterange 2026 11 1 9 0 2026 11 5 10 30 - - - - - - - - 1319 1366\n"
	  "= 1:30\n"
	  "plain-list 1375 1375 1375 1437 1437\n"
	  "item 1375 1375 1377 1437 1437\n"
	  "paragraph 1377 1377 1377 1437 1437\n"
	  "headline 1443 1443 - - 1455\n"
	  "+ 4 - - - \"Item 9\" \n"
	  "headline 1455 1455 1483 1644 1644\n"
	  "+ 2 TODO 65 - \"Item 10\" work\n"
	  "section 1483 1483 1483 1644 1644\n"
	  "planning 1483 1483 - - 1538\n"
	  "@ deadline active - 2026 12 2 - - 2026 12 2 - - - - - - - - - - 1521 1537\n"
	  "@ scheduled active - 2026 11 4 - - 2026 11 4 - - - - - - - - - - 1494 1511\n"
	  "property-drawer 1538 1538 1551 1565 1571\n"
	  "node-property 1551 1551 - - 1565\n"
	  "paragraph 1571 1571 1571 1581 1581\n"
	  "clock 1581 1581 - - 1644\n"
	  "@ clock inactive-range daterange 2026 11 12 9 0 2026 11 20 10 30 - - - - - - - - 1588 1635\n"
	  "= 1:30\n" },
	{ "t1.org",
	  "#+NAME: data\n"
	  "| a | b |\n"
	  "| 1 | 2 |\n",
	  "section 0 0 0 33 33\n"
	  "table 0 13 13 33 33\n"
	  "table-row 13 13 14 22 23\n"
	  "table-row 23 23 24 32 33\n" },
	{ "t2.org",
	  "| a | b |\n"
	  "| 1 | 2 |\n",
	  "section 0 0 0 20 20\n"
	  "table 0 0 0 20 20\n"
	  "table-row 0 0 1 9 10\n"
	  "table-row 10 10 11 19 20\n" },
	{ "t3.org",
	  "#+CAPTION: cap\n"
	  "| a | b |\n",
	  "section 0 0 0 25 25\n"
	  "table 0 15 15 25 25\n"
	  "table-row 15 15 16 24 25\n" },
	{ "t4.org",
	  "#+TBLNAME: x\n"
	  "| a |\n",
	  "section 0 0 0 19 19\n"
	  "table 0 13 13 19 19\n"
	  "table-row 13 13 14 18 19\n" },
	{ "t5.org",
	  "#+NAME: s\n"
	  "#+begin_src sh\n"
	  "echo\n"
	  "#+end_src\n",
	  "section 0 0 0 40 40\n"
	  "src-block 0 10 - - 40\n" },
	{ "t6.org",
	  "#+ATTR_HTML: :width 3\n"
	  "| a |\n",
	  "section 0 0 0 28 28\n"
	  "table 0 22 22 28 28\n"
	  "table-row 22 22 23 27 28\n" },
	{ "table.org",
	  "#+NAME: t\n"
	  "| a | b |\n"
	  "|---+---|\n"
	  "|  1 |\n"
	  "#+TBLFM: $2=$1\n"
	  "\n"
	  "+--+--+\n"
	  "| x|  |\n"
	  "+--+--+\n",
	  "section 0 0 0 77 77\n"
	  "table 0 10 10 37 53\n"
	  "table-row 10 10 11 19 20\n"
	  "table-row 20 20 - - 30\n"
	  "table-row 30 30 31 36 37\n"
	  "table 53 53 - - 77\n" },
	{ "tags.org",
	  "* :a:\n"
	  "* x :a::b:\n"
	  "* y :@x_1:%:\n"
	  "*  \t\n"
	  "* COMMENT\n"
	  "* TODO\n",
	  "headline 0 0 - - 6\n"
	  "+ 1 - - - \"\" a\n"
	  "headline 6 6 - - 17\n"
	  "+ 1 - - - \"x\" a::b\n"
	  "headline 17 17 - - 30\n"
	  "+ 1 - - - \"y\" @x_1:%\n"
	  "headline 30 30 - - 35\n"
	  "+ 1 - - - \"\" \n"
	  "headline 35 35 - - 45\n"
	  "+ 1 - - c \"\" \n"
	  "headline 45 45 - - 52\n"
	  "+ 1 TODO - - \"\" \n" },
	{ "top.org",
	  "\n"
	  "\n"
	  "# c\n"
	  ":PROPERTIES:\n"
	  ":ID: x\n"
	  ":END:\n"
	  "* h\n"
	  "  SCHEDULED: <2026-11-22 Sun>\n"
	  "\n"
	  ":PROPERTIES:\n"
	  ":A: b\n"
	  ":END:\n",
	  "section 2 2 2 32 32\n"
	  "comment 2 2 - - 6\n"
	  "property-drawer 6 6 19 26 32\n"
	  "node-property 19 19 - - 26\n"
	  "headline 32 32 36 92 92\n"
	  "+ 1 - - - \"h\" \n"
	  "section 36 36 36 92 92\n"
	  "planning 36 36 - - 67\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - - - - - - - - - 49 65\n"
	  "property-drawer 67 67 80 86 92\n"
	  "node-property 80 80 - - 86\n" },
	{ "ts-clock.org",
	  "CLOCK: [2026-11-01 Sun 09:00 +1D --3W]\n"
	  "clock: [2026-11-01 Sun 9:00-10:00]--[2026-11-02 Mon 8:00] =>  23:00\n"
	  "CLOCK: => 1:00\n",
	  "section 0 0 0 122 122\n"
	  "clock 0 0 - - 39\n"
	  "@ clock inactive - 2026 11 1 9 0 2026 11 1 9 0 cumulate 1 year - - first 3 year 7 38\n"
	  "= running\n"
	  "clock 39 39 - - 107\n"
	  "@ clock inactive-range daterange 2026 11 1 9 0 2026 11 2 8 0 - - - - - - - - 46 97\n"
	  "= 23:00\n"
	  "clock 107 107 - - 122\n"
	  "= 1:00\n" },
	{ "ts-odd.org",
	  "* a\n"
	  "SCHEDULED: <2026-11-22 Sun +1D -2D]--[2026-11-23 9:00] xCLOSED:[2026-11-01 Mon. 123:45] DEADLINE:<2026-11-22 DEADLINE: x>\n"
	  "CLOCK: [2026-11-01 Sun 09:00 +1D --3W] =>  x y\n"
	  "CLOCK: <%%(a]b) 9:00>   => 0:10\f\n"
	  "CLOCK: [2026-11-01 Sun 9:00-10:00] =>   2:00  \n",
	  "headline 0 0 4 253 253\n"
	  "+ 1 - - - \"a\" \n"
	  "section 4 4 4 253 253\n"
	  "planning 4 4 - - 126\n"
	  "@ closed inactive - 2026 11 1 - - 2026 11 1 - - - - - - - - - - 67 92\n"
	  "@ scheduled active-range daterange 2026 11 22 - - 2026 11 23 9 0 - - - - - - - - 15 59\n"
	  "paragraph 126 126 126 253 253\n" },
	{ "ts-planning.org",
	  "* TODO a\n"
	  "SCHEDULED: <2026-11-22 Sun .+1d/3d> DEADLINE: <2026-11-22 Sun 9:00-10:30 -2d>\n"
	  ":LOGBOOK:\n"
	  "CLOCK: [2026-11-01 Sun 09:00]--[2026-11-01 Sun 10:30] =>  1:30\n"
	  "CLOCK: [2026-11-01 Sun 11:00]\n"
	  ":END:\n"
	  "* b\n"
	  "CLOSED: [2026-11-02 Mon 08:00] SCHEDULED: <%%(diary-float t 4 2) 9:00-10:00>\n",
	  "headline 0 0 9 196 196\n"
	  "+ 1 TODO - - \"a\" \n"
	  "section 9 9 9 196 196\n"
	  "planning 9 9 - - 87\n"
	  "@ deadline active-range timerange 2026 11 22 9 0 2026 11 22 10 30 - - - - - all 2 day 55 86\n"
	  "@ scheduled active - 2026 11 22 - - 2026 11 22 - - restart 1 day 3 day - - - 20 45\n"
	  "drawer 87 87 97 190 196\n"
	  "clock 97 97 - - 160\n"
	  "@ clock inactive-range daterange 2026 11 1 9 0 2026 11 1 10 30 - - - - - - - - 104 151\n"
	  "= 1:30\n"
	  "clock 160 160 - - 190\n"
	  "@ clock inactive - 2026 11 1 11 0 2026 11 1 11 0 - - - - - - - - 167 189\n"
	  "= running\n"
	  "headline 196 196 200 277 277\n"
	  "+ 1 - - - \"b\" \n"
	  "section 200 200 200 277 277\n"
	  "planning 200 200 - - 277\n"
	  "@ closed inactive - 2026 11 2 8 0 2026 11 2 8 0 - - - - - - - - 208 231\n"
	  "@ scheduled diary timerange - - - 9 0 - - - 10 0 - - - - - - - - 242 276\n" },
	{ "uni-drawer.org",
	  ":x·:\n"
	  "a\n"
	  ":END:\n"
	  ":x→:\n"
	  "b\n"
	  ":END:\n",
	  "section 0 0 0 29 29\n"
	  "drawer 0 0 6 8 14\n"
	  "paragraph 6 6 6 8 8\n"
	  "paragraph 14 14 14 29 29\n" },
	{ "uni-fn.org",
	  "[fn:x·] a\n"
	  "\n"
	  "[fn:xº] b\n",
	  "section 0 0 0 23 23\n"
	  "footnote-definition 0 0 9 23 23\n"
	  "paragraph 9 9 9 11 12\n"
	  "paragraph 12 12 12 23 23\n" },
	{ "uni-tags.org",
	  "* a :x²:\n"
	  "* b :xº:\n"
	  "* c :é·:\n",
	  "headline 0 0 - - 10\n"
	  "+ 1 - - - \"a :x²:\" \n"
	  "headline 10 10 - - 20\n"
	  "+ 1 - - - \"b\" xº\n"
	  "headline 20 20 - - 31\n"
	  "+ 1 - - - \"c :é·:\" \n" },
	{ "ws.org",
	  "  \n"
	  "\n"
	  "  ",
	  "section 4 4 4 6 6\n"
	  "paragraph 4 4 4 6 6\n" },
};

int main(void)
{
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		char *got = NULL;
		size_t len = 0, n = strlen(cases[i].text);
		FILE *o = open_memstream(&got, &len);
		CHECK(o && dump(o, cases[i].text, n) == 0);
		fclose(o);
		if (strcmp(got, cases[i].want)) fprintf(stderr, "%s:\n%s--- Org says:\n%s", cases[i].name, got, cases[i].want);
		CHECK(!strcmp(got, cases[i].want));
		free(got);
		check_org(cases[i].text, n);
	}
	size_t k = 1;
	OrgNode *e = org_parse("", 0, &k);
	CHECK(e && k == 0);
	free(e);
	printf("test_org: %zu texts agree with Org\n", sizeof cases / sizeof *cases);
	return 0;
}
