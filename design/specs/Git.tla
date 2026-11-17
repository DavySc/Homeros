-------------------------------- MODULE Git --------------------------------
(***************************************************************************)
(* Integrated magit: the status buffer and git commands on one event loop. *)
(* Properties, stated before the mechanism:                                *)
(*  NoLock     two git commands that write the index never run at once    *)
(*             (git fails one with "index.lock exists")                    *)
(*  ActsOnSeen a stage/unstage/discard applies only to the repository the  *)
(*             user saw when choosing it                                   *)
(*  Truthful   once activity stops, the buffer shows the repository as it  *)
(*             is, including changes made outside the editor               *)
(* Mechanism ("ok"): writes go through one queue, one at a time; each      *)
(* carries the version of the status it was chosen from and is dropped     *)
(* (with a message) if the repository moved on; any finished write and     *)
(* any watch event on .git/index, HEAD, refs or the work tree marks the    *)
(* buffer dirty, and a dirty buffer is read again when nothing runs.       *)
(* Broken variants, each must fail:                                        *)
(*  "parallel" a write starts while another runs                          *)
(*  "blind"    a write is applied without checking its version            *)
(*  "nowatch"  only the editor's own writes trigger a refresh             *)
(***************************************************************************)
EXTENDS Naturals, Sequences
CONSTANTS MaxOps, Variant
VARIABLES repo, shown, running, queue, dirty, ops, lockClash, staleApply
vars == <<repo, shown, running, queue, dirty, ops, lockClash, staleApply>>

Init == /\ repo = 0 /\ shown = 0 /\ running = 0 /\ queue = <<>>
        /\ dirty = FALSE /\ ops = 0 /\ lockClash = FALSE /\ staleApply = FALSE

\* The user chooses a write in the buffer; it is tagged with what the buffer shows.
Ask == /\ ops < MaxOps /\ Len(queue) < 2
       /\ queue' = Append(queue, shown) /\ ops' = ops + 1
       /\ UNCHANGED <<repo, shown, running, dirty, lockClash, staleApply>>

Start == /\ queue /= <<>>
         /\ (Variant = "parallel" \/ running = 0)
         /\ lockClash' = (lockClash \/ running > 0)
         /\ LET stale == Head(queue) /= repo IN
              IF stale /\ Variant /= "blind"
              THEN /\ running' = running                   \* dropped
                   /\ staleApply' = staleApply
              ELSE /\ running' = running + 1
                   /\ staleApply' = (staleApply \/ stale)
         /\ queue' = Tail(queue)
         /\ UNCHANGED <<repo, shown, dirty, ops>>

Finish == /\ running > 0
          /\ running' = running - 1 /\ repo' = repo + 1 /\ dirty' = TRUE
          /\ UNCHANGED <<shown, queue, ops, lockClash, staleApply>>

\* A terminal, another program or a formatter changes the repository.
Outside == /\ ops < MaxOps
           /\ repo' = repo + 1 /\ ops' = ops + 1
           /\ dirty' = (Variant /= "nowatch" \/ dirty)
           /\ UNCHANGED <<shown, running, queue, lockClash, staleApply>>

\* git status --porcelain=v2 -z --branch, log, diff, diff --cached, describe or the merge log; redraw.
Refresh == /\ dirty /\ running = 0
           /\ shown' = repo /\ dirty' = FALSE
           /\ UNCHANGED <<repo, running, queue, ops, lockClash, staleApply>>

Next == Ask \/ Start \/ Finish \/ Outside \/ Refresh
Spec == Init /\ [][Next]_vars /\ WF_vars(Start) /\ WF_vars(Finish) /\ WF_vars(Refresh)

NoLock == ~lockClash
ActsOnSeen == ~staleApply
Truthful == <>[](shown = repo)
=============================================================================
