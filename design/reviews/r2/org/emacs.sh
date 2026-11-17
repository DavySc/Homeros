#!/bin/bash
# Review runs for the Emacs-as-backend claims.  Needs emacs 31 on PATH and synthetic-agenda.org copied here as a.org.
t(){ s=$(date +%s.%N); "$@" >/dev/null 2>&1; e=$(date +%s.%N); echo "$(echo "$e - $s" | bc) s"; }
ex(){ emacs -Q --batch a.org --eval "(progn (require 'ox-html) (setq org-export-with-broken-links 'mark) (org-html-export-to-html))" </dev/null; }
echo -n "batch export: "; t ex; cp a.html a1.html; ex >/dev/null 2>&1
echo "lines that differ between two exports of the same file: $(diff a1.html a.html | grep -c '^<')"
emacs -Q --daemon=rev --eval "(progn (require 'ox-html) (require 'org-agenda))" >/dev/null 2>&1
c(){ emacsclient -s rev --eval "$1"; }
for i in 1 2 3; do echo -n "daemon export, default gc: "; t c "(with-current-buffer (find-file-noselect \"$PWD/a.org\") (let ((org-export-with-broken-links 'mark)) (org-html-export-to-html)))"; done
for i in 1 2 3; do echo -n "daemon export, gc-cons-threshold 100 MB: "; t c "(with-current-buffer (find-file-noselect \"$PWD/a.org\") (let ((gc-cons-threshold 100000000) (org-export-with-broken-links 'mark)) (org-html-export-to-html)))"; done
for i in 1 2 3; do echo -n "week agenda: "; t c "(let ((org-agenda-files (list \"$PWD/a.org\")) (org-agenda-span 'week)) (org-agenda-list))"; done
c "(kill-emacs)" 2>/dev/null
# Babel state does not survive one batch job per block:
printf '#+begin_src emacs-lisp\n(setq rev-x 41)\n#+end_src\n\n#+begin_src emacs-lisp\n(1+ rev-x)\n#+end_src\n' > s.org
B(){ echo "(progn (goto-char (point-min)) (re-search-forward \"begin_src\" nil t $1) (princ (condition-case e (org-babel-execute-src-block) (error (format \"ERROR %S\" e)))))"; }
E_(){ emacs -Q --batch s.org --eval "(progn (setq org-confirm-babel-evaluate nil) $1)" </dev/null 2>/dev/null; echo; }
echo -n "one Emacs, block 1 then 2: "; E_ "$(B 1) (princ \" then \") $(B 2)"
echo -n "separate batch job, block 2: "; E_ "$(B 2)"
