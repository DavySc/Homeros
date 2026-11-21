;; PR 93 review: tools/org_todo.el with Org's questions answered yes
(advice-add 'y-or-n-p :override (lambda (&rest _) t))
(load (expand-file-name "tools/org_todo.el"))
