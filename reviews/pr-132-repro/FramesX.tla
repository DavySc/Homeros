------------------------------ MODULE FramesX ------------------------------
(***************************************************************************)
(* Review of PR 132.  design/specs/Frames.tla with three additions:       *)
(*  - frames are hidden and shown while the editor runs (a workspace      *)
(*    switch on Hyprland), at most Flips times; Fresh is asked of every   *)
(*    frame that ends up visible                                           *)
(*  - NoIdle: a frame is drawn at most once per edit that touched it      *)
(*    (an idle editor draws nothing)                                       *)
(*  - two mechanisms that Frames.tla's own properties accept:             *)
(*    "keep"       Draw never clears the frame's dirty flag               *)
(*    "sharedall"  one dirty flag, cleared once every frame has drawn the *)
(*                 last edit that touched it (the obvious repair of        *)
(*                 "shared")                                               *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets
CONSTANTS Keys, Frames, Visible0, Flips, Variant

VARIABLES typed, need, drawn, shown, cb, dirty, wasted, vis, flips, touched, idle
vars == <<typed, need, drawn, shown, cb, dirty, wasted, vis, flips, touched, idle>>

Init == /\ typed = 0
        /\ need = [f \in Frames |-> 0] /\ drawn = [f \in Frames |-> 0]
        /\ shown = [f \in Frames |-> 0] /\ cb = [f \in Frames |-> FALSE]
        /\ dirty = [f \in Frames |-> FALSE] /\ wasted = FALSE
        /\ vis = Visible0 /\ flips = 0
        /\ touched = [f \in Frames |-> FALSE] /\ idle = FALSE

Key == /\ typed < Keys
       /\ \E S \in (SUBSET Frames) \ {{}} :
            /\ need' = [f \in Frames |-> IF f \in S THEN typed + 1 ELSE need[f]]
            /\ dirty' = [f \in Frames |-> IF Variant = "sharedall" THEN TRUE ELSE dirty[f] \/ f \in S]
            /\ touched' = [f \in Frames |-> touched[f] \/ f \in S]
       /\ typed' = typed + 1
       /\ UNCHANGED <<drawn, shown, cb, wasted, vis, flips, idle>>

Ready(f) == ~cb[f]

Draw(f) == /\ dirty[f] /\ Ready(f)
           /\ drawn' = [drawn EXCEPT ![f] = need[f]]
           /\ cb' = [cb EXCEPT ![f] = TRUE]
           /\ wasted' = (wasted \/ cb[f])
           /\ idle' = (idle \/ ~touched[f])   \* no edit touched f since its last draw
           /\ touched' = [touched EXCEPT ![f] = FALSE]
           /\ dirty' = CASE Variant = "keep" -> dirty
                         [] Variant = "sharedall" ->
                              [g \in Frames |-> \E h \in Frames : drawn'[h] /= need[h]]
                         [] OTHER -> [dirty EXCEPT ![f] = FALSE]
           /\ UNCHANGED <<typed, need, shown, vis, flips>>

Repaint(f) == /\ f \in vis /\ cb[f]
              /\ shown' = [shown EXCEPT ![f] = drawn[f]]
              /\ cb' = [cb EXCEPT ![f] = FALSE]
              /\ UNCHANGED <<typed, need, drawn, dirty, wasted, vis, flips, touched, idle>>

Flip(f) == /\ flips < Flips /\ flips' = flips + 1
           /\ vis' = IF f \in vis THEN vis \ {f} ELSE vis \cup {f}
           /\ UNCHANGED <<typed, need, drawn, shown, cb, dirty, wasted, touched, idle>>

Next == Key \/ \E f \in Frames : Draw(f) \/ Repaint(f) \/ Flip(f)
Spec == Init /\ [][Next]_vars /\ \A f \in Frames : WF_vars(Draw(f)) /\ WF_vars(Repaint(f))

NoWaste == ~wasted
NoIdle == ~idle
Fresh == \A f \in Frames : <>[](typed = Keys /\ f \in vis => shown[f] = need[f])
=============================================================================
