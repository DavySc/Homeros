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
     Once       no thread is put back onto an instruction it has already run

   Given up: Settled, "while the target is stopped no thread stands behind a trap byte: every thread's
   registers are the program's". Windows hands over one debug event of a process at a time, and a
   thread that has executed a trap byte may not even have queued its event when another thread's
   event (or a halt) stops the target. Its event comes in some later run, and until then the thread
   is seen one byte behind the trap. The system can tell that a thread is inside an exception (Flag;
   GetThreadContext with CONTEXT_EXCEPTION_REQUEST), and the first mechanism put every such thread
   that stood one byte behind a trap back onto it at the stop. But a thread can be inside another
   exception there: when the instruction under the trap is one byte long, a thread that ran it
   before the trap was written stands at X+1, and its next instruction can fault (a page of code that
   the system has to bring in again after the trap byte was written into it). Put back, it runs the
   instruction at X a second time. On Windows this happened about once in 3,000 stops with three
   threads on one breakpoint; with Flag the model shows it (Run_flag_twice).

   Mechanism. Resume writes the trap bytes, then lets the threads go (ContinueDebugEvent for
   a stop that came from an event, ResumeThread after a halt). Every stop (an event for a trap
   of this run; a halt, which suspends every thread) takes the trap bytes out. Only an event puts
   its thread back on X, for only the event says that the thread ran the trap byte; when X is a
   trap of this run the target stops and the event is held, not continued, which keeps every
   thread still; when it is not (and the program's own byte there is no breakpoint) the event is
   from an earlier run and the target runs on. A thread whose event has not been answered executes
   nothing.

   Variant picks the mechanism: "ok", or one of the broken ones that must fail. *)
EXTENDS Naturals, Sequences
CONSTANTS Threads, Variant, Flag

VARIABLES pc,        \* per thread: "away", "atX" (X is next), "behind" (at X+1, ran the trap byte), "torn" (went on from there),
                     \* "after" (at X+1, ran the program's instruction at X), "twice" (put back from there)
          fault,     \* per thread: inside an exception that is no trap's (its next instruction faulted)
          ev,        \* per thread, its breakpoint event: "none", "flight" (raised, not queued yet), "queued"
          mem,       \* the byte at X: "own" or "trap"
          queue,     \* the events the system has for the debugger, oldest first
          run,       \* "running", "stopped", or "restoring" (only the broken halt passes through it)
          held,      \* the thread whose event the debugger holds, or "none"
          suspended, \* the debugger has suspended every thread (a halt)
          wanted,    \* X is a trap of this run
          told       \* the thread the last stop was reported for, or "none"
vars == <<pc, fault, ev, mem, queue, run, held, suspended, wanted, told>>

Init == pc = [t \in Threads |-> "away"] /\ fault = [t \in Threads |-> FALSE] /\ ev = [t \in Threads |-> "none"] /\ mem = "own" /\ queue = <<>>
        /\ run = "stopped" /\ held = "none" /\ suspended = FALSE /\ wanted = FALSE /\ told = "none"

\* a thread executes the program's code only while nothing holds it
Free(t) == run = "running" /\ ~suspended /\ held = "none" /\ ev[t] = "none"
Still == UNCHANGED <<fault, ev, mem, queue, run, held, suspended, wanted, told>>

Arrive(t) == Free(t) /\ pc[t] = "away" /\ pc' = [pc EXCEPT ![t] = "atX"] /\ Still
Execute(t) == Free(t) /\ pc[t] = "atX"
              /\ (IF mem = "trap" THEN pc' = [pc EXCEPT ![t] = "behind"] /\ ev' = [ev EXCEPT ![t] = "flight"]
                                  ELSE pc' = [pc EXCEPT ![t] = "after"] /\ ev' = ev)
              /\ UNCHANGED <<fault, mem, queue, run, held, suspended, wanted, told>>
\* the instruction after X: it runs, or it first faults and the system mends that without the debugger
Leave(t) == Free(t) /\ pc[t] = "after" /\ ~fault[t] /\ pc' = [pc EXCEPT ![t] = "away"] /\ Still
Fault(t) == Free(t) /\ pc[t] = "after" /\ fault' = [fault EXCEPT ![t] = ~fault[t]]
            /\ UNCHANGED <<pc, ev, mem, queue, run, held, suspended, wanted, told>>
\* a thread that stands behind a trap byte with no event to wait for goes on: torn
Tear(t) == Free(t) /\ pc[t] = "behind" /\ pc' = [pc EXCEPT ![t] = "torn"] /\ Still
\* the system queues the event of a thread that ran a trap byte: at any time, whoever is stopped
Queue(t) == ev[t] = "flight" /\ ev' = [ev EXCEPT ![t] = "queued"] /\ queue' = Append(queue, t)
            /\ UNCHANGED <<pc, fault, mem, run, held, suspended, wanted, told>>

\* what a stop of the first mechanism did to the threads: those inside an exception one byte behind X go back onto it.
\* The system's answer is the same for a thread that ran the trap byte and for one whose next instruction faulted.
Settle(p) == IF ~Flag THEN p ELSE [t \in Threads |-> IF p[t] = "behind" /\ ev[t] # "none" THEN "atX"
                                                     ELSE IF p[t] = "after" /\ fault[t] THEN "twice" ELSE p[t]]

Resume == run = "stopped" /\ \E w \in BOOLEAN : wanted' = w /\ mem' = (IF w THEN "trap" ELSE "own")
          /\ run' = "running" /\ held' = "none" /\ suspended' = FALSE /\ told' = "none"
          /\ UNCHANGED <<pc, fault, ev, queue>>
\* the debugger takes the oldest event (the system gives none while one is held)
Event == run = "running" /\ held = "none" /\ queue # <<>> /\
         LET t == Head(queue) IN
           /\ queue' = Tail(queue) /\ ev' = [ev EXCEPT ![t] = "none"]
           /\ (IF wanted
               THEN /\ pc' = Settle([pc EXCEPT ![t] = IF Variant = "hitnorollback" THEN pc[t] ELSE "atX"])
                    /\ mem' = "own" /\ run' = "stopped" /\ held' = t /\ told' = t
               ELSE /\ pc' = [pc EXCEPT ![t] = IF Variant = "latenorollback" THEN pc[t] ELSE "atX"]
                    /\ UNCHANGED <<mem, run, held, told>>)
           /\ UNCHANGED <<fault, suspended, wanted>>
Halt == run = "running" /\ held = "none" /\ suspended' = TRUE /\ pc' = Settle(pc)
        /\ (IF Variant = "reportfirst" THEN run' = "restoring" /\ mem' = mem ELSE run' = "stopped" /\ mem' = "own")
        /\ UNCHANGED <<fault, ev, queue, held, wanted, told>>
\* the broken halt tells the editor first; a read of memory can come before this step
Restore == run = "restoring" /\ mem' = "own" /\ run' = "stopped"
           /\ UNCHANGED <<pc, fault, ev, queue, held, suspended, wanted, told>>

Next == Resume \/ Event \/ Halt \/ Restore \/ \E t \in Threads : Arrive(t) \/ Execute(t) \/ Leave(t) \/ Fault(t) \/ Tear(t) \/ Queue(t)
Spec == Init /\ [][Next]_vars

Clean == run # "running" => mem = "own"
Whole == \A t \in Threads : pc[t] # "torn"
Reported == told # "none" => pc[told] = "atX"
Once == \A t \in Threads : pc[t] # "twice"
Settled == run = "stopped" => \A t \in Threads : pc[t] # "behind"
==========================================================================
