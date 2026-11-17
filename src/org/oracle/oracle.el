;;; -*- lexical-binding: t -*-
;; The oracle: what Org's own parser (org-element, Emacs 31.1) makes of each
;; file, one line per element in document order, positions as 0-based byte
;; offsets.  dump.c prints the same from src/org/; compare.sh diffs them.
;; emacs -Q --batch -l oracle.el FILE...
(require 'org-element)
(setq org-todo-keywords '((sequence "TODO(t)" "NEXT(n)" "WAIT(w@)" "|" "DONE(d!)" "CANCELLED(c@)")))
(defun o-pos (p) (if p (number-to-string (1- (position-bytes p))) "-"))
(defun o-num (n) (if n (format "%s" n) "-"))
(defun o-str (s)
  (if (not s) "-"
    (concat "\"" (replace-regexp-in-string "[\\\"\n]" (lambda (m) (if (equal m "\n") "\\n" (concat "\\" m))) s t t) "\"")))
(defun o-ts (ts)
  (if (not ts) "-"
    (let ((p (lambda (k) (org-element-property k ts))))
      (mapconcat
       #'identity
       (list (pcase (funcall p :type) ('active "a") ('active-range "A") ('inactive "i") ('inactive-range "I") ('diary "d"))
             (pcase (funcall p :range-type) ('daterange "d") ('timerange "t") (_ "-"))
             (o-pos (org-element-begin ts)) (o-pos (org-element-end ts))
             (o-num (funcall p :year-start)) (o-num (funcall p :month-start)) (o-num (funcall p :day-start))
             (o-num (funcall p :hour-start)) (o-num (funcall p :minute-start))
             (o-num (funcall p :year-end)) (o-num (funcall p :month-end)) (o-num (funcall p :day-end))
             (o-num (funcall p :hour-end)) (o-num (funcall p :minute-end))
             (pcase (funcall p :repeater-type) ('cumulate "+") ('catch-up "#") ('restart ".") (_ "-"))
             (o-num (funcall p :repeater-value)) (o-unit (funcall p :repeater-unit))
             (o-num (funcall p :repeater-deadline-value)) (o-unit (funcall p :repeater-deadline-unit))
             (pcase (funcall p :warning-type) ('all "a") ('first "f") (_ "-"))
             (o-num (funcall p :warning-value)) (o-unit (funcall p :warning-unit)))
       ","))))
(defun o-unit (u) (if u (substring (symbol-name u) 0 1) "-"))
(defun o-el (e)
  (let ((p (lambda (k) (org-element-property k e))))
    (princ (format "%s %s %s %s %s %s" (org-element-type e)
                   (o-pos (org-element-begin e)) (o-pos (org-element-end e))
                   (o-pos (org-element-post-affiliated e))
                   (o-pos (org-element-contents-begin e)) (o-pos (org-element-contents-end e))))
    (pcase (org-element-type e)
      ('headline
       (princ (format " L%s T%s P%s C%s R%s G%s" (funcall p :true-level) (or (funcall p :todo-keyword) "-")
                      (o-num (funcall p :priority)) (if (funcall p :commentedp) 1 0)
                      (o-str (funcall p :raw-value)) (o-str (mapconcat #'identity (funcall p :tags) ":")))))
      ('planning
       (princ (format " %s %s %s" (o-ts (funcall p :closed)) (o-ts (funcall p :deadline)) (o-ts (funcall p :scheduled)))))
      ('clock
       (princ (format " %s %s" (o-ts (funcall p :value)) (o-str (funcall p :duration))))))
    (terpri)))
(dolist (f command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
    (let ((org-inhibit-startup t)) (delay-mode-hooks (org-mode)))
    (princ (format "== %s\n" f))
    (org-element-map (org-element-parse-buffer 'element) org-element-all-elements #'o-el)))
(setq command-line-args-left nil)
