;;; -*- lexical-binding: t -*-
;; Org's own parser, org-element, as the oracle for src/org (package 16):
;; for each file, "== name", every element in document order as
;; tests/org_drv.c prints it, then org-element-timestamp-parser at every
;; character, as in a paragraph (case-fold-search t).  Positions are bytes
;; from 0; "-" is none.
;; emacs -Q --batch -l tools/org_dump.el FILE...
(require 'org-element)
(setq org-todo-keywords '((sequence "TODO" "NEXT" "WAIT" "|" "DONE" "CANCELLED")))
(defun org-dump-pos (p) (if p (number-to-string (1- (position-bytes p))) "-"))
(defun org-dump-v (v) (if v (format "%s" v) "-"))
(defun org-dump-ts (k ts)
  (when ts
    (princ (format "@ %s %s %s" k (org-element-property :type ts)
                   (org-dump-v (org-element-property :range-type ts))))
    (dolist (p '(:year-start :month-start :day-start :hour-start :minute-start
                 :year-end :month-end :day-end :hour-end :minute-end
                 :repeater-type :repeater-value :repeater-unit
                 :repeater-deadline-value :repeater-deadline-unit
                 :warning-type :warning-value :warning-unit))
      (princ (format " %s" (org-dump-v (org-element-property p ts)))))
    (princ (format " %s %s\n" (org-dump-pos (org-element-begin ts)) (org-dump-pos (org-element-end ts))))))
(defun org-dump-node (e)
  (princ (format "%s %s %s %s %s %s\n" (org-element-type e)
                 (org-dump-pos (org-element-begin e))
                 (org-dump-pos (org-element-post-affiliated e))
                 (org-dump-pos (org-element-contents-begin e))
                 (org-dump-pos (org-element-contents-end e))
                 (org-dump-pos (org-element-end e))))
  (when (eq (org-element-type e) 'planning)
    (org-dump-ts "closed" (org-element-property :closed e))
    (org-dump-ts "deadline" (org-element-property :deadline e))
    (org-dump-ts "scheduled" (org-element-property :scheduled e)))
  (when (eq (org-element-type e) 'clock)
    (org-dump-ts "clock" (org-element-property :value e))
    (princ (format "= %s\n" (or (org-element-property :duration e) "running"))))
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
        (let ((tree (org-element-parse-buffer 'element)) (case-fold-search t))
          (dolist (c (org-element-contents tree)) (org-dump-node c))
          (goto-char (point-min))
          (while (not (eobp))
            (org-dump-ts "ts" (org-element-timestamp-parser))
            (forward-char 1)))
      (error (princ (format "!error %S\n" err))))))
(setq command-line-args-left nil)
