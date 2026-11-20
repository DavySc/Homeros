-------------------------------- MODULE Halt --------------------------------
(* Halting a running target from the editor, through the command queue (dbg/session.c; the
   queue itself is specs/Queue.tla).

   Properties, stated before the mechanism:
     OneStop   every run command is answered by exactly one stop event
     OwnHalt   a halt stops only the run during which the editor sent it: never a later one
     OneAnswer every ask (ask.h: what the editor wants to see of the stopped program) is answered by exactly one
               event, in the order asked
     Untouched the program is never looked at while it runs

   What makes it hard: the editor sends a halt while it believes the target runs, and the
   target may have stopped by itself a moment before. That halt is then still in the queue
   when the engine is already stopped, and the editor's next run command comes behind it.

   Mechanism. The engine thread takes commands in order. Stopped, it waits for one: a run
   starts the target, a halt is dropped. Running, it looks at the queue between waits for
   debug events: a halt stops the target. The editor sends a run only after it has taken the
   stop event of the run before, so a late halt always lies before the next run in the queue.
   An ask may come at any time. Stopped, the engine answers it from the program. Running, it answers it at once
   as stale, with nothing: the editor keeps what it showed (RAD's rule). So an answer that is not stale always
   comes after the stop event of the stop it is about.

   Variants that must fail:
     "sticky"   a halt taken while stopped is remembered and stops the next run
     "eager"    the editor sends the next run before it has taken the stop event: its halt
                for the new run can stop the old one, and the new run is never halted
     "drop"     an ask taken while the target runs is dropped, like a run taken then: it is never answered
     "look"     an ask taken while the target runs is answered from the program *)
EXTENDS Naturals, Sequences

CONSTANTS MaxRuns, MaxAsks, Variant

VARIABLES commands,  \* the queue to the engine: <<"run", n>> or <<"halt", n>>, n the run the editor meant; <<"ask", k>>
          events,    \* the queue to the editor: <<"stop", n>> for a run that stopped, <<"answer", k, stale, the run before it>>
          asked,     \* asks the editor has sent
          answers,   \* per ask: how many answers it has had
          last,      \* the ask answered last
          skipped,   \* an answer came out of turn
          looked,    \* the program was looked at while it ran
          sent,      \* runs the editor has sent
          taken,     \* stop events the editor has taken
          run,       \* the engine: the run it is in or was last in
          running,   \* the engine: the target runs
          sticky,    \* the engine, variant "sticky": the run a remembered halt was meant for, or 0
          stops,     \* per run: how many stop events it has had
          wrong      \* a halt has stopped a run it was not sent for
vars == <<commands, events, asked, answers, last, skipped, looked, sent, taken, run, running, sticky, stops, wrong>>
asking == <<asked, answers, last, skipped, looked>>

Init == commands = <<>> /\ events = <<>> /\ sent = 0 /\ taken = 0 /\ run = 0 /\ running = FALSE
        /\ sticky = 0 /\ stops = [n \in 1..MaxRuns |-> 0] /\ wrong = FALSE
        /\ asked = 0 /\ answers = [k \in 1..MaxAsks |-> 0] /\ last = 0 /\ skipped = FALSE /\ looked = FALSE

\* the editor
SendRun == /\ sent < MaxRuns /\ (taken = sent \/ Variant = "eager")
           /\ sent' = sent + 1 /\ commands' = Append(commands, <<"run", sent + 1>>)
           /\ UNCHANGED <<events, taken, run, running, sticky, stops, wrong, asking>>
SendAsk == /\ asked < MaxAsks /\ Len(commands) < 3
           /\ asked' = asked + 1 /\ commands' = Append(commands, <<"ask", asked + 1>>)
           /\ UNCHANGED <<events, answers, last, skipped, looked, sent, taken, run, running, sticky, stops, wrong>>
SendHalt == /\ sent > taken /\ Len(commands) < 3   \* it believes the target runs (and does not press for ever)
            /\ commands' = Append(commands, <<"halt", sent>>)
            /\ UNCHANGED <<events, sent, taken, run, running, sticky, stops, wrong, asking>>
Take == /\ events # <<>> /\ events' = Tail(events) /\ taken' = taken + (IF Head(events)[1] = "stop" THEN 1 ELSE 0)
        /\ UNCHANGED <<commands, sent, run, running, sticky, stops, wrong, asking>>

\* the engine
Stop(halted_for) == /\ running' = FALSE /\ events' = Append(events, <<"stop", run>>)
                    /\ stops' = [stops EXCEPT ![run] = @ + 1]
                    /\ wrong' = (wrong \/ (halted_for # 0 /\ halted_for # run))
Answer(k) == LET stale == running /\ Variant # "look" IN
             /\ events' = Append(events, <<"answer", k, stale, run>>) /\ answers' = [answers EXCEPT ![k] = @ + 1] /\ last' = k
             /\ skipped' = (skipped \/ k # last + 1) /\ looked' = (looked \/ (running /\ ~stale))
             /\ UNCHANGED <<asked, run, running, sticky, stops, wrong>>
Command == /\ commands # <<>> /\ commands' = Tail(commands)
           /\ LET c == Head(commands) IN
              IF c[1] = "ask" /\ ~(running /\ Variant = "drop") THEN Answer(c[2])
              ELSE /\ UNCHANGED asking
                   /\ IF running
                      THEN IF c[1] = "halt" THEN Stop(c[2]) /\ UNCHANGED <<run, sticky>>
                           ELSE UNCHANGED <<run, running, sticky, events, stops, wrong>>   \* a run sent while it runs is dropped
                      ELSE IF c[1] = "run"
                           THEN /\ run' = c[2] /\ running' = TRUE /\ UNCHANGED <<sticky, events, stops, wrong>>
                           ELSE /\ sticky' = (IF Variant = "sticky" /\ c[1] = "halt" THEN c[2] ELSE 0)
                                /\ UNCHANGED <<run, running, events, stops, wrong>>
           /\ UNCHANGED <<sent, taken>>
Remembered == /\ running /\ sticky # 0 /\ Stop(sticky) /\ sticky' = 0 /\ UNCHANGED <<commands, sent, taken, run, asking>>
ByItself == /\ running /\ Stop(0) /\ UNCHANGED <<commands, sent, taken, run, sticky, asking>>

Next == SendRun \/ SendAsk \/ SendHalt \/ Take \/ Command \/ Remembered \/ ByItself
Spec == Init /\ [][Next]_vars /\ WF_vars(Command) /\ WF_vars(ByItself) /\ WF_vars(Take)

OwnHalt == ~wrong
AtMostOne == \A n \in 1..MaxRuns : stops[n] <= 1
\* every run the editor sent stops once (a target left alone stops by itself in the end: its exit is a stop)
OneStop == \A n \in 1..MaxRuns : (sent >= n) ~> (stops[n] = 1)
Untouched == ~looked
InTurn == ~skipped
AtMostOneAnswer == \A k \in 1..MaxAsks : answers[k] <= 1
OneAnswer == \A k \in 1..MaxAsks : (asked >= k) ~> (answers[k] = 1)
\* an answer with something in it comes behind the event of the stop it is about: the editor has that event first
Behind == \A i \in 1..Len(events) : (events[i][1] = "answer" /\ ~events[i][3]) => \A j \in i..Len(events) : events[j] # <<"stop", events[i][4]>>
=============================================================================
