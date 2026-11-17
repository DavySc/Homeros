------------------------------ MODULE LspSync_dropstale ------------------------------
(***************************************************************************)
(* Editor <-> language server document sync, with async diagnostics and   *)
(* a goal/hover request (Lean's $/lean/plainGoal works the same way).      *)
(*                                                                         *)
(* Property, stated before the mechanism: anything the editor displays     *)
(* from the server points at the character the server meant.  Characters  *)
(* carry unique ids, and each server result carries the id it meant as a   *)
(* ghost field, so "the right character" is checkable.                     *)
(*                                                                         *)
(* Mechanism (Variant = "ok"):                                             *)
(*  - every local edit bumps the version, is appended to an edit log and   *)
(*    sent as didChange on a FIFO pipe (stdio is FIFO);                    *)
(*  - shown annotations are shifted through each local edit;               *)
(*  - a diagnostic for version v is mapped through the logged edits after  *)
(*    v, then the log is pruned up to v (diagnostics arrive in version     *)
(*    order, so nothing older is needed again);                            *)
(*  - a request reply is shown only if the document has not changed since *)
(*    the request was sent; otherwise it is dropped and re-requested.      *)
(*                                                                         *)
(* Broken variants, each must fail TLC:                                    *)
(*  "nomap"       diagnostics are shown at their raw positions             *)
(*  "prunesend"   the edit log is dropped once didChange is sent           *)
(*  "mapreply"    replies are mapped from the request version instead of   *)
(*                dropped; wrong because the server may answer on a newer  *)
(*                snapshot than the one the request was made on            *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS MaxEdits, Variant

VARIABLES doc, ver, nextId, log, c2s, sdoc, sver, spend, s2c,
          shown, hasReq, diagVer, pub

vars == <<doc, ver, nextId, log, c2s, sdoc, sver, spend, s2c, shown, hasReq, diagVer, pub>>

Apply(s, op, p, ch) ==
  IF op = "ins" THEN SubSeq(s, 1, p - 1) \o <<ch>> \o SubSeq(s, p, Len(s))
                ELSE SubSeq(s, 1, p - 1) \o SubSeq(s, p + 1, Len(s))

\* Position q (0 = gone) after edit e.
MapPos(q, e) ==
  IF q = 0 THEN 0
  ELSE IF e.op = "ins" THEN (IF q >= e.pos THEN q + 1 ELSE q)
  ELSE IF q = e.pos THEN 0 ELSE IF q > e.pos THEN q - 1 ELSE q

RECURSIVE MapSeq(_, _)
MapSeq(q, L) == IF L = <<>> THEN q ELSE MapSeq(MapPos(q, Head(L)), Tail(L))

After(v) == SelectSeq(log, LAMBDA e : e.v > v)

Ann(k, q, g) == IF q = 0 THEN {} ELSE {[k |-> k, pos |-> q, ghost |-> g]}

Init ==
  /\ doc = <<1, 2>> /\ nextId = 3 /\ ver = 0 /\ log = <<>>
  /\ c2s = <<>> /\ sdoc = <<1, 2>> /\ sver = 0 /\ spend = {} /\ s2c = <<>>
  /\ shown = {} /\ hasReq = FALSE /\ diagVer = 0 /\ pub = TRUE

---------------------------------------------------------------------------
\* Editor actions

DoEdit(op, p, ch) ==
  LET e == [v |-> ver + 1, op |-> op, pos |-> p, ch |-> ch] IN
  /\ doc' = Apply(doc, op, p, ch)
  /\ ver' = ver + 1
  /\ log' = IF Variant \in {"prunesend","dropstale"} THEN <<>> ELSE Append(log, e)
  /\ c2s' = Append(c2s, [t |-> "change"] @@ e)
  /\ shown' = UNION {Ann(a.k, MapPos(a.pos, e), a.ghost) : a \in shown}

Edit ==
  /\ ver < MaxEdits
  /\ \/ \E p \in 1..Len(doc) + 1 : DoEdit("ins", p, nextId) /\ nextId' = nextId + 1
     \/ \E p \in 1..Len(doc) : DoEdit("del", p, 0) /\ UNCHANGED nextId
  /\ UNCHANGED <<sdoc, sver, spend, s2c, hasReq, diagVer, pub>>

Request ==
  /\ ~hasReq
  /\ hasReq' = TRUE
  /\ c2s' = Append(c2s, [t |-> "req", v |-> ver])
  /\ UNCHANGED <<doc, ver, nextId, log, sdoc, sver, spend, s2c, shown, diagVer, pub>>

RecvDiag ==
  /\ s2c /= <<>> /\ Head(s2c).t = "diag"
  /\ LET m == Head(s2c)
         q == IF Variant = "nomap" THEN m.pos ELSE MapSeq(m.pos, After(m.v))
     IN /\ shown' = IF Variant = "dropstale" /\ m.v /= ver THEN shown
                   ELSE {a \in shown : a.k = "goal"} \cup Ann("diag", q, m.ghost)
        /\ diagVer' = m.v
        /\ log' = After(m.v)
  /\ s2c' = Tail(s2c)
  /\ UNCHANGED <<doc, ver, nextId, c2s, sdoc, sver, spend, hasReq, pub>>

RecvReply ==
  /\ s2c /= <<>> /\ Head(s2c).t = "reply"
  /\ LET m == Head(s2c)
         q == IF Variant = "mapreply" THEN MapSeq(m.pos, After(m.rv))
              ELSE IF m.rv = ver THEN m.pos ELSE 0
     IN shown' = {a \in shown : a.k = "diag"} \cup Ann("goal", q, m.ghost)
  /\ hasReq' = FALSE
  /\ s2c' = Tail(s2c)
  /\ UNCHANGED <<doc, ver, nextId, log, c2s, sdoc, sver, spend, diagVer, pub>>

---------------------------------------------------------------------------
\* Server actions

\* Apply a didChange.  Elaboration takes time: diagnostics for the new
\* version are published later, and a version superseded before it is
\* published may never get diagnostics (Lean cancels stale elaboration).
SrvChange ==
  /\ c2s /= <<>> /\ Head(c2s).t = "change"
  /\ LET m == Head(c2s) IN
       /\ sdoc' = Apply(sdoc, m.op, m.pos, m.ch) /\ sver' = m.v
  /\ pub' = FALSE
  /\ c2s' = Tail(c2s)
  /\ UNCHANGED <<doc, ver, nextId, log, spend, s2c, shown, hasReq, diagVer>>

\* Publish one diagnostic at any position (none if the file is empty).
SrvPublish ==
  /\ ~pub /\ pub' = TRUE
  /\ \/ Len(sdoc) = 0 /\ s2c' = Append(s2c, [t |-> "diag", v |-> sver, pos |-> 0, ghost |-> 0])
     \/ \E p \in 1..Len(sdoc) :
          s2c' = Append(s2c, [t |-> "diag", v |-> sver, pos |-> p, ghost |-> sdoc[p]])
  /\ UNCHANGED <<doc, ver, nextId, log, c2s, sdoc, sver, spend, shown, hasReq, diagVer>>

\* Requests may be parked and answered later, after more didChanges.
SrvTakeReq ==
  /\ c2s /= <<>> /\ Head(c2s).t = "req"
  /\ spend' = spend \cup {Head(c2s).v}
  /\ c2s' = Tail(c2s)
  /\ UNCHANGED <<doc, ver, nextId, log, sdoc, sver, s2c, shown, hasReq, diagVer, pub>>

SrvAnswer ==
  /\ \E rv \in spend :
       /\ spend' = spend \ {rv}
       /\ \/ Len(sdoc) = 0 /\ s2c' = Append(s2c, [t |-> "reply", rv |-> rv, pos |-> 0, ghost |-> 0])
          \/ \E p \in 1..Len(sdoc) :
               s2c' = Append(s2c, [t |-> "reply", rv |-> rv, pos |-> p, ghost |-> sdoc[p]])
  /\ UNCHANGED <<doc, ver, nextId, log, c2s, sdoc, sver, shown, hasReq, diagVer, pub>>

Next == Edit \/ Request \/ RecvDiag \/ RecvReply \/ SrvChange \/ SrvPublish \/ SrvTakeReq \/ SrvAnswer

Spec == Init /\ [][Next]_vars
        /\ WF_vars(RecvDiag) /\ WF_vars(RecvReply) /\ WF_vars(SrvChange) /\ WF_vars(SrvPublish)
        /\ WF_vars(SrvTakeReq) /\ WF_vars(SrvAnswer)

---------------------------------------------------------------------------
TypeOK == ver \in 0..MaxEdits /\ sver <= ver /\ Len(c2s) <= MaxEdits + 1

\* The property: every shown annotation points at the character the server meant.
Correct == \A a \in shown : a.pos \in 1..Len(doc) /\ doc[a.pos] = a.ghost

\* Once editing stops, the editor ends up showing diagnostics for the
\* version it holds (the server is never left behind).
CatchUp == <>[](diagVer = ver)
=============================================================================
