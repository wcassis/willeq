# T415 — the tradeskill window (B321) and the combat abilities window (B320)

Class B (two panels the official client has and fable lacks; their
payload fields are already published), slot1, no server. Read B321/B320,
T413's §Review (the XML-placement method: `spellbookMemorizeGaugeRect()`),
T393/T409 (`inventory_layout.h`'s scaling from the oracle window size),
the static-panel seam (T54–T56), and the two XMLs:
`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeskillWnd.xml` and
`EQUI_CombatAbilityWnd.xml`.

## Leg A — B321 tradeskill window

On `TradeskillContainerOpenedEvent` draw the window: title from
`containerName`, `slotCount` slots at the XML's slot positions, the
Combine button rect, a Close; `TradeskillContainerClosedEvent` hides it.
Placement scaled from the XML's screen size the way `inventory_layout.h`
does. Slot contents come from the existing container state (T342 wired
combines — read what the container holds and where). Tests pre-fix
failing: open event → the window's rects; the slot rects match the XML
scaled; close hides. Strike B321.

## Leg B — B320 combat abilities window

`EQUI_CombatAbilityWnd.xml`: the ability list and
`CAW_CombatEffectTimeRemainingGauge` (EQType 26). Draw the gauge from
`SkillActivationFeedbackData::cooldownMs` (the field nothing consumed)
and the ability rows from the skills the character has (the skill list
already drives `/skills`, T363). A toggle key: the oracle's default is in
Options > Keys (T377's screenshots under `review/T377/oracle/` — read the
UI page; if the combat abilities key is not on it, `Alt+C` is the Titanium
default per the same table — quote your source; if unquotable, bind
nothing and board the key). Tests pre-fix failing. Strike B320.

## Proof

Build (background + exit file), `ctest -j6`, pre-fix/post-fix, visual
gate standalone + ARM (no golden should move — the gate opens no
windows), file sizes. Live screenshot pairs OWED to the next lock unit.

STOP: the container state for Leg A has no slot contents reachable from
the renderer (report the shape).
