------------------------------- MODULE SqlRun -------------------------------
(***************************************************************************)
(* Package 22: a SQL Server connection running batches and cancelling     *)
(* them (src/sql/tds.c).  One TCP stream each way, in order.  The client   *)
(* sends B batches one after the other and may cancel the running one; the *)
(* server sends R rows and a final DONE per batch.  On an attention the    *)
(* server stops and answers with a DONE that has DONE_ATTN, also when the  *)
(* batch had already ended before it read the attention ([MS-TDS] 3.2.5.1: *)
(* the attention always gets its acknowledgement).                         *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  EndsOnce      every run ends at most once (the editor runs the next   *)
(*                batch only after the end);                               *)
(*  NoSilentLoss  a run that ends uncancelled showed all R of its rows;    *)
(*  OwnRows       a run shows only its own batch's rows;                   *)
(*  Ends          every run ends.                                          *)
(*                                                                         *)
(* Mechanism (Variant = "ok", tds_cancel and token()'s DONE case): cancel  *)
(* only while busy; after a cancel, rows are read and dropped, and only    *)
(* the DONE with DONE_ATTN ends the run.                                   *)
(* Broken variants, must fail TLC:                                         *)
(*  "firstdone"  the cancelled run ends at the first final DONE: the       *)
(*               server's acknowledgement then ends the next run early     *)
(*  "noguard"    a cancel is sent while no batch runs: its acknowledgement *)
(*               ends a run that had already ended                         *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS B, R, Variant

VARIABLES toS, toC, srun, sleft, sb, cst, cur, attns, shown, ended, cancelled, foreign
vars == <<toS, toC, srun, sleft, sb, cst, cur, attns, shown, ended, cancelled, foreign>>
Runs == 1..B

Init == toS = <<>> /\ toC = <<>> /\ srun = FALSE /\ sleft = 0 /\ sb = 0
        /\ cst = "ready" /\ cur = 0 /\ attns = 0
        /\ shown = [b \in Runs |-> 0] /\ ended = [b \in Runs |-> 0]
        /\ cancelled = [b \in Runs |-> FALSE] /\ foreign = FALSE

(* the client: tds_batch, tds_cancel, and token() on each reply token *)
CBatch == cst = "ready" /\ cur < B /\ cur' = cur + 1 /\ cst' = "busy" /\ toS' = Append(toS, "batch")
          /\ UNCHANGED <<toC, srun, sleft, sb, attns, shown, ended, cancelled, foreign>>
CCancel == (cst = "busy" \/ (Variant = "noguard" /\ cst = "ready" /\ cur > 0)) /\ attns < B
           /\ cst' = "cancel" /\ attns' = attns + 1 /\ toS' = Append(toS, "attn")
           /\ UNCHANGED <<toC, srun, sleft, sb, cur, shown, ended, cancelled, foreign>>
End(c) == cst' = "ready" /\ ended' = [ended EXCEPT ![cur] = @ + 1] /\ cancelled' = [cancelled EXCEPT ![cur] = c]
CRecv == toC # <<>> /\ toC' = Tail(toC) /\ UNCHANGED <<toS, srun, sleft, sb, cur, attns>>
         /\ LET m == Head(toC) IN
            \/ m.k = "row" /\ cst = "busy" /\ shown' = [shown EXCEPT ![cur] = @ + 1] /\ foreign' = (foreign \/ m.b # cur)
               /\ UNCHANGED <<cst, ended, cancelled>>
            \/ m.k = "row" /\ cst # "busy" /\ UNCHANGED <<cst, shown, ended, cancelled, foreign>>
            \/ m.k = "done" /\ cst = "busy" /\ End(FALSE) /\ UNCHANGED <<shown, foreign>>
            \/ m.k = "done" /\ cst = "cancel" /\ Variant = "firstdone" /\ End(TRUE) /\ UNCHANGED <<shown, foreign>>
            \/ m.k = "done" /\ (cst = "ready" \/ (cst = "cancel" /\ Variant # "firstdone"))
               /\ UNCHANGED <<cst, shown, ended, cancelled, foreign>>
            (* tds.c: any final DONE ends a busy run; DONE_ATTN ends a cancelled one *)
            \/ m.k = "attn" /\ cst # "ready" /\ End(cst = "cancel") /\ UNCHANGED <<shown, foreign>>
            \/ m.k = "attn" /\ cst = "ready" /\ UNCHANGED <<cst, shown, ended, cancelled, foreign>>

(* the server: one request at a time; an attention stops it *)
SBatch == toS # <<>> /\ Head(toS) = "batch" /\ ~srun /\ toS' = Tail(toS)
          /\ srun' = TRUE /\ sleft' = R /\ sb' = sb + 1 /\ UNCHANGED <<toC, cst, cur, attns, shown, ended, cancelled, foreign>>
SAttn == toS # <<>> /\ Head(toS) = "attn" /\ toS' = Tail(toS) /\ srun' = FALSE
         /\ toC' = Append(toC, [k |-> "attn", b |-> sb]) /\ UNCHANGED <<sleft, sb, cst, cur, attns, shown, ended, cancelled, foreign>>
SRow == srun /\ sleft > 0 /\ sleft' = sleft - 1 /\ toC' = Append(toC, [k |-> "row", b |-> sb])
        /\ UNCHANGED <<toS, srun, sb, cst, cur, attns, shown, ended, cancelled, foreign>>
SDone == srun /\ sleft = 0 /\ srun' = FALSE /\ toC' = Append(toC, [k |-> "done", b |-> sb])
         /\ UNCHANGED <<toS, sleft, sb, cst, cur, attns, shown, ended, cancelled, foreign>>

Next == CBatch \/ CCancel \/ CRecv \/ SBatch \/ SAttn \/ SRow \/ SDone
Spec == Init /\ [][Next]_vars /\ WF_vars(CBatch) /\ WF_vars(CRecv) /\ WF_vars(SBatch)
        /\ WF_vars(SAttn) /\ WF_vars(SRow) /\ WF_vars(SDone)

EndsOnce == \A b \in Runs : ended[b] <= 1
NoSilentLoss == \A b \in Runs : ended[b] = 1 /\ ~cancelled[b] => shown[b] = R
OwnRows == ~foreign
Ends == [](cst # "ready" => <>(cst = "ready"))
=============================================================================
