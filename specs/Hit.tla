-------------------------------- MODULE Hit --------------------------------
(* Threads that come to a breakpoint while the debugger can report one stop at a time
   (dbg/engine.c on dbg/win/target.c).

   Property, stated before the mechanism:
     Told   no thread runs the instruction under a breakpoint without a stop having been
            reported for it there since it came: no hit is lost (told in the model is
            taken back when the thread comes to the breakpoint again; the engine takes it
            back when all threads run, which is before)

   What makes it hard. To let a thread go on from a breakpoint the debugger takes the trap
   out and runs that thread's instruction alone; it must do so for every thread whose stop
   has been reported, or the thread hits the same breakpoint again without having moved.
   But a thread can stand on the breakpoint's address in three ways: its stop there was
   reported; it ran the trap while another thread's stop was reported, and was put back
   (its own event is still on the way, and is dropped if it comes while the trap is out);
   or it was frozen by another thread's stop just as it arrived, the trap not yet run.
   Its registers are the same in all three.

   Mechanism. The engine keeps, per thread, whether a stop at the place it stands on has
   been reported (told), and steps off only those threads. The others run into the planted
   trap, and their stop comes.

   Variants that must fail:
     "position"  every thread that stands on the breakpoint is stepped off (RAD's rule; RAD
                 does not put threads back, so the second way reports there, the third is lost)
     "owed"      every thread but those put back is stepped off: the third way is lost
     "kept"      told is not taken back when the thread runs on: it comes again, is frozen
                 before the trap, and counts as told *)
CONSTANTS Threads, Variant

VARIABLES pos,      \* per thread: "away", "on" the breakpoint's address, or "behind" the trap byte it has run
          pending,  \* per thread: its trap event has not been handed to the debugger yet (it runs nothing meanwhile)
          told,     \* per thread: what the engine keeps: a stop where it stands has been reported
          seen,     \* per thread: what is true: a stop has been reported for it since it came to the breakpoint
          owed,     \* per thread: it was put back onto the trap
          mode,     \* "stopped", "run", or the thread that runs one instruction alone
          missed    \* a thread has run the breakpoint's instruction untold
vars == <<pos, pending, told, seen, owed, mode, missed>>

All(v) == [t \in Threads |-> v]
Init == pos = All("away") /\ pending = All(FALSE) /\ told = All(FALSE) /\ seen = All(FALSE) /\ owed = All(FALSE) /\ mode = "run" /\ missed = FALSE

\* running with the trap planted: a thread comes to the breakpoint; then it runs the trap byte
Arrive(t) == mode = "run" /\ pos[t] = "away" /\ pos' = [pos EXCEPT ![t] = "on"] /\ told' = [told EXCEPT ![t] = told[t] /\ Variant = "kept"]
             /\ seen' = [seen EXCEPT ![t] = FALSE] /\ UNCHANGED <<pending, owed, mode, missed>>
Trap(t) == mode = "run" /\ pos[t] = "on" /\ ~pending[t] /\ pos' = [pos EXCEPT ![t] = "behind"]
           /\ pending' = [pending EXCEPT ![t] = TRUE] /\ UNCHANGED <<told, seen, owed, mode, missed>>

\* Windows hands over one event. With the trap planted it is this thread's stop: reported, and every other thread
\* that has run the trap is put back. With the trap out (another thread's step) it is late and dropped.
Event(t) == /\ pending[t] /\ mode # "stopped" /\ pending' = [pending EXCEPT ![t] = FALSE]
            /\ IF mode = "run"
               THEN /\ mode' = "stopped" /\ told' = [told EXCEPT ![t] = TRUE] /\ seen' = [seen EXCEPT ![t] = TRUE]
                    /\ pos' = [u \in Threads |-> IF pos[u] = "behind" THEN "on" ELSE pos[u]]
                    /\ owed' = [u \in Threads |-> IF u = t THEN FALSE ELSE owed[u] \/ pos[u] = "behind"]
               ELSE UNCHANGED <<mode, told, seen, pos, owed>>
            /\ UNCHANGED missed

Stuck(t) == pos[t] = "on" /\ CASE Variant = "position" -> TRUE
                               [] Variant = "owed" -> ~owed[t]
                               [] OTHER -> told[t]
\* the user goes on: a stuck thread runs its instruction alone, the trap out; with none stuck all run, the trap planted
Resume == /\ mode = "stopped"
          /\ \/ \E t \in Threads : Stuck(t) /\ mode' = t
             \/ (\A t \in Threads : ~Stuck(t)) /\ mode' = "run"
          /\ UNCHANGED <<pos, pending, told, seen, owed, missed>>
Step(t) == /\ mode = t /\ ~pending[t] /\ pos' = [pos EXCEPT ![t] = "away"] /\ mode' = "stopped"
           /\ missed' = (missed \/ ~seen[t]) /\ owed' = [owed EXCEPT ![t] = FALSE]
           /\ UNCHANGED <<pending, told, seen>>

Next == Resume \/ \E t \in Threads : Arrive(t) \/ Trap(t) \/ Event(t) \/ Step(t)
Spec == Init /\ [][Next]_vars

Told == ~missed
\* while stopped every thread shows the program's own registers
Settled == mode = "stopped" => \A t \in Threads : pos[t] # "behind"
=============================================================================
