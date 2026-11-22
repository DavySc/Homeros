---------------------------- MODULE LatexRender ----------------------------
(***************************************************************************)
(* LaTeX previews (src/image/latex.c, package 21), 2026-11-21.  A fragment *)
(* is rendered to dir/ltximg-<hash>.png; an editor that finds that name   *)
(* shows it and never renders it again.  Several editors (Davy may open   *)
(* the same notes twice) and several lookups in one editor can ask for    *)
(* the same fragment at once.  dvipng writes the PNG a byte at a time.     *)
(* Where does it write, and what stops two jobs?  Mode says:              *)
(*   "own"     a name of its own (the hash and the pid), renamed onto the  *)
(*             final name once written (latex.c), one job per hash in an  *)
(*             editor (its job table)                                     *)
(*   "direct"  the final name itself                                       *)
(*   "shared"  the hash's name without the pid, renamed once written      *)
(*   "noguard" as "own", without the job table                            *)
(* Property, stated before the mechanism:                                  *)
(*   Whole     whenever the final name exists, the file it names is whole *)
(*             and no job writes into it any more: a lookup never shows a *)
(*             half-written PNG, and never one that changes under it      *)
(* Claims: "own" has it; "direct", "shared" and "noguard" break it.        *)
(* A file opened for writing by an existing name is that file, truncated  *)
(* (fopen "wb"); rename replaces the target in one step (POSIX; Windows'  *)
(* fails instead, which leaves the final name as it was).                 *)
(***************************************************************************)
EXTENDS Integers, FiniteSets
CONSTANTS Editors, Lookups, L, Mode
VARIABLES names, written, pc, ino, next
vars == <<names, written, pc, ino, next>>

Jobs == Editors \X (1..Lookups)
Files == 1..(Cardinality(Jobs) + 1)
\* the name a job writes to
Tmp(j) == CASE Mode = "direct" -> "final"
          [] Mode = "shared" -> "tmp"
          [] OTHER -> j[1]

Init == /\ names = [n \in {"final", "tmp"} \cup Editors |-> 0]
        /\ written = [f \in Files |-> 0]
        /\ pc = [j \in Jobs |-> "idle"] /\ ino = [j \in Jobs |-> 0] /\ next = 1

\* a lookup finds no final name and starts a job, unless its editor's job
\* table has one for this hash
Start(j) == /\ pc[j] = "idle" /\ names["final"] = 0
            /\ (Mode = "noguard" \/ \A k \in Jobs : k[1] = j[1] => pc[k] /= "writing")
            /\ pc' = [pc EXCEPT ![j] = "writing"]
            /\ IF names[Tmp(j)] /= 0
               THEN /\ ino' = [ino EXCEPT ![j] = names[Tmp(j)]]
                    /\ written' = [written EXCEPT ![names[Tmp(j)]] = 0]
                    /\ UNCHANGED <<names, next>>
               ELSE /\ ino' = [ino EXCEPT ![j] = next]
                    /\ names' = [names EXCEPT ![Tmp(j)] = next]
                    /\ next' = next + 1
                    /\ UNCHANGED written

Write(j) == /\ pc[j] = "writing" /\ written[ino[j]] < L
            /\ written' = [written EXCEPT ![ino[j]] = @ + 1]
            /\ UNCHANGED <<names, pc, ino, next>>

\* dvipng is done; the job renames its file onto the final name (a rename
\* of a name already gone fails and ends the job)
Finish(j) == /\ pc[j] = "writing" /\ written[ino[j]] = L
             /\ pc' = [pc EXCEPT ![j] = "done"]
             /\ IF Mode /= "direct" /\ names[Tmp(j)] /= 0
                THEN names' = [names EXCEPT !["final"] = names[Tmp(j)], ![Tmp(j)] = 0]
                ELSE UNCHANGED names
             /\ UNCHANGED <<written, ino, next>>

Next == \E j \in Jobs : Start(j) \/ Write(j) \/ Finish(j)
Spec == Init /\ [][Next]_vars

TypeOK == /\ \A n \in DOMAIN names : names[n] \in 0..Cardinality(Jobs) + 1
          /\ \A f \in Files : written[f] \in 0..L
          /\ \A j \in Jobs : pc[j] \in {"idle", "writing", "done"}

Whole == names["final"] /= 0 =>
           /\ written[names["final"]] = L
           /\ \A j \in Jobs : pc[j] = "writing" => ino[j] /= names["final"]
=============================================================================
