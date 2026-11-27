(require 'org)
(load (expand-file-name "../new/org_footnote.el" (file-name-directory load-file-name)) nil t)
(with-temp-buffer
  (insert-file-contents (car command-line-args-left))
  (org-mode)
  (let ((debug-on-error t)) (font-lock-ensure)))
