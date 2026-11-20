------------------------------- MODULE Kitten -------------------------------
(***************************************************************************)
(* The save kitten (src/kitten/, Davy's my-nvim kitten.lua), 2026-11-18.   *)
(* After a save it plays N frames, each shown for T ticks of a loop_timer; *)
(* any key sends it away.  The window can be hidden (no frame callback, so *)
(* main.c draws nothing) and other things can ask for a redraw at any time *)
(* (a picker, syntax, a language server).  When is the next frame's timer *)
(* armed?  ArmOn says:                                                     *)
(*   "newframe"   by the draw that first shows a frame (src/kitten/)      *)
(*   "everydraw"  by every draw that shows the kitten                     *)
(*   "step"       by the timer itself, as kitten.lua's step() does        *)
(* Properties, stated before the mechanism:                                *)
(*   Idle      no kitten, no timer: an idle editor is never woken by it    *)
(*   Shown     a timer runs only for the frame on screen                   *)
(*   Hidden    a hidden window is woken at most once by the kitten        *)
(*   Finishes  once the window stays visible, a playing kitten ends       *)
(*             (its last frame passes, or a key sends it away), however   *)
(*             often other things redraw                                   *)
(* The start screen's cat (src/tools/dash.c, splash.lua) is the same      *)
(* mechanism: N frames in its 300 s, Start an opening, Key its closing.   *)
(* Claims: "newframe" has all four; "everydraw" breaks Finishes (redraws  *)
(* keep pushing the timer back); "step" breaks Shown and Hidden.          *)
(***************************************************************************)
EXTENDS Integers
CONSTANTS N, T, ArmOn, Saves
VARIABLES playing, frame, dirty, visible, drawn, armed, left, hfires, saves
vars == <<playing, frame, dirty, visible, drawn, armed, left, hfires, saves>>

None == -1

Init == playing = FALSE /\ frame = 0 /\ dirty = FALSE /\ visible = TRUE
        /\ drawn = None /\ armed = None /\ left = 0 /\ hfires = 0 /\ saves = 0

\* A save (or :KittenPaw) starts it again from the first frame; Saves of
\* them, so that Finishes asks about the kitten, not about endless saving.
Start == /\ saves < Saves /\ saves' = saves + 1 /\ playing' = TRUE /\ frame' = 0 /\ dirty' = TRUE /\ drawn' = None
         /\ IF ArmOn = "step" THEN armed' = 0 /\ left' = T
            ELSE armed' = None /\ left' = 0
         /\ UNCHANGED <<visible, hfires>>

\* Any key: the kitten goes away and its timer is cancelled.
Key == /\ playing' = FALSE /\ armed' = None /\ left' = 0 /\ dirty' = TRUE
       /\ UNCHANGED <<frame, visible, drawn, hfires, saves>>

\* Something else wants a redraw.
Other == dirty' = TRUE /\ UNCHANGED <<playing, frame, visible, drawn, armed, left, hfires, saves>>

\* main.c draws only when the backend may (Render.tla, Present.tla).
Draw == /\ dirty /\ visible /\ dirty' = FALSE
        /\ drawn' = IF playing THEN frame ELSE None
        /\ IF playing /\ ((ArmOn = "newframe" /\ armed /= frame) \/ ArmOn = "everydraw")
           THEN armed' = frame /\ left' = T
           ELSE UNCHANGED <<armed, left>>
        /\ UNCHANGED <<playing, frame, visible, hfires, saves>>

Tick == armed /= None /\ left > 0 /\ left' = left - 1
        /\ UNCHANGED <<playing, frame, dirty, visible, drawn, armed, hfires, saves>>

\* The timer wakes the loop: the next frame, or the end.
Fire == /\ armed /= None /\ left = 0 /\ dirty' = TRUE
        /\ hfires' = IF visible THEN hfires ELSE hfires + 1
        /\ IF frame + 1 < N
           THEN /\ frame' = frame + 1 /\ playing' = TRUE
                /\ IF ArmOn = "step" THEN armed' = frame + 1 /\ left' = T
                   ELSE armed' = None /\ left' = 0
           ELSE frame' = frame /\ playing' = FALSE /\ armed' = None /\ left' = 0
        /\ UNCHANGED <<visible, drawn, saves>>

Hide == visible /\ visible' = FALSE /\ UNCHANGED <<playing, frame, dirty, drawn, armed, left, hfires, saves>>
Show == ~visible /\ visible' = TRUE /\ hfires' = 0
        /\ UNCHANGED <<playing, frame, dirty, drawn, armed, left, saves>>

Next == Start \/ Key \/ Other \/ Draw \/ Tick \/ Fire \/ Hide \/ Show
Spec == Init /\ [][Next]_vars /\ WF_vars(Draw) /\ WF_vars(Tick) /\ WF_vars(Fire)

TypeOK == playing \in BOOLEAN /\ frame \in 0..N-1 /\ armed \in {None} \cup 0..N-1
          /\ left \in 0..T /\ drawn \in {None} \cup 0..N-1
Idle     == ~playing => armed = None
Shown    == armed /= None => drawn = armed
Hidden   == hfires <= 1
Finishes == <>[]visible => [](playing => <>~playing)
=============================================================================
