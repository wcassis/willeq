# T350 — Auto-attack produced no swing in 12 live runs (B003): name the cause

Class A, unconfirmed since the reachability sweep; measurement first.
Slot3; eqemu lock (T348 may take it for a short live proof — the lock
decides).

1. Reproduce: Testchar (GM, `invulnerable=0` — T345 found invulnerable
   accounts refuse their own actions; restore afterwards), qeynos2, a
   GM-spawned mob (`#npctypespawn`, placed two units away per T342's
   note — never `#goto` onto it), `/target <name>`, `/attack on`. Record
   for 10 s (frame dump + client log at DEBUG for COMBAT/MOVEMENT/NET).
2. Classify from the log and packets, in this order: (a) does the client
   send `OP_AutoAttack` (on)? (b) does the server send `OP_Action`/
   `OP_Damage` swings back? (c) does fable's `CombatManager` tick run
   (the T245 combat update thread) and reach the attack-animation draw
   predicate (T309's rule: the model is drawn for the duration of an
   attack animation)? (d) does the renderer receive the animation event?
   The first missing link is the defect; name the function.
3. If the server never swings: compare with the official client (T328's
   rig) doing the same `/attack` — if the oracle also gets no swing, the
   defect is server/test setup and B003 closes as "not fable" with the
   evidence.
4. `review/T350/README.md` with the sequence table; B003 updated with the
   named cause (fixing it is a follow-up unit unless it is one line).

STOP: lock held; the mob cannot be spawned/targeted after two attempts.
