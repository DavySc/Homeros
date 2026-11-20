;;; -*- lexical-binding: t -*-
;; org-element-timestamp-parser at every position of each file, as in a
;; paragraph (case-fold-search t); printed as tests/org_drv.c prints one.
(require 'org-element)
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
(dolist (f command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
    (let ((org-inhibit-startup t)) (delay-mode-hooks (org-mode)))
    (princ (format "== %s\n" (file-name-nondirectory f)))
    (let ((case-fold-search t))
      (goto-char (point-min))
      (while (not (eobp))
        (let ((ts (condition-case err (org-element-timestamp-parser)
                    (error (princ (format "!error %d %S\n" (1- (position-bytes (point))) err)) nil))))
          (when ts (org-dump-ts "ts" ts)))
        (forward-char 1)))))
(setq command-line-args-left nil)
