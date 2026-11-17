-------------------------------- MODULE Exit --------------------------------
(***************************************************************************)
(* A child process ends.  It writes up to N bytes into its output pipe,   *)
(* then exits; a process it started (a holder) may keep the pipe open for  *)
(* as long as it likes, even forever (a daemon).  The editor reads the     *)
(* pipe, reaps the child after SIGCHLD and reports the exit; the user may  *)
(* stop the job (proc_kill) while it looks busy.                           *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  Reported     once the child has exited, its exit is reported, whatever *)
(*               the holder does (Neovim's on_exit comes in 1 to 2 ms).    *)
(*  OutputFirst  the report comes after every byte the child wrote.        *)
(*  NoStale      no signal goes to a pid after it was reaped (the kernel   *)
(*               may have given it to a stranger).                         *)
(*                                                                         *)
(* Mechanism (Variant = "ok", src/loop/): at the reap, read what the pipe  *)
(* holds, close it, report and free the slot; proc_kill on a free slot     *)
(* does nothing.                                                           *)
(* Broken variants, each must fail TLC:                                    *)
(*  "waiteof"  report only once the pipe's last writer closed it (PR 15 as *)
(*             first submitted): fails Reported and NoStale                *)
(*  "early"    report at the reap without reading what the pipe holds:     *)
(*             fails OutputFirst                                           *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS N, Variant

VARIABLES written, pipe, got, alive, holder, reaped, reported, stale
vars == <<written, pipe, got, alive, holder, reaped, reported, stale>>

Init == written = 0 /\ pipe = 0 /\ got = 0 /\ alive = TRUE /\ holder = TRUE
        /\ reaped = FALSE /\ reported = FALSE /\ stale = FALSE

Write  == alive /\ written < N /\ written' = written + 1 /\ pipe' = pipe + 1
          /\ UNCHANGED <<got, alive, holder, reaped, reported, stale>>
ExitC  == alive /\ alive' = FALSE /\ UNCHANGED <<written, pipe, got, holder, reaped, reported, stale>>
\* The holder may close the pipe, or never.
HolderEnd == holder /\ holder' = FALSE /\ UNCHANGED <<written, pipe, got, alive, reaped, reported, stale>>

Read == ~reported /\ pipe > 0 /\ pipe' = pipe - 1 /\ got' = got + 1
        /\ UNCHANGED <<written, alive, holder, reaped, reported, stale>>

Reap == /\ ~alive /\ ~reaped /\ reaped' = TRUE
        /\ UNCHANGED <<written, alive, holder, stale>>
        /\ IF Variant = "waiteof" THEN UNCHANGED <<pipe, got, reported>>
           ELSE /\ reported' = TRUE
                /\ IF Variant = "ok" THEN got' = got + pipe /\ pipe' = 0 ELSE UNCHANGED <<pipe, got>>

\* waiteof: the end of the pipe (no writer left, nothing left in it).
Eof == Variant = "waiteof" /\ reaped /\ ~reported /\ ~alive /\ ~holder /\ pipe = 0
       /\ reported' = TRUE /\ UNCHANGED <<written, pipe, got, alive, holder, reaped, stale>>

\* proc_kill on a slot still in use signals the child's group.
Kill == ~reported /\ stale' = (stale \/ reaped)
        /\ UNCHANGED <<written, pipe, got, alive, holder, reaped, reported>>

Next == Write \/ ExitC \/ HolderEnd \/ Read \/ Reap \/ Eof \/ Kill
Spec == Init /\ [][Next]_vars /\ WF_vars(ExitC) /\ WF_vars(Read) /\ WF_vars(Reap) /\ WF_vars(Eof)

Reported    == ~alive ~> reported
OutputFirst == reported => got = written
NoStale     == ~stale
=============================================================================
