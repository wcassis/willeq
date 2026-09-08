# T449 — the owed live proofs, in one lock session

Class A/B (proof), eqemu lock (atomic; must be ABSENT), the slot that
frees after T431. Read each item's §Review for the exact check it owes
and its capture recipe; two-client rule (assert on what an OBSERVING
second instance sees); never sample a transient with stills.

Owed, in order of cost:
1. T432 per-character UI ini: toggle, `/camp`, relog on a second
   character — the file is per character and per server.
2. T430 icons + T436 merchant/bank/cursor + T439 zone-in snapshot: a
   real vendor (T342 rig), bank, item on cursor across a zone-in.
3. T434 Options: Alt+O, sliders move the volumes audibly (record the
   ALSA capture), clip plane moves the fog; T439 `/music` then a slider.
4. T443: `/who` shows the guild (create a guild with a GM); ESC on the
   tradeskill container; `/dumpscene` in sky.
5. B461 lift ride: WALK onto the Kelethin platform (no `#goto` onto it),
   trigger via the fixture route, `/loc` per frame vs T438's trace.
6. B356 caster's-own-landing re-capture (four spells, the duel rig); B227
   projectiles (duel rig, set distance).
7. B305 the three scenario-lane runs.
8. B460 ESC with a vendor open on the official client (what closes).

Each item: strike or reopen with the evidence path. Server values
restored with the world DOWN; `/camp` cleanly; lock released. Media
under `review/T449/` with a README per item.

STOP: a proof fails — reopen the item with the capture; do not fix in
this unit unless the fix is one line and pinned.

---

## Review

Branch `task/T449-live-proofs`, base **`a714738c`** (verified at checkout).
slot3, `CMAKE_BUILD_TYPE=RelWithDebInfo`, its own
`third_party/irrlicht-gles2-x86` (both checked before anything else). The
eqemu lock was ABSENT on arrival and taken atomically. **No source file was
changed by this unit** — it is a proof unit and every finding is a board item.
Full evidence, every frame and every log: `review/T449/README.md`.

### The headline

**A live SEGFAULT was found that nobody had named: fable crashes on a zone
change whenever the inventory popup is open**, in
`CharacterModelView::renderToTexture()` → `EQAnimatedMeshSceneNode::render()`
→ `COpenGLES2Driver::bindAlphaCompanionUnit()` →
`COGLES2Texture::getOpenGLTextureName()` on a texture the zone unload
destroyed. Reproduced twice, once under gdb for the backtrace, and A/B
controlled: the identical zone change with `[InventoryWindow] Show=0` in the
per-character ini completes two zone loads with no fault. **T432's own
persistence widens it** — a character who ever left the inventory open is
logged back in with the paperdoll already rendering. Board **B482**.

### The eight items

| # | item | verdict | evidence |
|---|---|---|---|
| 1 | T432 per-character UI ini | **PROVEN** | `review/T449/README.md` §2, `fable/T449_t432_*`, `data/UI_*.ini` |
| 2 | T430 icons + T436 merchant/bank/cursor + T439 zone-in snapshot | **PROVEN**, one gap (**B489**) | §3, `fable/T449_t430_*`, `fable/T449_t436_*`, `fable/T449_t439_01_*` |
| 3 | T434 Options + T439 `/music` then a slider | **PROVEN**, not audibly (**null audio device**) | §4, `fable/T449_t434_*`, `fable/T449_t439_02_*` |
| 4 | T443 `/who` guild, ESC on the container, `/dumpscene` in sky | **REOPENED** | §6 — `/dumpscene` blocked by **B488**; the other two not reached |
| 5 | B461 the lift ride | **REOPENED** | not reached — no capture, so the entry is untouched |
| 6 | B356 caster's-own-landing, B227 projectiles | **REOPENED** | not reached |
| 7 | B305 the three scenario-lane runs | **PARTIAL** — `zone_in` PROVEN, restore PROVEN, `cast_lands` PROVEN with a corrected spell, `two_client_survive` REOPENED | §5, `scenario/`, `logs/cast_lands_direct*.log` |
| 8 | B460 ESC with a vendor open on the OFFICIAL client | **REOPENED** | not reached |

