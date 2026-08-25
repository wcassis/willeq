# T223 — three small command-layer defects: headless outcome, /newskills registry, phantom gequip7

Slot: slot3. x86. NO eqemu needed. Class A backlog items. Do NOT touch
include/client/bridge/*, src/client/bridge/*, eq_bridge.cpp (T220 territory)
or command_processor.cpp's /reply (T222).

1. T139/T142: headless (`--no-graphics`) build reports `outcome=queued` for
   36 renderer-only command names instead of a "requires graphics" failure —
   `Application::dispatchToActionOrRenderer` lacks the `EQT_HAS_GRAPHICS`
   guard at the fallback site. Add the guard; test in the headless test
   binary that a renderer-only command yields a failure outcome with a clear
   message. Also restore the lost console echo for the renderer `chat()`
   path if it is the same site (T142) — otherwise note it.
2. T143: `/newskills` (the real skills-window toggle) is registered in NO
   registry and registry 3 (renderer `processSlashCommand` if-chain) is
   invisible to `/help`. Register `/newskills` properly so `/help newskills`
   finds it; add a catalog hook so `/help` lists renderer-only commands with
   a "(graphics)" tag — derive from the if-chain, don't hand-duplicate the
   list if a table can be extracted.
3. T198: `config/eqe1_archives.json:8` lists `gequip7`, for which no
   `gequip7.s3d` exists anywhere under /mnt/projects/EverQuestP1999. Remove
   the entry; confirm no test or tool pins it.

## Verification
Red/green per item; full ctest; visual gate not needed unless graphics code
changed; Review in this file; evidence review/T223/. Commit:
`T223: headless refuses renderer commands; /newskills registered; phantom gequip7 dropped`.
