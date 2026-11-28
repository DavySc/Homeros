;;; -*- lexical-binding: t -*-
;; Org's own display as the oracle for orgfx_conceal (package 16), with
;; Davy's settings (kittymax's kitty-org.el): org-pretty-entities,
;; org-hide-emphasis-markers, org-use-sub-superscripts '{}.  For each file,
;; "== name", then the runs after fontifying the whole buffer at once:
;; "c BEGIN END STRING" what compose-region shows there, "h BEGIN END"
;; invisible, "u BEGIN END" / "d BEGIN END" raised up / down.  Positions
;; are bytes from 0.
;; emacs -Q --batch -l tools/org_show.el FILE...
(require 'org)
(setq org-pretty-entities t org-hide-emphasis-markers t org-use-sub-superscripts '{})
;; Davy's choice (2026-11-21): the lattice and order symbols Org lacks, as
;; org-entities-user entries; src/org/entities.h is generated with them
(setq org-entities-user
      (mapcar (lambda (e) (list (car e) (concat "\\" (car e)) t (cdr e) (car e) (car e) (cdr e)))
              '(("top" . "⊤") ("bot" . "⊥") ("sqsubseteq" . "⊑") ("sqsubset" . "⊏") ("sqsupseteq" . "⊒")
                ("sqsupset" . "⊐") ("sqcup" . "⊔") ("sqcap" . "⊓") ("bigvee" . "⋁") ("bigwedge" . "⋀")
                ("subseteq" . "⊆") ("supseteq" . "⊇") ("mapsto" . "↦") ("models" . "⊨") ("vdash" . "⊢")
                ("implies" . "⟹") ("iff" . "⟺") ("mid" . "∣"))))
(load (expand-file-name "org_advice.el" (file-name-directory load-file-name)) nil t)
;; batch Emacs has no display to compose on: keep what Org asks for, one
;; cons a call, so that \Alpha\Alpha is two runs
(advice-add 'compose-region :before
            (lambda (a z &optional c &rest _) (put-text-property a z 'org-show-c (cons a c))))
(defun org-show-runs (kind prop val)
  (let ((p (point-min)))
    (while (< p (point-max))
      (let ((v (funcall val (get-text-property p prop)))
            (e (next-single-property-change p prop nil (point-max))))
        (when v
          (princ (format "%s %d %d%s\n" kind (1- (position-bytes p)) (1- (position-bytes e))
                         (if (stringp v) (concat " " v) ""))))
        (setq p e)))))
(dolist (file command-line-args-left)
  (with-temp-buffer
    (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents file))
    (org-mode)
    (princ (format "== %s\n" (file-name-nondirectory file)))
    (condition-case err
        (progn
          (font-lock-ensure)
          (org-show-runs "c" 'org-show-c #'cdr)
          (org-show-runs "h" 'invisible (lambda (v) (and v (not (eq v 'org-fold-outline)) t)))
          (let ((raise (lambda (v) (cadr (assq 'raise (and (consp v) (if (consp (car v)) v (list v))))))))
            (org-show-runs "u" 'display (lambda (v) (let ((r (funcall raise v))) (and r (> r 0)))))
            (org-show-runs "d" 'display (lambda (v) (let ((r (funcall raise v))) (and r (< r 0)))))))
      (error (princ (format "!error %S\n" err))))))
(setq command-line-args-left nil)
