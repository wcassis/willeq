# T277 — click off a buff (T264 F6)

Slot: slot1. x86. OWNS eqemu (noclobber) for a live click-off. Class A.
Territory: the buff-removal intent/handler T264 KEPT (its Review names
them; delist in `tests/test_reachability_sweep.cpp` when wired), the buff
bar panel's click path (static_panels / static_ui_input — T272 added the
spellbook click, follow that shape), the OP_BuffRemoveRequest sender,
CommandProcessor for a scriptable form, tests. Do NOT touch ui_renderer
(T275 slot2), spell scribe (T276 slot3).

## Gap
The buff bar draws buffs; nothing can remove one. eqemu:
`Handle_OP_BuffRemoveRequest` (zone/client_packet.cpp) with
`BuffRemoveRequest_Struct {slot_id, entity_id}` — verify, including which
buffs the server refuses to drop (song/detrimental/lore).

## Scope
1. Command first (T156 rule — a click is not scriptable): `/removebuff
   <slot|name>` resolving through the buff model the bar already reads.
2. Click: buff-bar rect click → the same handler body (T230/T239 one-body
   rule, T272's click plumbing). Note if the bar has no hit-test yet.
3. Refuse locally only what the client can know (empty slot, out of range);
   let the server refuse the rest.
4. Tests: request bytes for a given slot; the buff leaves the model on the
   server's confirmation, NOT optimistically. Red/green.
5. Live: Testcaster self-buffs (Minor Shielding from T272's staging),
   `/removebuff 0` → server drops it, buff bar empties, observer sees the
   appearance change if one is sent. Restore rows.

## Verification
Full ctest; visual gate if the panel changed; Review; evidence
review/T277/. Commit: `T277: buffs can be clicked off`.
