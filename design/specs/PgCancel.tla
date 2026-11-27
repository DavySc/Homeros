------------------------------ MODULE PgCancel ------------------------------
(***************************************************************************)
(* Package 22: a PostgreSQL connection running queries and cancelling     *)
(* them (src/sql/pg.c).  A cancel goes on a second connection to the      *)
(* postmaster, which signals the backend (SIGINT) and then closes that     *)
(* connection.  The backend acts on the signal at its next interrupt       *)
(* check: a running query ends with error 57014; an idle backend ignores   *)
(* it, and a signal sent before the next query arrives is taken before     *)
(* that query is read (the kernel runs a pending handler before recv      *)
(* returns).  The client sends Q queries one after another.                *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  OwnCancel  only a query the client cancelled ends with 57014;          *)
(*  Ends       every query ends.                                           *)
(*                                                                         *)
(* Mechanism (Variant = "ok", pg_io and 'Z' in message()): after a cancel  *)
(* the run is over only once ReadyForQuery has come AND the postmaster has *)
(* closed the cancel connection (libpq's PQcancel waits for the close).   *)
(* Broken variant, must fail TLC:                                          *)
(*  "noclose"  the run is over at ReadyForQuery: the next query can reach  *)
(*             the backend before the signal and be cancelled instead     *)
(*             (PR 133's review measured 16 of 20).                        *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS Q, Variant

VARIABLES toS, toC, kill, sig, bq, cst, cur, asked, err
vars == <<toS, toC, kill, sig, bq, cst, cur, asked, err>>
Qs == 1..Q

Init == toS = <<>> /\ toC = <<>> /\ kill = "none" /\ sig = FALSE /\ bq = 0
        /\ cst = "ready" /\ cur = 0 /\ asked = [q \in Qs |-> FALSE] /\ err = [q \in Qs |-> FALSE]

(* the client: pg_query, pg_cancel, pg_io *)
CQuery == cst = "ready" /\ cur < Q /\ cur' = cur + 1 /\ cst' = "busy" /\ toS' = Append(toS, cur + 1)
          /\ UNCHANGED <<toC, kill, sig, bq, asked, err>>
CCancel == cst = "busy" /\ kill = "none" /\ kill' = "sent" /\ cst' = "cancel" /\ asked' = [asked EXCEPT ![cur] = TRUE]
           /\ UNCHANGED <<toS, toC, sig, bq, cur, err>>
CReady == toC # <<>> /\ toC' = Tail(toC)
          /\ cst' = (IF Variant = "ok" /\ kill = "sent" THEN "waitclose" ELSE "ready")
          /\ UNCHANGED <<toS, kill, sig, bq, cur, asked, err>>
CClosed == kill = "closed" /\ kill' = "none" /\ cst' = (IF cst = "waitclose" THEN "ready" ELSE cst)
           /\ UNCHANGED <<toS, toC, sig, bq, cur, asked, err>>

(* the postmaster: the signal, then the close *)
PCancel == kill = "sent" /\ sig' = TRUE /\ kill' = "closed" /\ UNCHANGED <<toS, toC, bq, cst, cur, asked, err>>

(* the backend *)
BRead == bq = 0 /\ ~sig /\ toS # <<>> /\ bq' = Head(toS) /\ toS' = Tail(toS)
         /\ UNCHANGED <<toC, kill, sig, cst, cur, asked, err>>
BIdleSig == bq = 0 /\ sig /\ sig' = FALSE /\ UNCHANGED <<toS, toC, kill, bq, cst, cur, asked, err>>
BSig == bq # 0 /\ sig /\ sig' = FALSE /\ err' = [err EXCEPT ![bq] = TRUE] /\ bq' = 0 /\ toC' = Append(toC, "Z")
        /\ UNCHANGED <<toS, kill, cst, cur, asked>>
BDone == bq # 0 /\ bq' = 0 /\ toC' = Append(toC, "Z") /\ UNCHANGED <<toS, kill, sig, cst, cur, asked, err>>

Next == CQuery \/ CCancel \/ CReady \/ CClosed \/ PCancel \/ BRead \/ BIdleSig \/ BSig \/ BDone
Spec == Init /\ [][Next]_vars /\ WF_vars(CQuery) /\ WF_vars(CReady) /\ WF_vars(CClosed) /\ WF_vars(PCancel)
        /\ WF_vars(BRead) /\ WF_vars(BIdleSig) /\ WF_vars(BSig) /\ WF_vars(BDone)

OwnCancel == \A q \in Qs : err[q] => asked[q]
Ends == [](cst # "ready" => <>(cst = "ready"))
=============================================================================
