------------------------------ MODULE Frames ------------------------------
(***************************************************************************)
(* Several OS windows (Emacs frames) driven by one event loop.  Each      *)
(* frame is its own surface with its own frame callback (Present.tla      *)
(* "framecb"); one edit may change what several frames show (the same     *)
(* buffer in two frames).  A frame on a hidden workspace or monitor that  *)
(* is off never gets its callback.                                         *)
(*                                                                         *)
(* Properties, stated before the mechanism:                               *)
(*  Fresh    once typing stops, every visible frame shows the last edit   *)
(*           that touched it (a hidden frame never holds up the others)   *)
(*  NoWaste  no frame is drawn while its previous one is not yet shown    *)
(*                                                                         *)
(* Mechanism (Variant = "ok"): a dirty flag and a callback per frame;     *)
(* draw a frame when it is dirty and its own callback has come.           *)
(* Broken variants, each must fail TLC:                                    *)
(*  "shared"  one dirty flag for all frames, cleared after drawing the    *)
(*            frames that were ready (fails Fresh)                         *)
(*  "all"     draw only when every frame's callback has come (fails Fresh *)
(*            when one frame is hidden)                                    *)
(*  "nocb"    draw a dirty frame without waiting for its callback         *)
(*            (fails NoWaste)                                              *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets
CONSTANTS Keys, Frames, Visible, Variant

VARIABLES typed, need, drawn, shown, cb, dirty, wasted
vars == <<typed, need, drawn, shown, cb, dirty, wasted>>

Init == /\ typed = 0
        /\ need = [f \in Frames |-> 0] /\ drawn = [f \in Frames |-> 0]
        /\ shown = [f \in Frames |-> 0] /\ cb = [f \in Frames |-> FALSE]
        /\ dirty = [f \in Frames |-> FALSE] /\ wasted = FALSE

\* An edit changes what the frames in S show.
Key == /\ typed < Keys
       /\ \E S \in (SUBSET Frames) \ {{}} :
            /\ need' = [f \in Frames |-> IF f \in S THEN typed + 1 ELSE need[f]]
            /\ dirty' = [f \in Frames |-> dirty[f] \/ f \in S]
       /\ typed' = typed + 1
       /\ UNCHANGED <<drawn, shown, cb, wasted>>

Ready(f) == IF Variant = "nocb" THEN TRUE
            ELSE IF Variant = "all" THEN \A g \in Frames : ~cb[g]
            ELSE ~cb[f]

Draw(f) == /\ dirty[f] /\ Ready(f)
           /\ drawn' = [drawn EXCEPT ![f] = need[f]]
           /\ cb' = [cb EXCEPT ![f] = TRUE]
           /\ wasted' = (wasted \/ cb[f])   \* the frame it replaces was never shown
           /\ dirty' = IF Variant = "shared" THEN [g \in Frames |-> FALSE]
                       ELSE [dirty EXCEPT ![f] = FALSE]
           /\ UNCHANGED <<typed, need, shown>>

\* The compositor shows a visible frame's last commit and sends its callback.
Repaint(f) == /\ f \in Visible /\ cb[f]
              /\ shown' = [shown EXCEPT ![f] = drawn[f]]
              /\ cb' = [cb EXCEPT ![f] = FALSE]
              /\ UNCHANGED <<typed, need, drawn, dirty, wasted>>

Next == Key \/ \E f \in Frames : Draw(f) \/ Repaint(f)
Spec == Init /\ [][Next]_vars /\ \A f \in Frames : WF_vars(Draw(f)) /\ WF_vars(Repaint(f))

NoWaste == ~wasted
Fresh == \A f \in Visible : <>[](typed = Keys => shown[f] = need[f])
=============================================================================
