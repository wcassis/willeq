# T256 — /who anon / linkdead / guild lines verified live (T224 owed)

Slot: slot1. x86. OWNS eqemu (noclobber) — two clients. Class B (verify;
fix only what the live lines refute). Territory: src/client/who_response.*
+ its test, `ZoneProcessWhoAllResponse` in eq_player_info.cpp (formatting
only), tests. Do NOT touch inventory (T255 slot3), combat/eq_movement
(T254 slot2).

## Owed (T224 Review)
T224 verified the 47-byte WhoAllPlayerPart layout and the plain-player
line live, but anon (format 5024 / 5022 for GM viewers), linkdead
(`unknown80[1]=12313`), guild (rankstring/guild cstr) and roleplay lines
were exercised by unit tests only. The `%n` argument order for those
formats was inferred from template shapes.

## Scope
1. Stage: Oraclerogue `/anon` on; a guild for Testchar via DB (guilds +
   guild_members rows, documented + restored) or `#guildcreate` if the GM
   command exists; linkdead by SIGKILL-ing the observer client mid-session
   (eqemu marks LD for ~90 s).
2. Testchar (GM, status 200) `/who all` after each stage; a NON-GM viewer
   too if a third account exists (willeq_rogue is status 250 — check; if
   no non-GM account, note it). Compare each printed line to the official
   template with the %n slots; fix `formatWhoAllEntry`'s args switch where
   the live line proves the order wrong (red/green on the constructed
   packet).
3. Restore everything (anon off, guild rows, positions).

## Verification
Full ctest if code changed; Review with the live lines verbatim; evidence
review/T256/. Commit: `T256: /who anon, linkdead and guild lines verified live` (+ fixes if any).
