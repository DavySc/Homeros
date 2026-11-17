------------------------------ MODULE Present ------------------------------
(***************************************************************************)
(* Review model, 2026-11-17.  When may the editor draw a frame into a      *)
(* Wayland window?  The design has two goals that meet here:               *)
(*   Prompt   a keystroke is in a committed frame before anything else     *)
(*            happens (the "keystroke to frame under 2 ms" target)         *)
(*   NoWaste  no frame is drawn that the compositor never shows            *)
(*            (the "no wasted cycle" rule)                                 *)
(* plus  Fresh: once typing stops, the screen shows the last key.          *)
(*                                                                         *)
(* The compositor repaints at its own rate (Repaint).  It gives a shm      *)
(* buffer back either when a newer one replaces it on screen               *)
(* (Release = "replace": weston's pixman renderer) or as soon as it has    *)
(* copied it at commit (Release = "commit": GL compositors).               *)
(* Client policies:                                                        *)
(*   "twobuf"  gui/wayland-ed.c: draw whenever one of 2 buffers is free    *)
(*   "framecb" draw only after the compositor's frame callback             *)
(* The model starts with the first frame (the empty text) on screen, so    *)
(* one of the two buffers is held by the compositor.                       *)
(***************************************************************************)
EXTENDS Naturals
CONSTANTS Keys, Policy, Release, Visible
VARIABLES typed, drawn, pending, shown, free, cb, wasted, late
vars == <<typed, drawn, pending, shown, free, cb, wasted, late>>

Init == typed = 0 /\ drawn = 0 /\ pending = 0 /\ shown = 0 /\ free = 1 /\ cb = FALSE
        /\ wasted = 0 /\ late = FALSE

CanDraw == /\ typed > drawn
           /\ IF Policy = "framecb" THEN ~cb /\ free > 0 ELSE free > 0

\* The client commits a frame holding everything typed so far.
Draw == /\ CanDraw
        /\ drawn' = typed /\ pending' = typed
        /\ wasted' = (IF pending /= 0 THEN wasted + 1 ELSE wasted) \* the frame it replaces was never shown
        /\ free' = IF Release = "commit" THEN free
                   ELSE IF pending /= 0 THEN free  \* the replaced one comes back at once
                   ELSE free - 1
        /\ cb' = (Policy = "framecb")
        /\ UNCHANGED <<typed, shown, late>>

\* A key arrives.  If the previous key is still not in a committed frame, it was late.
Key == /\ typed < Keys /\ ~CanDraw \* the loop draws before it reads more input
       /\ typed' = typed + 1
       /\ late' = (late \/ typed > drawn)
       /\ UNCHANGED <<drawn, pending, shown, free, cb, wasted>>

Repaint == /\ Visible
           /\ shown' = IF pending /= 0 THEN pending ELSE shown
           /\ pending' = 0
           /\ free' = IF Release = "replace" /\ pending /= 0 THEN free + 1 ELSE free
           /\ cb' = FALSE
           /\ UNCHANGED <<typed, drawn, wasted, late>>

Next == Draw \/ Key \/ Repaint \/ (typed = Keys /\ shown = typed /\ UNCHANGED vars)
Spec == Init /\ [][Next]_vars /\ WF_vars(Draw) /\ WF_vars(Repaint)

NoWaste == wasted = 0
Prompt  == ~late
Fresh   == <>[](typed = Keys => shown = Keys)
=============================================================================
