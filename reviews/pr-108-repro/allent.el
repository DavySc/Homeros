(load-file "/home/claude/p108/tools/org_show.el")
(require 'org-entities)
(with-temp-file "/tmp/claude-0/s/dir2/all.org"
  (dolist (e (append org-entities-user org-entities))
    (when (consp e) (insert "\\" (car e) " " "\\" (car e) "{}x x^{\\" (car e) "}\n"))))
