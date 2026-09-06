# T428 — fable's night base matched to the oracle (B280)

Class A; eqemu lock (atomic; must be ABSENT); the slot that frees. Read
B280 (oracle night base R .01900 / G .03333 / B .02797, grey .02675;
fable R .00540 / G .00453 / B .00445, grey .00478 — 5.6x darker and a
different colour; every vision number is a ratio because of it), T408's
§Review (the night deficit is UNIFORM: ×2.27 lamp-free, ×2.09 by a lamp —
so it is the ambient, not a lamp term), T405's rig (heading pinned via
`#goto x y z h`, rain_chance zeroed, `#set time 22 0` re-pinned per
window, `#weather 3 1 0`), T423 (the ambient recomposes on any input;
`composeSceneAmbient(dayNight, weather, vision)`), and
`updateTimeOfDay()`'s night ambient (0.08, 0.08, 0.15) and its day/night
curve.

1. Live A/B on the night ambient's three channels (and, if the curve's
   shape matters, the dusk/dawn interpolation): probe builds, one
   parameter at a time, until fable's unbuffed night base matches the
   oracle's per channel within the noise floor at T405's spot AND at
   T367's lamp spot (both, so a lamp term is not being hidden). Every
   value carries its probe. Then re-run the vision ratios (T405's
   constants were solved against the OLD base — they may need re-solving:
   do it in the same session, same method).
2. Day must not move: the gate's zones are daytime — run the gate after
   each probe; if an image golden moves, STOP with the contact sheet (a
   night-base change that moves a daytime golden means the curve is
   coupled — report the coupling).
3. Tests pin the solved night base and the re-solved vision constants
   (pre-fix failing). Strike B280; annotate B067/B203.

Restore every server value (world DOWN); `down.sh`; delete the lock.

STOP: lock present; a daytime golden moves.
