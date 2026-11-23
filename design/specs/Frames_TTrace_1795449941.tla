---- MODULE Frames_TTrace_1795449941 ----
EXTENDS Frames, Sequences, TLCExt, Toolbox, Naturals, TLC

_expression ==
    LET Frames_TEExpression == INSTANCE Frames_TEExpression
    IN Frames_TEExpression!expression
----

_trace ==
    LET Frames_TETrace == INSTANCE Frames_TETrace
    IN Frames_TETrace!trace
----

_inv ==
    ~(
        TLCGet("level") = Len(_TETrace)
        /\
        dirty = (<<FALSE, TRUE>>)
        /\
        typed = (2)
        /\
        vis = ({1})
        /\
        touched = (<<FALSE, TRUE>>)
        /\
        shown = (<<0, 0>>)
        /\
        need = (<<2, 2>>)
        /\
        idle = (FALSE)
        /\
        flips = (0)
        /\
        wasted = (TRUE)
        /\
        drawn = (<<2, 0>>)
        /\
        cb = (<<TRUE, FALSE>>)
    )
----

_init ==
    /\ touched = _TETrace[1].touched
    /\ typed = _TETrace[1].typed
    /\ dirty = _TETrace[1].dirty
    /\ need = _TETrace[1].need
    /\ wasted = _TETrace[1].wasted
    /\ shown = _TETrace[1].shown
    /\ vis = _TETrace[1].vis
    /\ idle = _TETrace[1].idle
    /\ flips = _TETrace[1].flips
    /\ cb = _TETrace[1].cb
    /\ drawn = _TETrace[1].drawn
----

_next ==
    /\ \E i,j \in DOMAIN _TETrace:
        /\ \/ /\ j = i + 1
              /\ i = TLCGet("level")
        /\ touched  = _TETrace[i].touched
        /\ touched' = _TETrace[j].touched
        /\ typed  = _TETrace[i].typed
        /\ typed' = _TETrace[j].typed
        /\ dirty  = _TETrace[i].dirty
        /\ dirty' = _TETrace[j].dirty
        /\ need  = _TETrace[i].need
        /\ need' = _TETrace[j].need
        /\ wasted  = _TETrace[i].wasted
        /\ wasted' = _TETrace[j].wasted
        /\ shown  = _TETrace[i].shown
        /\ shown' = _TETrace[j].shown
        /\ vis  = _TETrace[i].vis
        /\ vis' = _TETrace[j].vis
        /\ idle  = _TETrace[i].idle
        /\ idle' = _TETrace[j].idle
        /\ flips  = _TETrace[i].flips
        /\ flips' = _TETrace[j].flips
        /\ cb  = _TETrace[i].cb
        /\ cb' = _TETrace[j].cb
        /\ drawn  = _TETrace[i].drawn
        /\ drawn' = _TETrace[j].drawn

\* Uncomment the ASSUME below to write the states of the error trace
\* to the given file in Json format. Note that you can pass any tuple
\* to `JsonSerialize`. For example, a sub-sequence of _TETrace.
    \* ASSUME
    \*     LET J == INSTANCE Json
    \*         IN J!JsonSerialize("Frames_TTrace_1795449941.json", _TETrace)

=============================================================================

 Note that you can extract this module `Frames_TEExpression`
  to a dedicated file to reuse `expression` (the module in the 
  dedicated `Frames_TEExpression.tla` file takes precedence 
  over the module `Frames_TEExpression` below).

---- MODULE Frames_TEExpression ----
EXTENDS Frames, Sequences, TLCExt, Toolbox, Naturals, TLC

expression == 
    [
        \* To hide variables of the `Frames` spec from the error trace,
        \* remove the variables below.  The trace will be written in the order
        \* of the fields of this record.
        touched |-> touched
        ,typed |-> typed
        ,dirty |-> dirty
        ,need |-> need
        ,wasted |-> wasted
        ,shown |-> shown
        ,vis |-> vis
        ,idle |-> idle
        ,flips |-> flips
        ,cb |-> cb
        ,drawn |-> drawn
        
        \* Put additional constant-, state-, and action-level expressions here:
        \* ,_stateNumber |-> _TEPosition
        \* ,_touchedUnchanged |-> touched = touched'
        
        \* Format the `touched` variable as Json value.
        \* ,_touchedJson |->
        \*     LET J == INSTANCE Json
        \*     IN J!ToJson(touched)
        
        \* Lastly, you may build expressions over arbitrary sets of states by
        \* leveraging the _TETrace operator.  For example, this is how to
        \* count the number of times a spec variable changed up to the current
        \* state in the trace.
        \* ,_touchedModCount |->
        \*     LET F[s \in DOMAIN _TETrace] ==
        \*         IF s = 1 THEN 0
        \*         ELSE IF _TETrace[s].touched # _TETrace[s-1].touched
        \*             THEN 1 + F[s-1] ELSE F[s-1]
        \*     IN F[_TEPosition - 1]
    ]

=============================================================================



Parsing and semantic processing can take forever if the trace below is long.
 In this case, it is advised to uncomment the module below to deserialize the
 trace from a generated binary file.

\*
\*---- MODULE Frames_TETrace ----
\*EXTENDS Frames, IOUtils, TLC
\*
\*trace == IODeserialize("Frames_TTrace_1795449941.bin", TRUE)
\*
\*=============================================================================
\*

---- MODULE Frames_TETrace ----
EXTENDS Frames, TLC

trace == 
    <<
    ([dirty |-> <<FALSE, FALSE>>,typed |-> 0,vis |-> {1},touched |-> <<FALSE, FALSE>>,shown |-> <<0, 0>>,need |-> <<0, 0>>,idle |-> FALSE,flips |-> 0,wasted |-> FALSE,drawn |-> <<0, 0>>,cb |-> <<FALSE, FALSE>>]),
    ([dirty |-> <<TRUE, FALSE>>,typed |-> 1,vis |-> {1},touched |-> <<TRUE, FALSE>>,shown |-> <<0, 0>>,need |-> <<1, 0>>,idle |-> FALSE,flips |-> 0,wasted |-> FALSE,drawn |-> <<0, 0>>,cb |-> <<FALSE, FALSE>>]),
    ([dirty |-> <<FALSE, FALSE>>,typed |-> 1,vis |-> {1},touched |-> <<FALSE, FALSE>>,shown |-> <<0, 0>>,need |-> <<1, 0>>,idle |-> FALSE,flips |-> 0,wasted |-> FALSE,drawn |-> <<1, 0>>,cb |-> <<TRUE, FALSE>>]),
    ([dirty |-> <<TRUE, TRUE>>,typed |-> 2,vis |-> {1},touched |-> <<TRUE, TRUE>>,shown |-> <<0, 0>>,need |-> <<2, 2>>,idle |-> FALSE,flips |-> 0,wasted |-> FALSE,drawn |-> <<1, 0>>,cb |-> <<TRUE, FALSE>>]),
    ([dirty |-> <<FALSE, TRUE>>,typed |-> 2,vis |-> {1},touched |-> <<FALSE, TRUE>>,shown |-> <<0, 0>>,need |-> <<2, 2>>,idle |-> FALSE,flips |-> 0,wasted |-> TRUE,drawn |-> <<2, 0>>,cb |-> <<TRUE, FALSE>>])
    >>
----


=============================================================================

---- CONFIG Frames_TTrace_1795449941 ----
CONSTANTS
    Keys = 3
    Frames = { 1 , 2 }
    Visible0 = { 1 }
    Flips = 0
    Variant = "nocb"

INVARIANT
    _inv

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
\* Generated on Mon Nov 23 16:05:41 UTC 2026