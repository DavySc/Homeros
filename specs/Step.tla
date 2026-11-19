------------------------------- MODULE Step -------------------------------
(* A single step under process control on Windows (dbg/win/target.c): one thread runs one
   instruction while every other thread waits.

   Properties, stated before the mechanism:
     Alone      during a step no thread but the stepping one executes anything: no other thread
                passes a place whose trap is out for the step, and what the editor shows of the
                other threads is still true after it
     Balanced   outside a step no thread stays suspended by the debugger: nothing is left
                waiting for good, however the step ended (its event, or a halt)

   What makes Alone hard: the debugger knows a thread only from its creation event, and Windows
   hands over one event at a time. A thread created just before a stop is there, unknown, when
   the step begins; it runs nothing before its creation event has been answered, and that event
   comes during the step.

   Mechanism. Resume with a step suspends every known thread but the stepping one. A thread
   whose creation event comes during a step is suspended before the event is answered. The stop
   (the step's event, or a halt, which first suspends every known thread) resumes every known
   thread but the stepping one.

   Not modelled, and true of RAD's solo step as well: an instruction that waits for another
   thread (a system call) does not end until the user halts and lets all run. *)
EXTENDS Naturals

CONSTANTS Threads, First, Variant

VARIABLES born,     \* the threads that exist
          known,    \* those whose creation event the debugger has answered
          count,    \* per thread: how often the debugger has suspended it, less the resumes
          mode,     \* "stopped", "step" or "run"
          stepper,  \* the thread a step is for
          held,     \* the stop came from an event, which holds every thread still by itself (else: from a halt)
          ran       \* the threads that have executed something since the last resume
vars == <<born, known, count, mode, stepper, held, ran>>

Init == born = {First} /\ known = {First} /\ count = [t \in Threads |-> 0] /\ mode = "stopped"
        /\ stepper = First /\ held = TRUE /\ ran = {}

Less(n) == IF n = 0 THEN 0 ELSE n - 1   \* ResumeThread does nothing to a thread that is not suspended
\* after a halt the resume lets every known thread go; with a step it suspends all but one
Resume(step, s) == mode = "stopped" /\ s \in known
  /\ count' = [t \in Threads |-> LET c == IF held \/ t \notin known THEN count[t] ELSE Less(count[t])
                                 IN IF step /\ t \in known /\ t # s /\ Variant # "nosuspend" THEN c + 1 ELSE c]
  /\ mode' = (IF step THEN "step" ELSE "run") /\ stepper' = s /\ ran' = {}
  /\ UNCHANGED <<born, known, held>>
\* a running thread creates another, which waits for its creation event to be answered
Create(t) == mode # "stopped" /\ t \notin born /\ (\E r \in known : count[r] = 0) /\ born' = born \cup {t}
             /\ UNCHANGED <<known, count, mode, stepper, held, ran>>
Announce(t) == mode # "stopped" /\ t \in born \ known /\ known' = known \cup {t}
               /\ count' = [count EXCEPT ![t] = IF mode = "step" /\ Variant # "nohold" THEN 1 ELSE 0]
               /\ UNCHANGED <<born, mode, stepper, held, ran>>
Execute(t) == mode # "stopped" /\ t \in known /\ count[t] = 0 /\ ran' = ran \cup {t}
              /\ UNCHANGED <<born, known, count, mode, stepper, held>>
\* what every stop does after a step
Release(c) == [t \in Threads |-> IF mode = "step" /\ t \in known /\ t # stepper /\ Variant # "noresume" THEN Less(c[t]) ELSE c[t]]
\* the step's event, or in a run any event the debugger stops for
Event == ((mode = "step" /\ stepper \in ran) \/ mode = "run") /\ count' = Release(count) /\ mode' = "stopped" /\ held' = TRUE
         /\ UNCHANGED <<born, known, stepper, ran>>
Halt == mode # "stopped" /\ count' = Release([t \in Threads |-> IF t \in known THEN count[t] + 1 ELSE count[t]])
        /\ mode' = "stopped" /\ held' = FALSE /\ UNCHANGED <<born, known, stepper, ran>>

Next == Event \/ Halt \/ (\E s \in Threads, step \in BOOLEAN : Resume(step, s)) \/ \E t \in Threads : Create(t) \/ Announce(t) \/ Execute(t)
Spec == Init /\ [][Next]_vars

Alone == mode = "step" => ran \subseteq {stepper}
Balanced == \A t \in Threads : (mode = "run" => count[t] = 0) /\ (mode = "stopped" => count[t] = IF held \/ t \notin known THEN 0 ELSE 1)
==========================================================================
