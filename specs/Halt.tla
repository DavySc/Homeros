-------------------------------- MODULE Halt --------------------------------
(* Halting a running target from the editor, through the command queue (dbg/session.c; the
   queue itself is specs/Queue.tla).

   Properties, stated before the mechanism:
     OneStop   every run command is answered by exactly one stop event
     OwnHalt   a halt stops only the run during which the editor sent it: never a later one

   What makes it hard: the editor sends a halt while it believes the target runs, and the
   target may have stopped by itself a moment before. That halt is then still in the queue
   when the engine is already stopped, and the editor's next run command comes behind it.

   Mechanism. The engine thread takes commands in order. Stopped, it waits for one: a run
   starts the target, a halt is dropped. Running, it looks at the queue between waits for
   debug events: a halt stops the target. The editor sends a run only after it has taken the
   stop event of the run before, so a late halt always lies before the next run in the queue.

   Variants that must fail:
     "sticky"   a halt taken while stopped is remembered and stops the next run
     "eager"    the editor sends the next run before it has taken the stop event: its halt
                for the new run can stop the old one, and the new run is never halted *)
EXTENDS Naturals, Sequences

CONSTANTS MaxRuns, Variant

VARIABLES commands,  \* the queue to the engine: <<"run", n>> or <<"halt", n>>, n the run the editor meant
          events,    \* the queue to the editor: the numbers of the runs that stopped
          sent,      \* runs the editor has sent
          taken,     \* stop events the editor has taken
          run,       \* the engine: the run it is in or was last in
          running,   \* the engine: the target runs
          sticky,    \* the engine, variant "sticky": the run a remembered halt was meant for, or 0
          stops,     \* per run: how many stop events it has had
          wrong      \* a halt has stopped a run it was not sent for
vars == <<commands, events, sent, taken, run, running, sticky, stops, wrong>>

Init == commands = <<>> /\ events = <<>> /\ sent = 0 /\ taken = 0 /\ run = 0 /\ running = FALSE
        /\ sticky = 0 /\ stops = [n \in 1..MaxRuns |-> 0] /\ wrong = FALSE

\* the editor
SendRun == /\ sent < MaxRuns /\ (taken = sent \/ Variant = "eager")
           /\ sent' = sent + 1 /\ commands' = Append(commands, <<"run", sent + 1>>)
           /\ UNCHANGED <<events, taken, run, running, sticky, stops, wrong>>
SendHalt == /\ sent > taken /\ Len(commands) < 3   \* it believes the target runs (and does not press for ever)
            /\ commands' = Append(commands, <<"halt", sent>>)
            /\ UNCHANGED <<events, sent, taken, run, running, sticky, stops, wrong>>
Take == /\ events # <<>> /\ events' = Tail(events) /\ taken' = taken + 1
        /\ UNCHANGED <<commands, sent, run, running, sticky, stops, wrong>>

\* the engine
Stop(halted_for) == /\ running' = FALSE /\ events' = Append(events, run)
                    /\ stops' = [stops EXCEPT ![run] = @ + 1]
                    /\ wrong' = (wrong \/ (halted_for # 0 /\ halted_for # run))
Command == /\ commands # <<>> /\ commands' = Tail(commands)
           /\ LET c == Head(commands) IN
              IF running
              THEN IF c[1] = "halt" THEN Stop(c[2]) /\ UNCHANGED <<run, sticky>>
                   ELSE UNCHANGED <<run, running, sticky, events, stops, wrong>>   \* a run sent while it runs is dropped
              ELSE IF c[1] = "run"
                   THEN /\ run' = c[2] /\ running' = TRUE /\ UNCHANGED <<sticky, events, stops, wrong>>
                   ELSE /\ sticky' = (IF Variant = "sticky" THEN c[2] ELSE 0)
                        /\ UNCHANGED <<run, running, events, stops, wrong>>
           /\ UNCHANGED <<sent, taken>>
Remembered == /\ running /\ sticky # 0 /\ Stop(sticky) /\ sticky' = 0 /\ UNCHANGED <<commands, sent, taken, run>>
ByItself == /\ running /\ Stop(0) /\ UNCHANGED <<commands, sent, taken, run, sticky>>

Next == SendRun \/ SendHalt \/ Take \/ Command \/ Remembered \/ ByItself
Spec == Init /\ [][Next]_vars /\ WF_vars(Command) /\ WF_vars(ByItself) /\ WF_vars(Take)

OwnHalt == ~wrong
AtMostOne == \A n \in 1..MaxRuns : stops[n] <= 1
\* every run the editor sent stops once (a target left alone stops by itself in the end: its exit is a stop)
OneStop == \A n \in 1..MaxRuns : (sent >= n) ~> (stops[n] = 1)
=============================================================================
