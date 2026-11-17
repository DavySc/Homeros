------------------------------- MODULE Pace --------------------------------
(***************************************************************************)
(* Package 9, 2026-11-17.  When may the Win32 backend draw?  A GDI window  *)
(* gets no frame callback (Present.tla's answer on Wayland).  DWM composes *)
(* about every P ticks; one composition may come up to D ticks earlier or  *)
(* later than P after the one before.  The backend draws a frame only when *)
(* Gap ticks have passed since its last one, and never while minimised     *)
(* (HideCheck).  A frame replaced before any composition took it was drawn *)
(* for nothing.  Within one tick, a draw and a composition may come in     *)
(* either order (the backend's timer and DWM are different clocks).        *)
(* The loop sleeps when it may not draw; a key wakes it, and so does the   *)
(* backend's waitable timer (Timer), armed when a frame waits for the gap  *)
(* and allowed to fire up to a tick early (another clock again), after     *)
(* which the loop finds it may not draw yet, sleeps and arms it again.     *)
(*   NoWaste  no frame is drawn that DWM never shows                       *)
(*   Fresh    once typing stops, the screen shows the last key             *)
(* Claims: NoWaste holds exactly when Gap > P + D (and HideCheck); Fresh   *)
(* holds with the timer and fails without it.                              *)
(***************************************************************************)
EXTENDS Naturals
CONSTANTS Keys, P, D, Gap, Visible, HideCheck, Timer
VARIABLES typed, drawn, pending, shown, since, till, wasted, awake, armed
vars == <<typed, drawn, pending, shown, since, till, wasted, awake, armed>>

Init == typed = 0 /\ drawn = 0 /\ pending = 0 /\ shown = 0
        /\ since = Gap /\ till = 0 /\ wasted = 0 /\ awake = TRUE /\ armed = FALSE

CanDraw == typed > drawn /\ since >= Gap /\ (Visible \/ ~HideCheck)

Draw == /\ awake /\ CanDraw
        /\ drawn' = typed /\ pending' = typed /\ since' = 0
        /\ wasted' = IF pending /= 0 THEN wasted + 1 ELSE wasted
        /\ UNCHANGED <<typed, shown, till, awake, armed>>

Key == /\ typed < Keys /\ typed' = typed + 1 /\ awake' = TRUE
       /\ UNCHANGED <<drawn, pending, shown, since, till, wasted, armed>>

\* The loop waits; backend_ready armed the timer if a frame waits for the gap.
Sleep == /\ awake /\ ~CanDraw /\ awake' = FALSE
         /\ armed' = (Timer /\ typed > drawn /\ (Visible \/ ~HideCheck))
         /\ UNCHANGED <<typed, drawn, pending, shown, since, till, wasted>>

Fire == /\ armed /\ since + 1 >= Gap /\ awake' = TRUE /\ armed' = FALSE
        /\ UNCHANGED <<typed, drawn, pending, shown, since, till, wasted>>

\* DWM takes what the window shows now.
Compose == /\ Visible /\ till >= P - D
           /\ shown' = IF pending /= 0 THEN pending ELSE shown
           /\ pending' = 0 /\ till' = 0
           /\ UNCHANGED <<typed, drawn, since, wasted, awake, armed>>

\* Time passes; DWM never waits more than P + D.  Minimised, it composes nothing.
Tick == /\ Visible => till < P + D
        /\ till' = IF Visible THEN till + 1 ELSE till
        /\ since' = IF since < Gap THEN since + 1 ELSE since
        /\ UNCHANGED <<typed, drawn, pending, shown, wasted, awake, armed>>

Next == Draw \/ Key \/ Sleep \/ Fire \/ Compose \/ Tick
Spec == Init /\ [][Next]_vars /\ WF_vars(Draw) /\ WF_vars(Fire) /\ WF_vars(Compose) /\ WF_vars(Tick)

NoWaste == wasted = 0
Fresh   == <>[](typed = Keys => shown = Keys)
=============================================================================
