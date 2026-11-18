---- MODULE Queue_TTrace_1795031542 ----
EXTENDS Sequences, TLCExt, Toolbox, Naturals, TLC, Queue

_expression ==
    LET Queue_TEExpression == INSTANCE Queue_TEExpression
    IN Queue_TEExpression!expression
----

_trace ==
    LET Queue_TETrace == INSTANCE Queue_TETrace
    IN Queue_TETrace!trace
----

_prop ==
    ~<>[](
        evq = (<<"stopped">>)
        /\
        wake = (FALSE)
        /\
        made = (<<"running", "stopped", "running", "stopped">>)
        /\
        handled = (4)
        /\
        cmdq = (<<>>)
        /\
        quit = (FALSE)
        /\
        sent = (4)
        /\
        got = (<<"running", "stopped", "running">>)
        /\
        out = (<<>>)
        /\
        target = ("stopped")
        /\
        eng = ("take")
        /\
        ed = ("loop")
    )
----

_init ==
    /\ out = _TETrace[1].out
    /\ quit = _TETrace[1].quit
    /\ target = _TETrace[1].target
    /\ eng = _TETrace[1].eng
    /\ sent = _TETrace[1].sent
    /\ cmdq = _TETrace[1].cmdq
    /\ wake = _TETrace[1].wake
    /\ got = _TETrace[1].got
    /\ handled = _TETrace[1].handled
    /\ evq = _TETrace[1].evq
    /\ ed = _TETrace[1].ed
    /\ made = _TETrace[1].made
----

_next ==
    /\ \E i,j \in DOMAIN _TETrace:
        /\ \/ /\ j = i + 1
              /\ i = TLCGet("level")
        /\ out  = _TETrace[i].out
        /\ out' = _TETrace[j].out
        /\ quit  = _TETrace[i].quit
        /\ quit' = _TETrace[j].quit
        /\ target  = _TETrace[i].target
        /\ target' = _TETrace[j].target
        /\ eng  = _TETrace[i].eng
        /\ eng' = _TETrace[j].eng
        /\ sent  = _TETrace[i].sent
        /\ sent' = _TETrace[j].sent
        /\ cmdq  = _TETrace[i].cmdq
        /\ cmdq' = _TETrace[j].cmdq
        /\ wake  = _TETrace[i].wake
        /\ wake' = _TETrace[j].wake
        /\ got  = _TETrace[i].got
        /\ got' = _TETrace[j].got
        /\ handled  = _TETrace[i].handled
        /\ handled' = _TETrace[j].handled
        /\ evq  = _TETrace[i].evq
        /\ evq' = _TETrace[j].evq
        /\ ed  = _TETrace[i].ed
        /\ ed' = _TETrace[j].ed
        /\ made  = _TETrace[i].made
        /\ made' = _TETrace[j].made

\* Uncomment the ASSUME below to write the states of the error trace
\* to the given file in Json format. Note that you can pass any tuple
\* to `JsonSerialize`. For example, a sub-sequence of _TETrace.
    \* ASSUME
    \*     LET J == INSTANCE Json
    \*         IN J!JsonSerialize("Queue_TTrace_1795031542.json", _TETrace)

=============================================================================

 Note that you can extract this module `Queue_TEExpression`
  to a dedicated file to reuse `expression` (the module in the 
  dedicated `Queue_TEExpression.tla` file takes precedence 
  over the module `Queue_TEExpression` below).

---- MODULE Queue_TEExpression ----
EXTENDS Sequences, TLCExt, Toolbox, Naturals, TLC, Queue

expression == 
    [
        \* To hide variables of the `Queue` spec from the error trace,
        \* remove the variables below.  The trace will be written in the order
        \* of the fields of this record.
        out |-> out
        ,quit |-> quit
        ,target |-> target
        ,eng |-> eng
        ,sent |-> sent
        ,cmdq |-> cmdq
        ,wake |-> wake
        ,got |-> got
        ,handled |-> handled
        ,evq |-> evq
        ,ed |-> ed
        ,made |-> made
        
        \* Put additional constant-, state-, and action-level expressions here:
        \* ,_stateNumber |-> _TEPosition
        \* ,_outUnchanged |-> out = out'
        
        \* Format the `out` variable as Json value.
        \* ,_outJson |->
        \*     LET J == INSTANCE Json
        \*     IN J!ToJson(out)
        
        \* Lastly, you may build expressions over arbitrary sets of states by
        \* leveraging the _TETrace operator.  For example, this is how to
        \* count the number of times a spec variable changed up to the current
        \* state in the trace.
        \* ,_outModCount |->
        \*     LET F[s \in DOMAIN _TETrace] ==
        \*         IF s = 1 THEN 0
        \*         ELSE IF _TETrace[s].out # _TETrace[s-1].out
        \*             THEN 1 + F[s-1] ELSE F[s-1]
        \*     IN F[_TEPosition - 1]
    ]

=============================================================================



Parsing and semantic processing can take forever if the trace below is long.
 In this case, it is advised to uncomment the module below to deserialize the
 trace from a generated binary file.

