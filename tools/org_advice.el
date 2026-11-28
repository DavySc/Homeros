;;; -*- lexical-binding: t -*-
;; the advice tools/org_faces.el and tools/org_show.el share: footnote
;; spans, and Org's own block-fontifier errors
;; Org's own parser fails on some texts (a CAPTION[...] with a blank inside
;; its brackets, left without an element), and its block fontifier hides the
;; error in a message and leaves the buffer half coloured: let it reach the
;; caller, which prints !error (seed 45599443883, t01403)
(advice-add 'org-fontify-meta-lines-and-blocks :override #'org-fontify-meta-lines-and-blocks-1)
;; org-activate-footnote-links returns add-text-properties' value: nil for a
;; reference inside an earlier one's span, and font-lock stops looking for
;; footnotes in the whole buffer there (a live Emacs only in its chunk);
;; src/org goes on, as for dates, so this does too
(advice-add 'org-activate-footnote-links :around
            (lambda (f lim) (let ((p (point))) (funcall f lim) (/= p (point)))))
;; a reference's span stops at the next headline, as in src/org (a scan
;; starts at a headline; a live Emacs drops a span's part in a later chunk)
(advice-add 'org-footnote-next-reference-or-definition :filter-return
            (lambda (r)
              (when (nth 2 r)
                (save-excursion
                  (goto-char (nth 1 r))
                  (when (re-search-forward org-outline-regexp-bol (nth 2 r) t)
                    (setf (nth 2 r) (match-beginning 0)))))
              r))
