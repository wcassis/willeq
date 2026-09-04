# T358 — Owed live proofs 3: Ultravision landing (T348), Dark Elf zone-in (B056), self-death (T346/T349)

Measurement only; slot2; eqemu lock (take it atomically per the protocol —
it must be ABSENT; three units this week found it absent while "held", so
if it exists, read its contents and STOP with them). Reuse T336's driver
(`/mnt/projects/fable/review/T336/t336_live.sh`) and T338's config
(`maps_path` = `/mnt/projects/maps/base`, `--eqe1-path`). Binary rebuilt
from the slot at the current tip (must contain T348, T346, T349 — check
`git log`). Read `refactor/T345_owed_live_proofs_2.md` §Review first: it
records the rig facts (`#castspell 46` needs no class/gem; the account's
`invulnerable` flag must be OFF or the cast refuses; Escape releases the
chat-focus gate; eqemu never sends an "applied" OP_Buff, which is why T348
moved the landing to OP_Action effect_flag 4).

1. **Ultravision landing (B055, T348):** night (`/say #set time 22 0`),
   `/say #set invulnerable off`, `#castspell 46` on self. Capture before,
   after landing, and after `#nukebuffs`. The zone light must change and
   the log must show T348's landing line and the fade line. Record video
   for the transition (never stills for a transient — step the video).
2. **Dark Elf zone-in (B056, T348):** a Dark Elf character (provisioning is
   automated — see the memory note on eq-oracle provisioning / `#race 6`
   on an existing character is acceptable if it takes effect on zone-in).
   Zone in at night: the racial floor must give Ultravision with no buff.
   Capture + log line. Then `#race 1` (Human) and zone again: Normal.
3. **Self-death (T346/T349):** die once (`/say #kill` on self, or let a
   `#npctypespawn`ed mob kill you — it lands on the invoker). Assert on the
   packet log: Lying appearance sent, NO ClientUpdate while dead, OP_Save
   body = death position (192 bytes), then bind re-entry through a FULL
   zone-in with the world visible afterwards. Video + log excerpt.
4. `/mnt/projects/fable/review/T358/README.md` (one section per proof,
   PASS/FAIL stated first, the exact commands, the log lines). Strike the
   "owed" notes on B055/B056 rows and in T346/T348/T349 plans (in-repo text
   edits: docs-only commit). Restore every flag/race/time you changed;
   `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (then it is a defect: record the evidence
in the README, add a board item B066+, do the remaining proofs, do NOT fix
code in this unit).
