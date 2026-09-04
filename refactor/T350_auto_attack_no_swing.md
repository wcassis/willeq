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

---

## Review

**B003 closes as NOT FABLE for the auto-attack path.** The first missing
link is (b) — the server never sends `OP_Damage` — and the reason is that
eqemu refuses the swing when the player is not FACING the target, in the
one branch of `Client::Process` that is completely silent:

```
// zone/client_process.cpp:430
if (!CombatRange(t))                        MessageString(TARGET_TOO_FAR);
else if (t == this)                         MessageString(TRY_ATTACKING_SOMEONE);
else if (!los_status || !los_status_facing) { }   // no packet, no message, no log
else if (... IsAttackAllowed(t))            DoAttackRounds(t, slotPrimary);
```

`los_status_facing` is `Mob::IsFacingMob` (`zone/mob.cpp:7599`), tolerance
+-80 of 511.5 heading units == +-56 degrees. Nothing in fable's `/target`
or `/attack` path sets the heading, and correctly so: the official client
does not face for you either, so there is no divergence to fix. The
archived 12 runs failed because `#npctypespawn` drops the mob ON the
invoker, and at a separation of 0.04 units the bearing (255.75) is 95.75
units away from the character's stored heading (160) -- outside the
window from the first frame.

### Measured, three live phases (review/T350/)

| link | phase A | phase B | phase C (heading sweep) |
|---|---|---|---|
| (a) client sends `OP_AutoAttack`(on) | YES | YES | YES |
| (b) server sends `OP_Damage` back | 0 in 15 s | 0 in 15 s | **17**, and only in the two of eight sweep windows whose heading points at the mob |
| (c) `ZoneProcessDamage` -> `CombatAnimation` | not reached | not reached | YES, 17x, plus `You hit a fire beetle for 2 points of damage.` |
| (d) renderer receives the animation | not reached | not reached | published; renderer side not instrumented this run |

The sweep is the decisive measurement: auto-attack held ON, target held,
only the heading varied, `/face <x> <y> <z>` (the coordinate form, which
does not go through the entity resolver). Swings appear at server headings
135.02 and 180.04 -- the mob was due 180.04 -- and nowhere else.

### The heading pipeline is CORRECT

Checked because the project's standing note makes a 90-degree offset the
obvious first suspect. `character_data.heading = 160` -> client
`337.50deg` -> back out as `heading_scaled=640` -> eqemu
`EQ12toFloat(640) = 160`. Exact round trip, consistent across all three
sessions. The client<->server transform is `server_deg = (450 -
client_deg) mod 360`. Refuted; do not revive.

### The one real defect found, and fixed here

`ActionDispatcher::faceEntity` (`src/client/action/action_dispatcher.cpp:99`)
used the UNRANKED `EntityManager::findEntityByName(name)`, which ties on
the LOWEST SPAWN ID; `/target <name>` uses the T244 ranked overload, which
ties on DISTANCE. With two `a_fire_beetle`s in qeynos2 the two commands
disagreed: `/target` took spawn 277 at 3.5 units, `/face` took spawn 68 at
550 units and turned the player 131 degrees away from the target -- after
which the server silently refuses every swing. Confirmed numerically:
`/face` produced client heading 138.92deg, and the bearing from the player
to spawn 68 at that instant is 138.92deg.

This is the shape of defect that makes a scripted `/target; /face; /attack`
run look like "auto-attack is broken", so it is fixed here rather than
deferred: one line (use the ranked overload with the player position),
plus `ActionDispatcherTest.FaceEntity_PicksTheNearestMatch_NotTheLowestSpawnId`,
verified to FAIL on the unpatched source (`git apply -R`, rebuild, run)
and pass with the fix.

`moveToEntity`/`followEntity` (`action_dispatcher.cpp:154,167`) call the
same unranked overload, but only as an existence check before passing the
NAME to the handler, so the ranking is not observable there. Left alone,
recorded on the board.

### Deviations from the plan

- **Step 3 (the oracle comparison) was not run.** It is conditioned on
  "if the server never swings"; the server does swing on this same build
  and character as soon as the heading points at the mob, so there is no
  fable/oracle divergence left to explain. Running the T328 rig would have
  measured eqemu's rule twice.
- **The mob is `a_fire_beetle` (npctype 2024), not `a_training_dummy`
  (77002).** The dummy carries `special_abilities` 21 and 24
  (IMMUNE_MAGIC, IMMUNE_MELEE_NONMAGICAL), which would have made a
  zero-damage result ambiguous. 2024 is T245/T247's mob.
- **"Placed two units away" was done with `/move`, not a keystroke.**
  Phase A proved the chat-focus gate eats a bare `s` and submits it as
  chat (`Chat submit: '/ssss'`); `/move` is immune. Worth carrying into
  future live scripts.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 (after one exit-143; the protocol truncation scan ran clean: 267 binaries + 2 archives, bad=0) |
| `ctest --test-dir build -j6` | **3837/3838**; the only failure is the known ALSA exception `AudioManagerTest.PreloadCommonSounds` |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh fable` | 12 files checked, 12 ok, 0 warn, 0 fail |
| visual gate | not run: the change is a name-resolution tie-break with no pixel path |