\*
\*---- MODULE Queue_TETrace ----
\*EXTENDS IOUtils, TLC, Queue
\*
\*trace == IODeserialize("Queue_TTrace_1795031542.bin", TRUE)
\*
\*=============================================================================
\*

---- MODULE Queue_TETrace ----
EXTENDS TLC, Queue

trace == 
    <<
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<>>,handled |-> 0,cmdq |-> <<>>,quit |-> FALSE,sent |-> 0,got |-> <<>>,out |-> <<>>,target |-> "stopped",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<>>,handled |-> 0,cmdq |-> <<"run">>,quit |-> FALSE,sent |-> 1,got |-> <<>>,out |-> <<>>,target |-> "stopped",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<>>,handled |-> 1,cmdq |-> <<>>,quit |-> FALSE,sent |-> 1,got |-> <<>>,out |-> <<"running">>,target |-> "running",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> TRUE,made |-> <<>>,handled |-> 1,cmdq |-> <<>>,quit |-> FALSE,sent |-> 1,got |-> <<>>,out |-> <<"running">>,target |-> "running",eng |-> "push",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<>>,handled |-> 1,cmdq |-> <<>>,quit |-> FALSE,sent |-> 1,got |-> <<>>,out |-> <<"running">>,target |-> "running",eng |-> "push",ed |-> "take"]),
    ([evq |-> <<"running">>,wake |-> FALSE,made |-> <<"running">>,handled |-> 1,cmdq |-> <<>>,quit |-> FALSE,sent |-> 1,got |-> <<>>,out |-> <<>>,target |-> "running",eng |-> "take",ed |-> "take"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running">>,handled |-> 1,cmdq |-> <<>>,quit |-> FALSE,sent |-> 1,got |-> <<"running">>,out |-> <<>>,target |-> "running",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running">>,handled |-> 1,cmdq |-> <<"halt">>,quit |-> FALSE,sent |-> 2,got |-> <<"running">>,out |-> <<>>,target |-> "running",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running">>,handled |-> 2,cmdq |-> <<>>,quit |-> FALSE,sent |-> 2,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running">>,handled |-> 2,cmdq |-> <<"run">>,quit |-> FALSE,sent |-> 3,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running">>,handled |-> 2,cmdq |-> <<"run", "halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<>>,wake |-> TRUE,made |-> <<"running">>,handled |-> 2,cmdq |-> <<"run", "halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "push",ed |-> "loop"]),
    ([evq |-> <<"stopped">>,wake |-> TRUE,made |-> <<"running", "stopped">>,handled |-> 2,cmdq |-> <<"run", "halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<>>,target |-> "stopped",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<"stopped">>,wake |-> TRUE,made |-> <<"running", "stopped">>,handled |-> 3,cmdq |-> <<"halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"running">>,target |-> "running",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<"stopped">>,wake |-> TRUE,made |-> <<"running", "stopped">>,handled |-> 3,cmdq |-> <<"halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"running">>,target |-> "running",eng |-> "push",ed |-> "loop"]),
    ([evq |-> <<"stopped", "running">>,wake |-> TRUE,made |-> <<"running", "stopped", "running">>,handled |-> 3,cmdq |-> <<"halt">>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<>>,target |-> "running",eng |-> "take",ed |-> "loop"]),
    ([evq |-> <<"stopped", "running">>,wake |-> TRUE,made |-> <<"running", "stopped", "running">>,handled |-> 4,cmdq |-> <<>>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "signal",ed |-> "loop"]),
    ([evq |-> <<"stopped", "running">>,wake |-> TRUE,made |-> <<"running", "stopped", "running">>,handled |-> 4,cmdq |-> <<>>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "push",ed |-> "loop"]),
    ([evq |-> <<"stopped", "running">>,wake |-> FALSE,made |-> <<"running", "stopped", "running">>,handled |-> 4,cmdq |-> <<>>,quit |-> FALSE,sent |-> 4,got |-> <<"running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "push",ed |-> "take"]),
    ([evq |-> <<>>,wake |-> FALSE,made |-> <<"running", "stopped", "running">>,handled |-> 4,cmdq |-> <<>>,quit |-> FALSE,sent |-> 4,got |-> <<"running", "stopped", "running">>,out |-> <<"stopped">>,target |-> "stopped",eng |-> "push",ed |-> "loop"]),
    ([evq |-> <<"stopped">>,wake |-> FALSE,made |-> <<"running", "stopped", "running", "stopped">>,handled |-> 4,cmdq |-> <<>>,quit |-> FALSE,sent |-> 4,got |-> <<"running", "stopped", "running">>,out |-> <<>>,target |-> "stopped",eng |-> "take",ed |-> "loop"])
    >>
----


=============================================================================

---- CONFIG Queue_TTrace_1795031542 ----
CONSTANTS
    Cap = 2
    MaxCmds = 4
    Variant = "signalfirst"

PROPERTY
    _prop

CHECK_DEADLOCK
    \* CHECK_DEADLOCK off because of PROPERTY or INVARIANT above.
    FALSE

INIT
    _init

NEXT
    _next

CONSTANT
    _TETrace <- _trace

ALIAS
    _expression
=============================================================================
\* Generated on Wed Nov 18 19:52:23 UTC 2026