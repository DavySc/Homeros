;;; -*- lexical-binding: t -*-
;; Org's org-todo as the oracle for org_todo_step (package 16), with Davy's
;; settings (kittymax's kitty-org.el): for each FILE, the command in
;; FILE.cmd, "POS DIRS" (POS a byte from 0, DIRS a string of r and l, for
;; org-todo 'right and 'left at POS), then "== name" and the buffer after
;; each step, each followed by "--".  A note a state asks for (@) is
;; stored empty at once, as C-c C-c on the note buffer.  The clock stands
;; at 2026-11-19 17:30; run with TZ=UTC.
;; emacs -Q --batch -l tools/org_todo.el FILE...
(require 'org)
(setq org-todo-keywords '((sequence "TODO(t)" "NEXT(n)" "WAIT(w@)" "|" "DONE(d!)" "CANCELLED(c@)"))
      org-log-done 'time org-log-into-drawer t org-enforce-todo-dependencies t
      org-auto-align-tags nil org-tags-column 0)
(defconst org-todo-now (encode-time '(0 30 17 19 11 2026 nil nil 0)))
;; nil is now to Emacs's time functions in C too, which current-time does not reach
(dolist (f '((float-time 0) (decode-time 0) (format-time-string 1) (time-less-p 0 1) (time-subtract 0 1)
             (time-add 0 1) (time-convert 0) (time-equal-p 0 1)))
  (advice-add (car f) :filter-args
              (let ((ks (cdr f)))
                (lambda (args)
                  (let ((a (copy-sequence args)))
                    (dolist (k ks a)
                      (when (and (< k (length a)) (null (nth k a))) (setf (nth k a) org-todo-now))))))))
(defun org-todo-step (dir)
  (org-todo dir)
  (when (memq 'org-add-log-note post-command-hook)
    (let ((this-command org-log-note-this-command)) (org-add-log-note))
    (when (get-buffer "*Org Note*") (with-current-buffer "*Org Note*" (org-store-log-note)))))
(dolist (f command-line-args-left)
  (let ((cmd (with-temp-buffer (insert-file-contents (concat f ".cmd")) (split-string (buffer-string)))))
    (with-temp-buffer
      (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
      (let ((org-inhibit-startup t)) (delay-mode-hooks (org-mode)))
      (princ (format "== %s\n" (file-name-nondirectory f)))
      (cl-letf (((symbol-function 'current-time) (lambda () org-todo-now))
                ((symbol-function 'message) #'ignore))
        (catch 'stop   ; Org stops at an error: no more steps
          (dolist (d (string-to-list (cadr cmd)))
            (goto-char (or (byte-to-position (1+ (string-to-number (car cmd)))) (point-max)))
            (condition-case err
                (progn (org-todo-step (if (eq d ?r) 'right 'left))
                       (princ (buffer-string)) (princ "--\n"))
              (error (princ (format "!error %S\n" err)) (throw 'stop nil)))))))))
(setq command-line-args-left nil)
