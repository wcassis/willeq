# T225 — RendererEventReceiver read-and-reset flags: the 17 residual TSAN warnings

Slot: slot2. x86. NO eqemu needed for the fix; the TSAN tour needs a zone-in —
take the eqemu lock per protocol only for that run and release it. Class A
(T114 inventory #10; T220 measured the residual: `review/T220/tsan_after_*.txt`).
Do NOT touch include/client/bridge/*, src/client/bridge/*, game_state*,
eq_*.cpp, spell_manager.cpp, combat.cpp (T221), eq_player_info.cpp or
wld_loader.cpp (T224).

## Defect
`RendererEventReceiver` (include/client/graphics/... — grep) holds ~15 plain
`bool`/int fields that the RENDER thread sets from Irrlicht input events and
the GAME thread reads-and-resets (key/mouse flags). Only one is `std::atomic`.
TSAN reports 17 distinct races on them under T220's tours.

## Scope
1. Read T114's write-up (refactor/T114_worker_render_race_audit.md, inventory
   #10) and T220's Review for the exact fields and stacks.
2. Fix with the codebase's discipline: prefer routing the flags through the
   existing intent queue (GameStateBridge intents, drained on the consumer
   thread) if the consumers are already intent-driven; otherwise make each
   read-and-reset flag a `std::atomic<bool>` with `exchange(false)` at the
   reader (relaxed is fine for independent flags; document why). Do NOT add
   a mutex. Keep the change mechanical; no behaviour change.
3. Unit test where one exists for the receiver; otherwise none — the
   acceptance is TSAN.
4. TSAN: build a TSAN tree exactly as T220 did (its README documents the
   command), run T220's DENSE tour script from review/T220/ unchanged.
   Expect 0 warnings; if any remain, list them with stacks — they are new
   inventory, not yours to fix.
5. Normal build: full ctest, visual gate, ARM build.

## Verification
Review in this file; evidence review/T225/ (tsan_before = T220's file,
tsan_after yours). Commit: `T225: RendererEventReceiver flags are atomic -- zero TSAN warnings on the tour`.
