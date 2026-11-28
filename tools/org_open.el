;;; -*- lexical-binding: t -*-
;; Org's org-open-at-point as the oracle for org_open (package 16), with
;; Davy's settings (kittymax's kitty-org.el: SPC o o, and RET on a link).
;; For each FILE, FILE.cmd holds positions (bytes from 0); prints "== name"
;; and for each position one line: "POS goto P" where point ends (P a byte
;; from 0; P is POS when Org does nothing), "POS file PATH" for a file link
;; (with ::SEARCH), "POS link TYPE PATH" for any other link type (http,
;; mailto, ...), "POS error MESSAGE", or the kind of what Org would
;; open instead: "offer" (a menu of the entry's links), "agenda" (a date),
;; "tags", "result" (a source block's), "cite", "new" (a new footnote, on the
;; blanks after a reference), "menu" (the footnote menu).  Org's question "create this
;; as a new heading?" is answered no.
;; emacs -Q --batch -l tools/org_open.el FILE...
(require 'org)
(setq org-todo-keywords '((sequence "TODO" "NEXT" "WAIT" "|" "DONE" "CANCELLED")))
(advice-add 'yes-or-no-p :override (lambda (&rest _) nil))
(dolist (k '((org-offer-links-in-entry . "offer") (read-char-exclusive . "menu") (org-footnote-new . "new") (org-follow-timestamp-link . "agenda") (org-tags-view . "tags")
             (org-babel-open-src-block-result . "result") (org-cite-follow . "cite")))
  (advice-add (car k) :override (lambda (&rest _) (throw 'open (cdr k)))))
(advice-add 'org-link-open-as-file :override (lambda (path &rest _) (throw 'open (concat "file " path))))
(advice-add 'org-link-open :around
            (lambda (f link &rest r)
              (let ((type (org-element-property :type link)))
                (if (member type '("file" "coderef" "custom-id" "fuzzy" "radio"))
                    (apply f link r)
                  (throw 'open (format "link %s %s" type (org-element-property :path link)))))))
(dolist (f command-line-args-left)
  (let ((cmd (with-temp-buffer (insert-file-contents (concat f ".cmd")) (mapcar #'string-to-number (split-string (buffer-string))))))
    (with-temp-buffer
      (let ((coding-system-for-read 'utf-8-unix)) (insert-file-contents f))
      (org-mode)   ; with its startup: the radio targets' regexp
      (princ (format "== %s\n" (file-name-nondirectory f)))
      (cl-letf (((symbol-function 'message) #'ignore))
        (dolist (p cmd)
          (goto-char (or (byte-to-position (1+ p)) (point-max)))
          (princ (format "%d %s\n" p
                         (catch 'open
                           (condition-case err
                               (progn (org-open-at-point) (format "goto %d" (1- (position-bytes (point)))))
                             (error (concat "error " (error-message-string err))))))))))))
(setq command-line-args-left nil)
