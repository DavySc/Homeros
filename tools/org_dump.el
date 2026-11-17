;;; -*- lexical-binding: t -*-
;; Org's own parser, org-element, as the oracle for src/org (package 16):
;; for each file, "== name" and then every element in document order as
;; tests/org_drv.c prints it.  Positions are bytes from 0; "-" is none.
;; emacs -Q --batch -l tools/org_dump.el FILE...
(require 'org-element)
(setq org-todo-keywords '((sequence "TODO" "NEXT" "WAIT" "|" "DONE" "CANCELLED")))
(defun org-dump-pos (p) (if p (number-to-string (1- (position-bytes p))) "-"))
(defun org-dump-node (e)
  (princ (format "%s %s %s %s %s %s\n" (org-element-type e)
                 (org-dump-pos (org-element-begin e))
                 (org-dump-pos (org-element-post-affiliated e))
                 (org-dump-pos (org-element-contents-begin e))
                 (org-dump-pos (org-element-contents-end e))
                 (org-dump-pos (org-element-end e))))
  (when (eq (org-element-type e) 'headline)
    (princ (format "+ %d %s %s %s %S %s\n" (org-element-property :level e)
                   (or (org-element-property :todo-keyword e) "-")
                   (or (org-element-property :priority e) "-")
                   (if (org-element-property :commentedp e) "c" "-")
                   (org-element-property :raw-value e)
                   (mapconcat #'identity (org-element-property :tags e) ":"))))
  (dolist (c (org-element-contents e))
    (unless (stringp c) (org-dump-node c))))
(dolist (f command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
    (let ((org-inhibit-startup t)) (delay-mode-hooks (org-mode)))
    (princ (format "== %s\n" (file-name-nondirectory f)))
    ;; Org itself fails on some texts (a CAPTION[...] with a blank inside its
    ;; brackets, left without an element, errors in the keyword parser)
    (condition-case err
        (let ((tree (org-element-parse-buffer 'element)))
          (dolist (c (org-element-contents tree)) (org-dump-node c)))
      (error (princ (format "!error %S\n" err))))))
(setq command-line-args-left nil)
