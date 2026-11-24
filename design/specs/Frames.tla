------------------------------ MODULE Frames ------------------------------
(***************************************************************************)
(* Several OS windows (Emacs frames) driven by one event loop.  Each      *)
(* frame is its own surface with its own frame callback (Present.tla      *)
(* "framecb"); one edit may change what several frames show (the same     *)
(* buffer in two frames).  A frame on a hidden workspace or a monitor     *)
(* that is off never gets its callback, and a workspace switch hides or   *)
(* shows a frame while the editor runs (Flip, at most Flips times).       *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  Fresh    once typing stops, every frame that ends up visible shows    *)
(*           the last edit that touched it (a hidden frame never holds up *)
(*           the others)                                                   *)
(*  NoWaste  no frame is drawn while its previous one is not yet shown    *)
(*  NoIdle   a frame is drawn only after an edit touched it since its     *)
(*           last draw (an idle editor draws nothing; Kitten.tla's kind)  *)
(*  Own      a dirty frame whose callback came may draw, whatever the     *)
(*           others do (a 144 Hz frame is never held to a 60 Hz one's     *)
(*           pace; the model has no time, so this says it without one)   *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): a dirty flag and a callback per frame;     *)
(* draw a frame when it is dirty and its own callback has come; drawing   *)
(* clears its flag.                                                        *)
(* Broken variants, each must fail TLC:                                    *)
(*  "shared"  one dirty flag for all frames, cleared after drawing the    *)
(*            frames that were ready (fails Fresh)                         *)
(*  "all"     draw only when every frame's callback has come (fails Own)  *)
(*  "nocb"    draw a dirty frame without waiting for its callback         *)
(*            (fails NoWaste)                                              *)
(*  "keep"    drawing does not clear the flag (fails NoIdle; it passes    *)
(*            Fresh and NoWaste, found in PR 132's review)                 *)
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

\* An edit changes what the frames in S show.
Key == /\ typed < Keys
       /\ \E S \in (SUBSET Frames) \ {{}} :
            /\ need' = [f \in Frames |-> IF f \in S THEN typed + 1 ELSE need[f]]
            /\ dirty' = [f \in Frames |-> dirty[f] \/ f \in S]
            /\ touched' = [f \in Frames |-> touched[f] \/ f \in S]
       /\ typed' = typed + 1
       /\ UNCHANGED <<drawn, shown, cb, wasted, vis, flips, idle>>

Ready(f) == IF Variant = "nocb" THEN TRUE
            ELSE IF Variant = "all" THEN \A g \in Frames : ~cb[g]
            ELSE ~cb[f]

Draw(f) == /\ dirty[f] /\ Ready(f)
           /\ drawn' = [drawn EXCEPT ![f] = need[f]]
           /\ cb' = [cb EXCEPT ![f] = TRUE]
           /\ wasted' = (wasted \/ cb[f])   \* the frame it replaces was never shown
           /\ idle' = (idle \/ ~touched[f]) \* no edit touched f since its last draw
           /\ touched' = [touched EXCEPT ![f] = FALSE]
           /\ dirty' = CASE Variant = "shared" -> [g \in Frames |-> FALSE]
                         [] Variant = "keep" -> dirty
                         [] OTHER -> [dirty EXCEPT ![f] = FALSE]
           /\ UNCHANGED <<typed, need, shown, vis, flips>>

\* The compositor shows a visible frame's last commit and sends its callback.
Repaint(f) == /\ f \in vis /\ cb[f]
              /\ shown' = [shown EXCEPT ![f] = drawn[f]]
              /\ cb' = [cb EXCEPT ![f] = FALSE]
              /\ UNCHANGED <<typed, need, drawn, dirty, wasted, vis, flips, touched, idle>>

\* A workspace switch hides or shows frame f.
Flip(f) == /\ flips < Flips /\ flips' = flips + 1
           /\ vis' = IF f \in vis THEN vis \ {f} ELSE vis \cup {f}
           /\ UNCHANGED <<typed, need, drawn, shown, cb, dirty, wasted, touched, idle>>

Next == Key \/ \E f \in Frames : Draw(f) \/ Repaint(f) \/ Flip(f)
Spec == Init /\ [][Next]_vars /\ \A f \in Frames : WF_vars(Draw(f)) /\ WF_vars(Repaint(f))

NoWaste == ~wasted
NoIdle == ~idle
Own == \A f \in Frames : dirty[f] /\ ~cb[f] => ENABLED Draw(f)
Fresh == \A f \in Frames : <>[](typed = Keys /\ f \in vis => shown[f] = need[f])
=============================================================================
