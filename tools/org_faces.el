;;; -*- lexical-binding: t -*-
;; Org's own font-lock as the oracle for src/org's colours (package 16): for
;; each file, "== name" and then "begin end face" for every run of one top
;; face after fontifying the whole buffer at once.  Positions are bytes from
;; 0.  Off until src/org colours them: the objects (emphasis, targets,
;; macros, inline source blocks, citations); inside a source block the
;; language's own colours (org-src-fontify-natively) count as org-block.
;; emacs -Q --batch -l tools/org_faces.el FILE...
(require 'org)
(setq org-todo-keywords '((sequence "TODO" "NEXT" "WAIT" "|" "DONE" "CANCELLED"))
      org-highlight-links '(bracket angle plain tag date footnote)
      org-fontify-emphasized-text nil)
(load (expand-file-name "org_footnote.el" (file-name-directory load-file-name)) nil t)
(defun org-faces-top (p)
  (let ((f (get-text-property p 'face)))
    (while (and (consp f) (not (keywordp (car f)))) (setq f (car f)))
    (when (keywordp (car-safe f))
      (setq f (plist-get f :inherit))
      (while (consp f) (setq f (car f))))
    (if (and f (get-text-property p 'src-block)) 'org-block f)))
(dolist (file command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents file))
    (org-mode)
    (setq org-font-lock-keywords
          (seq-remove (lambda (k) (member (car-safe k) (list org-radio-target-regexp org-target-regexp
                                                             'org-fontify-macros 'org-fontify-inline-src-blocks 'org-cite-activate)))
                      org-font-lock-keywords))
    (princ (format "== %s\n" (file-name-nondirectory file)))
    ;; Org errs on a DONE headline with no title ("No match 2 in highlight")
    (condition-case err
        (progn
          (font-lock-ensure)
          (let ((p (point-min)))
            (while (< p (point-max))
              (let ((f (org-faces-top p)) (e (1+ p)))
                (while (and (< e (point-max)) (eq (org-faces-top e) f)) (setq e (1+ e)))
                (when f (princ (format "%d %d %s\n" (1- (position-bytes p)) (1- (position-bytes e)) f)))
                (setq p e)))))
      (error (princ (format "!error %S\n" err))))))
(setq command-line-args-left nil)
