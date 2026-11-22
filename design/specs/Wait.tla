-------------------------------- MODULE Wait --------------------------------
(***************************************************************************)
(* proc_wait (loop.h), Neovim's system() for a format filter: the command *)
(* blocks on one child (T) while another child (O, a language server, git) *)
(* may exit at any time.  T writes up to N bytes and exits, or hangs, or   *)
(* ignores SIGTERM; the wait's time may run out or Ctrl-C may come         *)
(* (Expire).  Exits reach the loop through one wake-up (the SIGCHLD        *)
(* self-pipe on Linux), which the wait drains.                             *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  Deferred       nothing of O runs during the wait (the command's state  *)
(*                 is half done).                                          *)
(*  ReportedFirst  the wait returns only after T was reported, with every  *)
(*                 byte it wrote.                                          *)
(*  Returns        the wait returns, whatever T does.                     *)
(*  OtherReported  once O has exited, its exit is reported.               *)
(*                                                                         *)
(* Mechanism (Variant = "ok", src/loop/): the wait polls only T's pipes   *)
(* and the wake-up, reaps only T, kills T when time is up (SIGKILL after   *)
(* SIGTERM, as proc_kill) and returns at T's report, writing the wake-up   *)
(* again so the loop looks at the others.                                  *)
(* Broken variants, each must fail TLC:                                    *)
(*  "drain"   the wake-up is not written again: fails OtherReported        *)
(*  "others"  the wait reaps every child: fails Deferred                   *)
(*  "early"   the wait returns at the kill, before the report: fails       *)
(*            ReportedFirst                                                *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS N, Variant

VARIABLES tAlive, written, pipe, got, tRep, oAlive, oRep, wake, phase, expired, killed, otherRan
vars == <<tAlive, written, pipe, got, tRep, oAlive, oRep, wake, phase, expired, killed, otherRan>>

Init == tAlive = TRUE /\ written = 0 /\ pipe = 0 /\ got = 0 /\ tRep = FALSE
        /\ oAlive = TRUE /\ oRep = FALSE /\ wake = FALSE /\ phase = "wait"
        /\ expired = FALSE /\ killed = FALSE /\ otherRan = FALSE

TWrite == tAlive /\ written < N /\ written' = written + 1 /\ pipe' = pipe + 1
          /\ UNCHANGED <<tAlive, got, tRep, oAlive, oRep, wake, phase, expired, killed, otherRan>>
\* T exits by itself (or on SIGTERM), or never; SIGKILL always ends it.
TExit  == tAlive /\ tAlive' = FALSE /\ wake' = TRUE
          /\ UNCHANGED <<written, pipe, got, tRep, oAlive, oRep, phase, expired, killed, otherRan>>
Kill9  == killed /\ TExit
OExit  == oAlive /\ oAlive' = FALSE /\ wake' = TRUE
          /\ UNCHANGED <<tAlive, written, pipe, got, tRep, oRep, phase, expired, killed, otherRan>>

\* time runs out or Ctrl-C comes; then proc_kill
Expire == phase = "wait" /\ ~expired /\ expired' = TRUE
          /\ UNCHANGED <<tAlive, written, pipe, got, tRep, oAlive, oRep, wake, phase, killed, otherRan>>
Kill   == phase = "wait" /\ expired /\ ~killed /\ ~tRep /\ killed' = TRUE
          /\ UNCHANGED <<tAlive, written, pipe, got, tRep, oAlive, oRep, wake, phase, expired, otherRan>>

WRead  == phase = "wait" /\ ~tRep /\ pipe > 0 /\ pipe' = 0 /\ got' = got + pipe
          /\ UNCHANGED <<tAlive, written, tRep, oAlive, oRep, wake, phase, expired, killed, otherRan>>
\* the wait drains the wake-up and reaps T (report reads what T's pipe holds)
WDrain == /\ phase = "wait" /\ wake /\ wake' = FALSE
          /\ IF ~tAlive /\ ~tRep THEN tRep' = TRUE /\ got' = got + pipe /\ pipe' = 0
             ELSE UNCHANGED <<tRep, got, pipe>>
          /\ IF Variant = "others" /\ ~oAlive /\ ~oRep THEN oRep' = TRUE /\ otherRan' = TRUE
             ELSE UNCHANGED <<oRep, otherRan>>
          /\ UNCHANGED <<tAlive, written, oAlive, phase, expired, killed>>
Return == /\ phase = "wait" /\ (tRep \/ (Variant = "early" /\ killed))
          /\ phase' = "after"
          /\ wake' = IF Variant = "drain" THEN wake ELSE TRUE
          /\ UNCHANGED <<tAlive, written, pipe, got, tRep, oAlive, oRep, expired, killed, otherRan>>
\* the loop after the command: drains the wake-up and reaps every child
LDrain == /\ phase = "after" /\ wake /\ wake' = FALSE
          /\ oRep' = (oRep \/ ~oAlive)
          /\ UNCHANGED <<tAlive, written, pipe, got, tRep, oAlive, phase, expired, killed, otherRan>>

Next == TWrite \/ TExit \/ Kill9 \/ OExit \/ Expire \/ Kill \/ WRead \/ WDrain \/ Return \/ LDrain
Spec == Init /\ [][Next]_vars /\ WF_vars(Expire) /\ WF_vars(Kill) /\ WF_vars(Kill9)
        /\ WF_vars(WRead) /\ WF_vars(WDrain) /\ WF_vars(Return) /\ WF_vars(LDrain)

Deferred      == ~otherRan
ReportedFirst == phase = "after" => tRep /\ got = written
Returns       == <>(phase = "after")
OtherReported == ~oAlive ~> oRep
=============================================================================