**Deviation, flagged rather than absorbed: four of the eight were not
reached.** The plan called for one lock session over eight items; items 1–3
and 7 consumed it. Nothing was claimed that was not captured — the four
reopened entries carry no new evidence and their board text is left as it was,
except B305 which is annotated with what its three runs actually returned.

### What items 1–3 proved, in one line each

* **T432** — Testchar toggled two windows, the ini appeared WHILE THE CLIENT
  WAS RUNNING (the per-frame compare, not just `/camp`), `/camp`, then
  **Testmelee on the same account and server** came up with its OWN defaults,
  wrote no file until it changed something, and then wrote a SECOND file while
  Testchar's stayed byte-identical.
* **T430/T436/T439** — a real vendor (`Mellisa_Purgor`) and a real banker
  (`#npctypespawn 772`): icon column, row select with `MW_SelectedItem`, a Buy
  that returns `You purchased Spell: Smite for 5g 1s 6c`, the icon in the
  inventory popup and in T421's display window, the bank's eight slots + four
  coin rows + disabled Change button, `/bank deposit 10 pp` moving both coin
  readouts, the cursor attachment's 40x40 icon under the pointer, and that
  same cursor item still drawn after a full zone change.
* **T434/T439** — `Alt+O` opens the window; a slider click logs
  `Options: music volume 90 %` → `Music volume: 90%`; **`/musicvolume 25`
  typed in chat moves the SLIDER**, which is exactly the B432 round trip; the
  Display page's Far Clip Plane goes `Clip 300` → `far clip plane 1000`; and
  all three persist as `[Options] Music=5 SoundVolume=20 ClipPlane=19`.

### B305 — three runs, three new defects

`zone_in` exits **0** and `restore_snapshot()` reported
`snapshot restored byte-identically` on all **four** runs that reached it, so
B305(b) is settled. B305(a) is settled the other way from its framing:
`#memspell 46 0` is ACCEPTED, but spell 46 has `spells_new.classes12 = 255`
and the config's Testcaster is a Wizard, so every cast fizzles — **B484**;
with spell 288 the scenario exits 0 on the first gem cast.
`two_client_survive` exits **1** because its peer runs `zone_in` and shuts
down 0.15 s after zoning in — **B485**. Both `cast_lands` runs through
`scenario.sh` stalled at character select because `up.sh`'s non-launcher path
starts a SECOND world on top of the one the akk-stack container starts by
itself — **B483**, measured directly (45 s after a bare `docker compose up -d`
with no `up.sh`, the spire launcher and its world are already running). And a
`cast_lands` re-run inside the buff's duration can never pass — **B486**.

### New board items

**B482** (A, the zone-change segfault), **B483** (P, the duplicate world),
**B484** (A, `cast_lands`'s uncastable default spell), **B485** (B, the peer
that cannot hold), **B486** (B, the buff-duration re-run), **B487** (B, the
chat input that stays open after a submit), **B488** (A, the Plane of Sky
zone-in that never completes — this is what blocks B453), **B489** (B, no UI
gesture picks an item up to the cursor).

Struck: **B275**'s owed live proof, **B429**'s (T436's merchant/bank/cursor
windows in front of a real vendor and banker), **B432**'s, and **B444**'s
cursor half. **B305** annotated, not struck.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit **0** (background + exit file; ~526 s under a load average of 55 from slot2) |
| `ctest --test-dir build -j6 --output-on-failure` | **4,672 of 4,672**, 0 failed, `logs/ctest.log` |
| `scripts/build-gles2-x86.sh` | exit **0** |
| visual gate / ARM / `check_file_sizes.sh` | **not run — no source file changed**; the tree is `a714738c` plus two documentation edits |
| goldens | untouched |

### Server state

Every value changed is listed in `review/T449/README.md` §9 and was restored
with the **world DOWN** (`down.sh`, then only mariadb up). `character_data`,
`character_currency`, `character_corpses` and Testcaster's 100
`character_skills` rows all `diff` **IDENTICAL** against their before-dumps;
the four `inventory` rows this unit created for Testchar were deleted, leaving
the character with the zero rows it had. No quest fixture was installed (B461
was not reached). `down.sh` exit 0, `docker ps -a` carries no akk-stack
container, `/mnt/projects/fable/.eqemu-lock` is deleted.
