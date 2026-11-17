-------------------------------- MODULE Undo --------------------------------
(***************************************************************************)
(* Linear undo/redo with grouping (an insert session, or a server         *)
(* WorkspaceEdit such as "format document", is one undo step).             *)
(*                                                                         *)
(* Property, stated before the mechanism: undo puts back exactly the text *)
(* that was there before the step, and redo puts back exactly the text    *)
(* that was there after it.  Each group carries ghost snapshots of the    *)
(* text before and after it; `bad` latches if undo or redo ever lands     *)
(* anywhere else, or tries an edit at a position that does not exist.     *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): undo first closes the open group; any new  *)
(* edit clears the redo stack.                                             *)
(* Broken variants, each must fail TLC:                                    *)
(*  "keepredo"  a new edit keeps the redo stack (redo replays edits on a   *)
(*              text they were not made for)                               *)
(*  "noclose"   undo pops the last closed group while an insert session   *)
(*              is still open                                              *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS MaxOps, Variant

VARIABLES doc, nextId, undo, redo, open, openBefore, ops, bad
vars == <<doc, nextId, undo, redo, open, openBefore, ops, bad>>

Valid(s, e) == IF e.op = "ins" THEN e.pos \in 1..Len(s) + 1 ELSE e.pos \in 1..Len(s)

Apply(s, e) ==
  IF e.op = "ins" THEN SubSeq(s, 1, e.pos - 1) \o <<e.ch>> \o SubSeq(s, e.pos, Len(s))
                  ELSE SubSeq(s, 1, e.pos - 1) \o SubSeq(s, e.pos + 1, Len(s))

Inverse(e) == IF e.op = "ins" THEN [op |-> "del", pos |-> e.pos, ch |-> e.ch]
                              ELSE [op |-> "ins", pos |-> e.pos, ch |-> e.ch]

Reverse(L) == [i \in 1..Len(L) |-> L[Len(L) - i + 1]]

\* Apply a list of edits; <<ok, text>>.  ok = FALSE if any position is invalid.
RECURSIVE Run(_, _)
Run(s, L) == IF L = <<>> THEN <<TRUE, s>>
             ELSE IF ~Valid(s, Head(L)) THEN <<FALSE, s>>
             ELSE Run(Apply(s, Head(L)), Tail(L))

Init == /\ doc = <<1, 2>> /\ nextId = 3 /\ undo = <<>> /\ redo = <<>>
        /\ open = <<>> /\ openBefore = <<>> /\ ops = 0 /\ bad = FALSE

Closed == IF open = <<>> THEN undo
          ELSE Append(undo, [edits |-> open, before |-> openBefore, after |-> doc])

\* A keystroke in an insert session (or a delete); the session stays open.
Key ==
  /\ ops < MaxOps
  /\ \E e \in {[op |-> "ins", pos |-> p, ch |-> nextId] : p \in 1..Len(doc) + 1}
            \cup {[op |-> "del", pos |-> p, ch |-> doc[p]] : p \in 1..Len(doc)} :
       /\ doc' = Apply(doc, e)
       /\ nextId' = IF e.op = "ins" THEN nextId + 1 ELSE nextId
       /\ openBefore' = IF open = <<>> THEN doc ELSE openBefore
       /\ open' = Append(open, e)
  /\ redo' = IF Variant = "keepredo" THEN redo ELSE <<>>
  /\ ops' = ops + 1
  /\ UNCHANGED <<undo, bad>>

\* Esc, or a cursor jump, ends the insert session.
CloseGroup ==
  /\ open /= <<>>
  /\ undo' = Closed /\ open' = <<>>
  /\ UNCHANGED <<doc, nextId, redo, openBefore, ops, bad>>

\* A server edit (e.g. format document) of two edits, applied as one group.
\* Its version already matched (LspSync); here only grouping matters.
ServerEdit ==
  /\ ops < MaxOps /\ Len(doc) >= 1
  /\ LET e1 == [op |-> "ins", pos |-> 1, ch |-> nextId]
         e2 == [op |-> "del", pos |-> Len(doc) + 1, ch |-> doc[Len(doc)]]
         d  == Apply(Apply(doc, e1), e2)
     IN /\ doc' = d
        /\ undo' = Append(Closed, [edits |-> <<e1, e2>>, before |-> doc, after |-> d])
  /\ open' = <<>> /\ nextId' = nextId + 1 /\ ops' = ops + 1
  /\ redo' = IF Variant = "keepredo" THEN redo ELSE <<>>
  /\ UNCHANGED <<openBefore, bad>>

Undo ==
  /\ LET u == IF Variant = "noclose" THEN undo ELSE Closed IN
     /\ u /= <<>>
     /\ LET g == u[Len(u)]
            r == Run(doc, [i \in 1..Len(g.edits) |-> Inverse(Reverse(g.edits)[i])])
        IN /\ doc' = r[2]
           /\ bad' = (bad \/ ~r[1] \/ r[2] /= g.before)
           /\ undo' = SubSeq(u, 1, Len(u) - 1)
           /\ redo' = Append(redo, g)
  /\ open' = IF Variant = "noclose" THEN open ELSE <<>>
  /\ UNCHANGED <<nextId, openBefore, ops>>

Redo ==
  /\ redo /= <<>>
  /\ LET g == redo[Len(redo)]
         r == Run(doc, g.edits)
     IN /\ doc' = r[2]
        /\ bad' = (bad \/ ~r[1] \/ r[2] /= g.after)
        /\ undo' = Append(Closed, g)
        /\ redo' = SubSeq(redo, 1, Len(redo) - 1)
  /\ open' = <<>>
  /\ UNCHANGED <<nextId, openBefore, ops>>

Next == Key \/ CloseGroup \/ ServerEdit \/ Undo \/ Redo \/ (bad /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars

UndoRedoExact == ~bad
=============================================================================
