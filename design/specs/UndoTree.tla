------------------------------ MODULE UndoTree ------------------------------
(***************************************************************************)
(* Undo as a tree (Davy, 2026-11-17: "Undo must be a tree").  Behaviour   *)
(* follows Neovim 0.12, recorded in ref/undo/nvim-undo-tree.txt:           *)
(*   u      to the parent;  C-r  to the child last visited (so u then C-r *)
(*          is always a no-op);  an edit after u starts a new branch and  *)
(*          keeps the old one;  :undo N / g- / g+ jump to any state by     *)
(*          its number (g- = N-1, g+ = N+1, chronological, across branches)*)
(* Grouping (insert session = one node) is checked in Undo.tla; here one  *)
(* node is one edit.                                                       *)
(*                                                                         *)
(* Properties, stated before the mechanism:                                *)
(*  Exact     every move lands on exactly the text that node had when it  *)
(*            was made, and never applies an edit at a missing position.  *)
(*  NoLoss    every text that ever existed is still a node of the tree.   *)
(*  UndoRedo  C-r right after u returns to the node u left.               *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): nodes store one edit relative to their     *)
(* parent; a jump walks up to the common ancestor applying inverses, then *)
(* down applying edits, and records each child it walks into as that      *)
(* parent's last-visited child.  Broken variants, each must fail TLC:     *)
(*  "linear"     an edit after u discards the redo branch (plain linear    *)
(*               undo; violates NoLoss)                                    *)
(*  "firstchild" C-r follows the oldest child (violates UndoRedo)         *)
(*  "direct"     a jump applies only the target's own edit, without the   *)
(*               walk (violates Exact)                                     *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

CONSTANTS MaxNodes, Variant

VARIABLES doc, nodes, parent, edit, snap, last, cur, nextId, seen, undid, bad
vars == <<doc, nodes, parent, edit, snap, last, cur, nextId, seen, undid, bad>>

Valid(s, e) == IF e.op = "ins" THEN e.pos \in 1..Len(s) + 1 ELSE e.pos \in 1..Len(s)
Apply(s, e) ==
  IF e.op = "ins" THEN SubSeq(s, 1, e.pos - 1) \o <<e.ch>> \o SubSeq(s, e.pos, Len(s))
                  ELSE SubSeq(s, 1, e.pos - 1) \o SubSeq(s, e.pos + 1, Len(s))
Inverse(e) == [e EXCEPT !.op = IF e.op = "ins" THEN "del" ELSE "ins"]

\* <<ok, text>> after applying a list of edits
RECURSIVE Run(_, _)
Run(s, L) == IF L = <<>> THEN <<TRUE, s>>
             ELSE IF ~Valid(s, Head(L)) THEN <<FALSE, s>>
             ELSE Run(Apply(s, Head(L)), Tail(L))

\* Path from the root to n, root first: <<0, ..., n>>
RECURSIVE Path(_)
Path(n) == IF n = 0 THEN <<0>> ELSE Append(Path(parent[n]), n)

InPath(m, p) == \E i \in 1..Len(p) : p[i] = m
Lca(a, b) == LET pa == Path(a) pb == Path(b)
             IN  CHOOSE m \in nodes : InPath(m, pa) /\ InPath(m, pb)
                   /\ \A k \in nodes : (InPath(k, pa) /\ InPath(k, pb)) => Len(Path(k)) <= Len(Path(m))
\* Nodes strictly below `top` on the path to n, top-down
Below(top, n) == LET p == Path(n) IN SubSeq(p, Len(Path(top)) + 1, Len(p))
Rev(L) == [i \in 1..Len(L) |-> L[Len(L) - i + 1]]

Init == /\ doc = <<1, 2>> /\ nodes = {0} /\ parent = [n \in {0} |-> 0]
        /\ edit = [n \in {0} |-> [op |-> "ins", pos |-> 1, ch |-> 0]]
        /\ snap = [n \in {0} |-> <<1, 2>>] /\ last = [n \in {0} |-> 0]
        /\ cur = 0 /\ nextId = 3 /\ seen = {<<1, 2>>} /\ undid = 0 /\ bad = FALSE

Descendants(n) == {m \in nodes : m /= n /\ InPath(n, Path(m))}

NewEdit ==
  /\ nextId - 2 <= MaxNodes
  /\ \E e \in {[op |-> "ins", pos |-> p, ch |-> nextId] : p \in 1..Len(doc) + 1}
            \cup {[op |-> "del", pos |-> p, ch |-> doc[p]] : p \in 1..Len(doc)} :
       LET n    == nextId \* node numbers grow with time, like Vim's seq
           keep == IF Variant = "linear" THEN nodes \ Descendants(cur) ELSE nodes
       IN /\ doc' = Apply(doc, e)
          /\ nodes' = keep \cup {n}
          /\ parent' = [m \in keep \cup {n} |-> IF m = n THEN cur ELSE parent[m]]
          /\ edit' = [m \in keep \cup {n} |-> IF m = n THEN e ELSE edit[m]]
          /\ snap' = [m \in keep \cup {n} |-> IF m = n THEN Apply(doc, e) ELSE snap[m]]
          /\ last' = [m \in keep \cup {n} |-> IF m = cur THEN n ELSE IF m = n THEN n ELSE last[m]]
          /\ seen' = seen \cup {Apply(doc, e)}
          /\ cur' = n
  /\ nextId' = nextId + 1 /\ undid' = 0
  /\ UNCHANGED bad

Undo ==
  /\ cur /= 0
  /\ LET r == Run(doc, <<Inverse(edit[cur])>>) IN
     /\ doc' = r[2] /\ bad' = (bad \/ ~r[1] \/ r[2] /= snap[parent[cur]])
  /\ last' = [last EXCEPT ![parent[cur]] = cur]
  /\ cur' = parent[cur] /\ undid' = cur
  /\ UNCHANGED <<nodes, parent, edit, snap, nextId, seen>>

Children(n) == {m \in nodes \ {0} : parent[m] = n}

Redo ==
  /\ Children(cur) /= {}
  /\ LET c == IF Variant = "firstchild"
              THEN CHOOSE m \in Children(cur) : \A k \in Children(cur) : m <= k
              ELSE last[cur]
         r == Run(doc, <<edit[c]>>)
     IN /\ doc' = r[2]
        /\ bad' = (bad \/ ~r[1] \/ r[2] /= snap[c] \/ (undid /= 0 /\ c /= undid))
        /\ cur' = c
  /\ undid' = 0
  /\ UNCHANGED <<nodes, parent, edit, snap, last, nextId, seen>>

\* :undo N, and g- / g+ (N = cur - 1 / cur + 1 among existing nodes)
Goto(n) ==
  LET a    == Lca(cur, n)
      up   == Rev(Below(a, cur))
      down == Below(a, n)
      L    == IF Variant = "direct" THEN <<edit[n]>>
              ELSE [i \in 1..Len(up) |-> Inverse(edit[up[i]])] \o [i \in 1..Len(down) |-> edit[down[i]]]
      r    == Run(doc, L)
  IN /\ doc' = r[2] /\ bad' = (bad \/ ~r[1] \/ r[2] /= snap[n])
     /\ last' = [m \in nodes |-> IF \E i \in 1..Len(down) : down[i] /= 0 /\ parent[down[i]] = m
                                   THEN CHOOSE c \in nodes : \E i \in 1..Len(down) : down[i] = c /\ parent[c] = m /\ c /= 0
                                   ELSE last[m]]
     /\ cur' = n /\ undid' = 0
     /\ UNCHANGED <<nodes, parent, edit, snap, nextId, seen>>

Jump == \E n \in nodes : n /= cur /\ Goto(n)

Next == NewEdit \/ Undo \/ Redo \/ Jump \/ (bad /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars

Exact    == ~bad
NoLoss   == \A t \in seen : \E n \in nodes : snap[n] = t
=============================================================================
