----------------------------- MODULE FramesOwn -----------------------------
(* Review of PR 132: a property without time that rejects "all" even when  *)
(* every frame is visible (a 144 Hz frame held to a 60 Hz one's callback). *)
EXTENDS Frames
Own == \A f \in Frames : dirty[f] /\ ~cb[f] => ENABLED Draw(f)
=============================================================================
