------------------------------- MODULE Remote ------------------------------
(***************************************************************************)
(* One editor per Windows session (include/remote.h, src/win/remote.c).  *)
(* N launches of homeros FILE start in any order, some together (Explorer *)
(* opening several selected files).  The editor's user may quit (:q!) at  *)
(* any time, and the editor then closes.                                  *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  OneEditor  at most one editor takes files at a time                   *)
(*  NoLoss     no launch is told its file was taken when no editor will   *)
(*             open it                                                    *)
(*  Shown      every launch's file is opened: by the editor it handed it  *)
(*             to, or by the launch itself as the editor                  *)
(*                                                                         *)
(* Mechanism ("ok"): a launch takes a named mutex (the turn), looks for   *)
(* the editor's window (FindWindowEx), and if none is there creates it    *)
(* and is the editor; else it sends WM_COPYDATA, which returns once the   *)
(* editor's loop has read it (taken, queued) or fails when the window is  *)
(* gone (the launch then looks again and, finding none, is the editor);  *)
(* then gives the turn back.  After each batch of keys the editor opens what it took, which  *)
(* also cancels a :q! of that batch; with nothing taken, a :q! closes the *)
(* window at once, with no message read between that check and the       *)
(* close.  Finding and creating are two steps, so without the turn two    *)
(* launches can both find nothing.                                         *)
(* Broken variants, each must fail TLC:                                    *)
(*  "noturn"   no mutex (fails OneEditor: two launches find no window)   *)
(*  "keepquit" a :q! closes the editor although it took a file (NoLoss)  *)
(*  "late"     the window closes after the check, with messages read     *)
(*             between, as when remote_stop came after b->close (NoLoss) *)
(*  "post"     PostMessage: the launch counts its file taken at once, and *)
(*             one posted to a window that closes is dropped (NoLoss)     *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets
CONSTANTS N, Quits, Variant
L == 1..N

VARIABLES st, turn, found, wins, q, opened, quit, nq, lost
vars == <<st, turn, found, wins, q, opened, quit, nq, lost>>

\* st[l]: "idle" not started; "wait" for the turn; "find"; "create" (found
\* nothing); "send" (found found[l]); "handed"; "editor"; "stopping" (the
\* window still there, "late" only);
\* "gone" (the editor closed)
Init == /\ st = [l \in L |-> "idle"] /\ turn = 0 /\ found = [l \in L |-> 0]
        /\ wins = {} /\ q = [l \in L |-> {}] /\ opened = {}
        /\ quit = [l \in L |-> FALSE] /\ nq = 0 /\ lost = FALSE

Give == turn' = IF Variant = "noturn" THEN turn ELSE 0

Start(l) == /\ st[l] = "idle"
            /\ st' = [st EXCEPT ![l] = IF Variant = "noturn" THEN "find" ELSE "wait"]
            /\ UNCHANGED <<turn, found, wins, q, opened, quit, nq, lost>>
Take(l) == /\ st[l] = "wait" /\ turn = 0 /\ turn' = l
           /\ st' = [st EXCEPT ![l] = "find"]
           /\ UNCHANGED <<found, wins, q, opened, quit, nq, lost>>
Find(l) == /\ st[l] = "find"
           /\ IF wins = {} THEN st' = [st EXCEPT ![l] = "create"] /\ UNCHANGED found
              ELSE \E e \in wins : st' = [st EXCEPT ![l] = "send"] /\ found' = [found EXCEPT ![l] = e]
           /\ UNCHANGED <<turn, wins, q, opened, quit, nq, lost>>
Create(l) == /\ st[l] = "create" /\ st' = [st EXCEPT ![l] = "editor"]
             /\ wins' = wins \cup {l} /\ opened' = opened \cup {l} /\ Give
             /\ UNCHANGED <<found, q, quit, nq, lost>>
\* SendMessage returns: the editor read it, or its window is gone, and the
\* launch, still holding the turn, finds again (none: it creates)
Send(l) == LET e == found[l] IN
           /\ st[l] = "send"
           /\ IF e \in wins
              THEN /\ st' = [st EXCEPT ![l] = "handed"] /\ q' = [q EXCEPT ![e] = q[e] \cup {l}]
                   /\ Give /\ UNCHANGED lost
              ELSE IF Variant = "post"
              THEN st' = [st EXCEPT ![l] = "handed"] /\ lost' = TRUE /\ Give /\ UNCHANGED q
              ELSE st' = [st EXCEPT ![l] = "find"] /\ UNCHANGED <<turn, q, lost>>
           /\ UNCHANGED <<found, wins, opened, quit, nq>>

\* the user's :q! in the editor's batch
Quit(e) == /\ st[e] = "editor" /\ ~quit[e] /\ nq < Quits
           /\ quit' = [quit EXCEPT ![e] = TRUE] /\ nq' = nq + 1
           /\ UNCHANGED <<st, turn, found, wins, q, opened, lost>>
\* main.c after the batch: open what was taken (cancelling the :q!), else
\* close on the :q!
Check(e) ==
  /\ st[e] = "editor" /\ (q[e] /= {} \/ quit[e])
  /\ IF q[e] /= {} /\ ~(Variant = "keepquit" /\ quit[e])
     THEN /\ opened' = opened \cup q[e] /\ q' = [q EXCEPT ![e] = {}]
          /\ quit' = [quit EXCEPT ![e] = FALSE] /\ UNCHANGED <<st, wins, lost>>
     ELSE IF Variant = "late"
     THEN st' = [st EXCEPT ![e] = "stopping"] /\ UNCHANGED <<wins, q, opened, quit, lost>>
     ELSE /\ st' = [st EXCEPT ![e] = "gone"] /\ wins' = wins \ {e}
          /\ lost' = (lost \/ q[e] /= {}) /\ UNCHANGED <<q, opened, quit>>
  /\ UNCHANGED <<turn, found, nq>>
Stop(e) == /\ st[e] = "stopping" /\ st' = [st EXCEPT ![e] = "gone"] /\ wins' = wins \ {e}
           /\ lost' = (lost \/ q[e] /= {})
           /\ UNCHANGED <<turn, found, q, opened, quit, nq>>

Next == \E l \in L : Start(l) \/ Take(l) \/ Find(l) \/ Create(l) \/ Send(l)
                     \/ Quit(l) \/ Check(l) \/ Stop(l)
\* the user quits when they like; everything else happens
Spec == Init /\ [][Next]_vars
        /\ \A l \in L : WF_vars(Start(l)) /\ WF_vars(Take(l)) /\ WF_vars(Find(l))
                        /\ WF_vars(Create(l)) /\ WF_vars(Send(l)) /\ WF_vars(Check(l)) /\ WF_vars(Stop(l))

OneEditor == Cardinality(wins) <= 1
NoLoss == ~lost
Shown == <>(opened = L)
=============================================================================
