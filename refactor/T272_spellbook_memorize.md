# T272 — spellbook popup click cannot memorize (T264 F4)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live memorize. Class A.
Territory: the spellbook popup click handler (static_panels / the UI panel
that draws the spellbook — T264's Review names the LOG stub and
`MemorizeSpellIntent`), the intent arm in eq_bridge.cpp, CommandProcessor
`cmdSpellbook`'s stale "not implemented in UI" message, tests. Do NOT
touch eq_strings race names (T271 slot1), read-item (T273 slot3).

## Gap
The spellbook popup exists and lists spells; its click handler is a LOG
stub that names `MemorizeSpellIntent` as the missing piece (T264 kept the
intent for exactly this). `/mem` works, so the sender is proven — this is
the UI-gesture leg (T48 class), and it is the first one we can actually
close because the popup exists.

## Scope
1. Click on a spellbook entry (and drag-to-gem if the popup supports it —
   check what the panel already tracks) pushes `MemorizeSpellIntent{gem,
   spellId}`; the arm calls the same handler `/mem` uses (one body, T230
   pattern). Update `tests/test_reachability_sweep.cpp`'s expected set.
2. Fix `cmdSpellbook`'s stale message.
3. Tests: static-panel test (T54 added the first ones) that a click at a
   spell's rect pushes the intent with the right ids; handler test that
   the intent memorizes. Red/green.
4. Live: open the spellbook, click a spell into a gem, `/gems` shows it,
   server accepts (OP_MemorizeSpell on the wire), then cast it.

## Verification
Full ctest; visual gate (UI panel touched); Review; evidence review/T272/.
Commit: `T272: spellbook clicks memorize spells`.
