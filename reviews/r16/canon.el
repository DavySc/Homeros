;;; -*- lexical-binding: t -*-
;; Reviewer's oracle for the package 16 comparison: Org's own parser on each
;; file, one line per element in document order, in a format neither build
;; wrote: "type begin post-affiliated contents-begin contents-end end"
;; (0-based bytes, "-" for none), and for a headline a second line
;; "+ level todo priority commented hex(raw-value) hex(tags joined by :)".
;; emacs -Q --batch -l canon.el FILE...
(require 'org-element)
(setq org-todo-keywords '((sequence "TODO" "NEXT" "WAIT" "|" "DONE" "CANCELLED")))
(defun c-pos (p) (if p (number-to-string (1- (position-bytes p))) "-"))
(defun c-hex (s) (mapconcat (lambda (b) (format "%02x" b)) (encode-coding-string (or s "") 'utf-8-unix) ""))
(defun c-node (e)
  (princ (format "%s %s %s %s %s %s\n" (org-element-type e)
                 (c-pos (org-element-begin e)) (c-pos (org-element-post-affiliated e))
                 (c-pos (org-element-contents-begin e)) (c-pos (org-element-contents-end e))
                 (c-pos (org-element-end e))))
  (when (eq (org-element-type e) 'headline)
    (princ (format "+ %d %s %s %s %s. %s.\n" (org-element-property :level e)
                   (or (org-element-property :todo-keyword e) "-")
                   (or (org-element-property :priority e) "-")
                   (if (org-element-property :commentedp e) "c" "-")
                   (c-hex (org-element-property :raw-value e))
                   (c-hex (mapconcat #'identity (org-element-property :tags e) ":")))))
  (dolist (c (org-element-contents e)) (unless (stringp c) (c-node c))))
(dolist (f command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
    (let ((org-inhibit-startup t)) (delay-mode-hooks (org-mode)))
    (princ (format "== %s\n" (file-name-nondirectory f)))
    (condition-case err
        (dolist (c (org-element-contents (org-element-parse-buffer 'element))) (c-node c))
      (error (princ (format "!error %S\n" err))))))
(setq command-line-args-left nil)
