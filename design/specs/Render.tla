------------------------------- MODULE Render -------------------------------
(***************************************************************************)
(* The main loop: input arrives, commands edit the buffer, frames are     *)
(* drawn.  Single-threaded, as in Neovim, Helix and Emacs.                 *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  NoTornFrame  no frame shows a command half-applied (a macro, `.`, or a *)
(*               multi-cursor edit is one step on screen);                 *)
(*  Responsive   whatever the input rate, every change reaches the screen *)
(*               (key repeat or a paste storm cannot freeze the display).  *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): draw only between commands; run at most    *)
(* Batch commands between frames while input is pending.                   *)
(* Broken variants, each must fail TLC:                                    *)
(*  "drain"    process all pending input before drawing (fails Responsive) *)
(*  "midcmd"   allow a draw between the steps of a compound command        *)
(*             (fails NoTornFrame)                                         *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS Cap, Batch, Variant

VARIABLES inq, steps, dirty, since, torn
vars == <<inq, steps, dirty, since, torn>>

Init == inq = 0 /\ steps = 0 /\ dirty = FALSE /\ since = 0 /\ torn = FALSE

\* The environment: input may arrive at any time, as fast as it likes.
Arrive == inq < Cap /\ inq' = inq + 1 /\ UNCHANGED <<steps, dirty, since, torn>>

\* Take one input event: a simple edit (1 step) or a compound one (2 steps).
Take ==
  /\ inq > 0 /\ steps = 0
  /\ Variant = "drain" \/ since < Batch
  /\ inq' = inq - 1
  /\ \E n \in {1, 2} : steps' = n
  /\ UNCHANGED <<dirty, since, torn>>

Step ==
  /\ steps > 0
  /\ steps' = steps - 1 /\ dirty' = TRUE
  /\ since' = IF steps = 1 /\ since < Batch THEN since + 1 ELSE since
  /\ UNCHANGED <<inq, torn>>

Draw ==
  /\ dirty
  /\ Variant = "midcmd" \/ steps = 0
  /\ Variant /= "drain" \/ inq = 0
  /\ torn' = (torn \/ steps > 0)
  /\ dirty' = FALSE /\ since' = 0
  /\ UNCHANGED <<inq, steps>>

Next == Arrive \/ Take \/ Step \/ Draw
Spec == Init /\ [][Next]_vars /\ WF_vars(Take) /\ WF_vars(Step) /\ WF_vars(Draw)

NoTornFrame == ~torn
Responsive == dirty ~> ~dirty
=============================================================================
