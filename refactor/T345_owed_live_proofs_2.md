# T345 — Owed live proofs: autorun and /stop (T343), Ultravision zone light (T339)

Measurement only; slot2; eqemu lock (free after T342). Reuse T336's driver
(`review/T336/t336_live.sh`) and T338's config (`maps_path` =
`/mnt/projects/maps/base`). Binary rebuilt from the slot at the current
tip (must contain T343 and T339 — `git log`).

1. **Autorun (B002):** qeynos2, Testchar, daylight, `--eqe1-path`. Press
   Num Lock; capture two frames one second apart — the position (HUD
   `/loc` or the client log's position line) must change with no key
   held. Press W once — autorun stops (two more frames, position steady).
   Then `/moveto` a nearby point and `/stop` mid-way — the position stays
   put on the next tick (T343 found `/stop` used to resume). Log lines +
   PNGs.
2. **Ultravision (B055):** `/say #cast` or `/cast` Ultravision on self
   (the GM kit: `#scribespells`/`#memspell`, see the `gm-kit-out-a-character`
   notes), night time (`/say #set time 22 0`); capture before and after
   the buff lands — the zone light change must be visible; wait for or
   `/say #buff`-remove the fade and capture again. If the spell cannot be
   cast by this character in 10 minutes, record why and leave the row
   owed.
3. `review/T345/README.md`; strike the "owed" notes on B002 and B055
   (and B032's live half if T343 recorded one); restore the character as
   found; `down.sh`; delete the lock. Docs-only commit.

STOP: lock held; Num Lock does not toggle (report the log lines, do not
touch code).

## Review

Branch `task/T345-owed-live-proofs-2`, cut from `fable` at `5b8bb271` (carries
`3e8bd7a3` T343 and `77825751` T339). Measurement only; no source changed.
Media, logs and the driver: `review/T345/` — `README.md` there is the
frame-by-frame account and this section is the summary.

Binary rebuilt from the slot: `scripts/build-gles2-x86.sh`. First invocation hit
the 600 s tool ceiling (exit 143); protocol recovery applied (10 zero-byte
artifacts under `build-gles2` deleted), re-run **exit 0**.

eqemu: lock taken atomically (`T345 2026-09-04`), `up.sh qeynos2` exit 0,
`down.sh` exit 0, lock deleted. Five graphical sessions.

### 1. Autorun (B002) — PROVEN

One Num Lock tap, nothing held. The client's own log:

    [17:55:50.641] [INPUT] Key pressed: NumLock ... action=ToggleAutorun
    [17:55:50.641] [INPUT] [INPUT-TRACE] OnEvent: action ToggleAutorun -> actionQ=1, bridgeQ=0, zoomDelta=0
    [17:55:50.641] [GRAPHICS] Autorun ON

`actionQ=1, bridgeQ=0` is T343's re-route visible on the wire. `MOVEMENT:TRACE`
emits a position line only when the position or heading changed, so the count is
the measurement:

| grab | lines so far | last position line |
| --- | --- | --- |
| before Num Lock | **0** | — |
| +1 s | 164 | `(306.08, 222.98, 5.12) heading=480` |
| +7 s | 347 | `(171.93, 403.66, 3.12) heading=480` |
| +3 s after one W tap | **351** | `(169.19, 403.66, 3.12)` @ 17:56:05.006 |
| +6 s later | **351** | the same line |

Position changed with no key held; a single W tap stopped the stream dead and it
stayed dead for 16 s. Five PNGs.

### 2. `/move` then `/stop` — PROVEN, with a routing correction

`/moveto` takes an ENTITY NAME in this client; `/move <x> <y> <z>` is the
coordinate form, so `/move` was used (deviation from the plan's wording, stated).
Destination derived from the client's own `[ZONE-IN] setPlayerPosition` line, so
it is already in `/move`'s frame.

    18:11:33.954  Chat submit: '/move 728.00 226.00 3.12'
    18:11:40.943  Following path: waypoint 32/43, dist to waypoint: 11.69   <- LAST [MOVEMENT] line
    18:11:40.955  Chat submit: '/stop'
    (no [MOVEMENT] line for the remaining 58 s)

`/stop` landed 12 ms after the last movement line, at waypoint 32 of 43 — 11
short of the destination — and nothing resumed. The `stop_settled` frame carries
the visual proof in the chat panel: four `/loc` lines, one before the move and
three spread over 44 s after the stop, the last three identical at
`642.4, 141.8, 5.1` against a destination of `728, 226`.

**Correction to T343's Review, flagged not absorbed.** T343 calls
`EqActionHandler::stopAllMovement()` "the `/stop` command". At this tip `/stop`
is an ALIAS of `stopfollow` (`command_processor_registration.cpp:556`) and runs
`cmdStopFollow` -> `stopFollow()` -> `MovementComponent::StopFollow()` ->
`StopMovement()`. `stopAllMovement()` is reached only from
`console_input_handler.cpp:412`; `--script` and piped stdin both go through the
same registry dispatch as chat and hit the alias. Both routes end at
`StopMovement()`, so the user-visible behaviour captured above is the behaviour
T343 describes — but the function T343 edited is not the one this capture
exercises. Not boarded as a defect: it is a Review wording error, corrected here
and in the B032 row.

Three runs were lost first: `/move` travels ~50 units/second and `/stop`
arrived after the walk had already finished. The driver gained `chatline_fast`
and a >400-unit destination.

### 3. Ultravision (B055) — NOT PROVEN. Row stays owed, with a measured cause.

Six frames at forced 22:00 night — before the cast, twice after, after a
re-cast, after `#zone`, after `#nukebuffs`. **The zone light does not change in
any of them, and no `Vision buff changed` or `Vision upgraded to:` line is ever
logged.**

Two rig facts first, both worth keeping: `#castspell 46` needs no class, level,
gem or spellbook (`AccountStatus::Guide`, self-target when untargeted), so
`#scribespells`/`#memspell` were unnecessary; but **Testchar's account is flagged
invulnerable** and an invulnerable client refuses its own `#castspell`
("has failed to land on yourself because you are invulnerable"). `/say #set
invulnerable off` is the fix, and the flag was restored.

With that done the spell lands — both `SpellAction` packets, `effect_flag=0`
then `4`. It still publishes nothing, and the cause is upstream of T339:

* `PlayerComponent::ZoneProcessBuff` (`player_buffs.cpp:257`) calls
  `BuffManager::setPlayerBuff()` — the only production caller that fires T339's
  apply callback — **only** on an `OP_Buff` with `effect_type == 2 && fade != 1`.
* Every `[SPELL] Buff:` line in the whole session carries `fade=1`. Server side,
  the only sender of `bufffade = 0` is `Client::SendBuffDurationPacket`, whose
  `BuffProcess` caller is gated on `zone->BuffTimersSuspended()`
  (`spell_effects.cpp:3866`). On a normal zone eqemu **never** sends a Titanium
  client an "applied" buff packet; `MakeBuffFadePacket` is the only `OP_Buff` it
  sends.
* The one route that does deliver a landed buff — the PlayerProfile at zone-in,
  `zone_entry.cpp:684` -> `BuffManager::setPlayerBuffs()` (plural) — fills
  `m_player_buffs` directly (`buff_manager.cpp:93`) and **does not fire the apply
  callback**. So a re-log with the buff up publishes nothing either.

T339's registration is correct and its tests are honest; nothing in a live
session can reach it at this tip. The missing piece is a PRODUCER, not the hook.
Boarded on B055 as a new open sub-item rather than struck.

**Second lead, recorded not concluded.** `PlayerCameraComponent::setPlayerRace()`
logs `Player race {} -> base vision: {}` at INFO on every call, and that line
appears **nowhere** in any of this session's logs, although the player entity is
created (`T105 ENTITY spawn=287 race=1 ... isPlayer=true`). Its only call site is
`IrrlichtRenderer::createEntity()` (`renderer/renderer_entities.cpp:29`), while
the live zone-load path builds entities through
`zone_load/renderer_zone_load_entities.cpp`. If that holds, `baseVision_` is
never set from the player's race in a real session. Not traced to a conclusion
in this unit.

### The rig finding that pays for itself: press Escape

T329 and T336 both recorded "no keystroke reaches the client after the chat
sequence" and built their drivers around sending every toggle BEFORE `/say`. The
cause is `RendererEventReceiver::OnEvent`'s `chatInputFocused_` gate
(`renderer_event_receiver.cpp:151`), which swallows every non-function key while
the chat line has focus. **Escape releases it.** All five sessions here set the
world time by chat FIRST, pressed Escape, then drove keys normally; the driver's
warm-up loop measures rather than assumes it and reported
`input reaches the client (attempt 2)` every time. This is why the autorun
session could be captured in forced daylight at all.

### Server state

`Testchar` found at `zone_id=2, x=-74, y=428.04, z=3.75, h=160` and restored to
exactly that. `account willeq_test` found at `status=200, invulnerable=1`;
`invulnerable` toggled off for the cast, restored in-game, row verified back at
`1`. `Testcaster` and `Testmelee` never written.

### Verification

Measurement-only unit: no build/ctest/visual-gate bar applies beyond the client
rebuild (`scripts/build-gles2-x86.sh`, exit 0) that the sessions ran on. Doc
changes only: this Review and `refactor/BOARD.md`.

### STOPs

Neither STOP triggered. The lock was free and taken; Num Lock toggles correctly.
Nothing in the tree was touched over the Ultravision result — it is reported, not
fixed.
