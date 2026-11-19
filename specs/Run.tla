------------------------------- MODULE Run -------------------------------
(* One run of the target under process control on Windows (dbg/win/target.c), seen at one code
   address X where the debugger may plant a breakpoint instruction ("trap"), with several
   target threads.

   Properties, stated before the mechanism:
     Clean      while the target is stopped its memory holds the program's own bytes: a reader
                never sees a trap byte, and nothing has to remember which traps are "in"
     Whole      no thread ever goes on from X+1 after executing a trap byte: the instruction the
                trap byte replaced runs, whole, before the thread moves on
     Reported   a stop the debugger reports for a trap names a thread that stands at X with the
                program's instruction still to run
     Settled    while the target is stopped no thread stands behind a trap byte: every thread's
                registers are the program's

   What makes Settled hard: Windows hands over one debug event of a process at a time, and a
   thread that has executed a trap byte may not even have queued its event when another
   thread's event (or a halt) stops the target. Its event comes in some later run.

   Mechanism. Resume writes the trap bytes, then lets the threads go (ContinueDebugEvent for
   a stop that came from an event, ResumeThread after a halt). Every stop (an event for a trap
   of this run; a halt, which suspends every thread) puts each thread that is inside an
   exception just behind a trap byte back onto it, and takes the trap bytes out. An event's
   thread is put back on X; when X is a trap of this run the target stops and the event is
   held, not continued, which keeps every thread still; when it is not (and the program's own
   byte there is no breakpoint) the event is from an earlier run and the target runs on.
   A thread whose event has not been answered executes nothing.

   Settled rests on one thing the system must do: tell, when asked, that a thread is inside an
   exception (Flag; GetThreadContext with CONTEXT_EXCEPTION_REQUEST). Clean, Whole and Reported
   do not: without it the two puts-back at an event still carry them, which is why the code
   keeps both although, with the flag, every thread an event names was put back at a stop before.

   Variant picks the mechanism: "ok", or one of the broken ones that must fail. *)
EXTENDS Naturals, Sequences
CONSTANTS Threads, Variant, Flag

VARIABLES pc,        \* per thread: "away", "atX" (X is next), "behind" (at X+1, ran the trap byte), "torn" (went on from there)
          ev,        \* per thread, its breakpoint event: "none", "flight" (raised, not queued yet), "queued"
          mem,       \* the byte at X: "own" or "trap"
          queue,     \* the events the system has for the debugger, oldest first
          run,       \* "running", "stopped", or "restoring" (only the broken halt passes through it)
          held,      \* the thread whose event the debugger holds, or "none"
          suspended, \* the debugger has suspended every thread (a halt)
          wanted,    \* X is a trap of this run
          told       \* the thread the last stop was reported for, or "none"
vars == <<pc, ev, mem, queue, run, held, suspended, wanted, told>>

Init == pc = [t \in Threads |-> "away"] /\ ev = [t \in Threads |-> "none"] /\ mem = "own" /\ queue = <<>>
        /\ run = "stopped" /\ held = "none" /\ suspended = FALSE /\ wanted = FALSE /\ told = "none"

\* a thread executes the program's code only while nothing holds it
Free(t) == run = "running" /\ ~suspended /\ held = "none" /\ ev[t] = "none"

Arrive(t) == Free(t) /\ pc[t] = "away" /\ pc' = [pc EXCEPT ![t] = "atX"]
             /\ UNCHANGED <<ev, mem, queue, run, held, suspended, wanted, told>>
Execute(t) == Free(t) /\ pc[t] = "atX"
              /\ (IF mem = "trap" THEN pc' = [pc EXCEPT ![t] = "behind"] /\ ev' = [ev EXCEPT ![t] = "flight"]
                                  ELSE pc' = [pc EXCEPT ![t] = "away"] /\ ev' = ev)
              /\ UNCHANGED <<mem, queue, run, held, suspended, wanted, told>>
\* a thread that stands behind a trap byte with no event to wait for goes on: torn
Tear(t) == Free(t) /\ pc[t] = "behind" /\ pc' = [pc EXCEPT ![t] = "torn"]
           /\ UNCHANGED <<ev, mem, queue, run, held, suspended, wanted, told>>
\* the system queues the event of a thread that ran a trap byte: at any time, whoever is stopped
Queue(t) == ev[t] = "flight" /\ ev' = [ev EXCEPT ![t] = "queued"] /\ queue' = Append(queue, t)
            /\ UNCHANGED <<pc, mem, run, held, suspended, wanted, told>>

\* what every stop does to the threads: those inside an exception behind a trap byte go back onto it
Settle(p) == IF ~Flag THEN p ELSE [t \in Threads |-> IF p[t] = "behind" /\ ev[t] # "none" THEN "atX" ELSE p[t]]

Resume == run = "stopped" /\ \E w \in BOOLEAN : wanted' = w /\ mem' = (IF w THEN "trap" ELSE "own")
          /\ run' = "running" /\ held' = "none" /\ suspended' = FALSE /\ told' = "none"
          /\ UNCHANGED <<pc, ev, queue>>
\* the debugger takes the oldest event (the system gives none while one is held)
Event == run = "running" /\ held = "none" /\ queue # <<>> /\
         LET t == Head(queue) IN
           /\ queue' = Tail(queue) /\ ev' = [ev EXCEPT ![t] = "none"]
           /\ (IF wanted
               THEN /\ pc' = Settle([pc EXCEPT ![t] = IF Variant = "hitnorollback" THEN pc[t] ELSE "atX"])
                    /\ mem' = "own" /\ run' = "stopped" /\ held' = t /\ told' = t
               ELSE /\ pc' = [pc EXCEPT ![t] = IF Variant = "latenorollback" THEN pc[t] ELSE "atX"]
                    /\ UNCHANGED <<mem, run, held, told>>)
           /\ UNCHANGED <<suspended, wanted>>
Halt == run = "running" /\ held = "none" /\ suspended' = TRUE /\ pc' = Settle(pc)
        /\ (IF Variant = "reportfirst" THEN run' = "restoring" /\ mem' = mem ELSE run' = "stopped" /\ mem' = "own")
        /\ UNCHANGED <<ev, queue, held, wanted, told>>
\* the broken halt tells the editor first; a read of memory can come before this step
Restore == run = "restoring" /\ mem' = "own" /\ run' = "stopped"
           /\ UNCHANGED <<pc, ev, queue, held, suspended, wanted, told>>

Next == Resume \/ Event \/ Halt \/ Restore \/ \E t \in Threads : Arrive(t) \/ Execute(t) \/ Tear(t) \/ Queue(t)
Spec == Init /\ [][Next]_vars

Clean == run # "running" => mem = "own"
Whole == \A t \in Threads : pc[t] # "torn"
Reported == told # "none" => pc[told] = "atX"
Settled == run = "stopped" => \A t \in Threads : pc[t] # "behind"
==========================================================================
