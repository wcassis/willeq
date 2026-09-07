# T443 — OP_GuildsList parsed into a lookup (B418); ESC on server-owned windows (B454); the scene breakdown counts the zone mesh (B453)

Class B, slot2, no server (T438 holds the lock; live proof OWED). Read
B418, B454 (renumbered from a duplicate B336 at this merge), B453;
T429 Leg B's §Review (guild measured against the oracle: where the
official client prints a guild name — /who line, the name tag; quote
the eqstr rows), `WorldProcessGuildsList` (a `LOG_DEBUG` that parses
nothing), eqemu's `OP_GuildsList` encoder for the Titanium patch
(`/mnt/projects/akk-stack/code/common/patches/titanium.cpp`, and
`common/guild_base.h` — cite absolutely) for the wire shape, T415/T436's
`TradeskillCloseIntent` and the "UI never closes itself" rule (T290),
`DiagnosticsComponent`'s scene breakdown, T440/T442 §Reviews (sky is
the one no-BSP zone; `zoneMeshNode`).

1. B418: parse `OP_GuildsList` into a guild-id → name lookup owned by
   the login/world component (the same shape as the eqstr/StringDatabase
   lookups); expose it where the oracle prints a guild: the /who line
   (eqstr row quoted) and the overhead name tag `<Guild Name>` line if
   fable already draws a second line — read the oracle's name-tag
   format from T3xx's captures before adding anything; if fable draws no
   guild line today, add ONLY the /who use and board the tag with the
   capture reference. Test pre-fix failing: a hand-built Titanium
   GuildsList packet resolves id → name; an unknown id resolves to the
   oracle's own fallback (measure: what does the official client print
   for a guildless player? nothing — quote the /who row without the
   guild fragment).
2. B454: ESC on the tradeskill container pushes `TradeskillCloseIntent`
   and leaves the panel until `TradeskillContainerClosed`; same shape for
   the merchant (T436: closes only on `VendorWindowClosed` — ESC pushes
   `VendorToggleIntent`? read what the oracle does on ESC with a vendor
   open: T342's captures) and the bank. Tests pre-fix failing per window.
3. B453: the breakdown subtracts `ctx_.zoneMeshNode`; test with a
   hand-built scene (an EDT_NULL device) showing `unaccounted=0`.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move), file sizes, DirectSenderCensus/intent-table pins
learn new intents in place. Strike B418/B454/B453 (or say what remains
for the lock unit). New items B455+ (T441 in slot1 allocates from B453
too — say which you used; collisions are renumbered at merge).

STOP: an image golden moves.
