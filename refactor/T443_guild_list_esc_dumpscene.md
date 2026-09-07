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

---

## Review

Branch `task/T443-guild-esc-dumpscene`, base `6d691ab5`. Slot 2,
RelWithDebInfo. **No server: T438 held the lock, so nothing here opened a
connection to 127.0.0.1:5998 and LIVE PROOF IS OWED for all three items.**

Board: **B418**, **B454**, **B453** struck. New items **B459** and **B460**
(T441 in slot1 may also allocate from B453+; collisions renumber at merge).

### 1. B418 -- OP_GuildsList, parsed

`LoginWorldComponent::WorldProcessGuildsList()` was one
`LOG_DEBUG(MOD_WORLD, "Received guilds list")` that read none of the packet.
It now fills `eqt::guild::GuildNameTable`
(`include/client/guild/guild_name_table.h`, `src/client/guild/guild_name_table.cpp`),
owned by the component and reachable as `LoginWorldComponent::GuildNames()`.

**The wire shape, quoted.**

| fact | source |
|---|---|
| `struct GuildsListEntry_Struct { char name[64]; };` | `/mnt/projects/akk-stack/code/common/patches/titanium_structs.h:1412` |
| `struct GuildsList_Struct { uint8 head[64]; GuildsListEntry_Struct Guilds[MAX_NUMBER_GUILDS]; };`, `MAX_NUMBER_GUILDS = 1500` | `/mnt/projects/akk-stack/code/common/patches/titanium_structs.h:1416` |
| `strn0cpy(out->Guilds[g.guild_id].name, g.guild_name.c_str(), sizeof(...))` | `/mnt/projects/akk-stack/code/common/patches/titanium.cpp:819` (`ENCODE(OP_GuildsList)`) |
| `if (g.guild_id < Titanium::constants::MAX_GUILD_ID)`, `MAX_GUILD_ID = 1500` | `/mnt/projects/akk-stack/code/common/patches/titanium_limits.h:287` |

Two facts a reader would get wrong, and both are asserted with a negative:

* **The array index IS the guild id.** The encoder writes
  `out->Guilds[g.guild_id]`, sends no id field and appends nothing, so a
  "walk the entries and number them" reading misnames every guild on a server
  whose ids are not dense from zero.
  `GuildNameTable.TheArrayIndexIsTheGuildIdNotAnOrdinal` asserts ids 7 and 413
  resolve AND that 0 and 1 do not.
* **The packet is fixed size and mostly empty**: always 64 + 1500*64 =
  **96,064** bytes, pinned by
  `GuildNameTable.TheFixedStructIsNinetySixThousandAndSixtyFourBytes`. An
  unwritten slot is 64 zero bytes -- an ABSENT guild, not a guild named `""`.
  A guild whose id is >= 1500 is dropped by the server and is not addressable.

`find()` returns `const std::string*`; a miss is `nullptr` and the caller draws
NOTHING. There is no fallback string, because the oracle has none: eqemu only
fills the /who guild fragment when `GuildExists`, so a guildless player's line
is row 5025 with an empty guild argument and no guild text at all.

**Deviation from the plan, and the reason: the lookup has NO display consumer,
by measurement.** The plan asked for the /who use plus the name tag. Both were
eliminated:

* **/who needs no lookup.** eqemu sends the guild as a WIRE STRING already
  formatted `"<%s>"` -- `/mnt/projects/akk-stack/code/world/clientlist.cpp:824`
  (`sprintf(guildbuffer,"<%s>", guild_mgr.GetGuildName(cle->GuildID()))`) for
  the world /who, and `/mnt/projects/akk-stack/code/zone/entity.cpp:4970`
  (`GuildName += guild_mgr.GetGuildName(ClientEntry->GuildID())`) for the zone
  /who. `who_response.h` already parses it into `WhoAllEntry::guild` and
  `formatWhoAllEntry()` already renders it as row 5025's `%6`. The /who row
  quoted, with and without the fragment, is the SAME row -- eqstr **5025**
  `%T1[%2 %3] %4 (%5) %6 %7 %8 %9` -- with `%6` empty when the player has no
  guild; there is no separate guildless row.
* **The target window has no guild element** (T429 Leg B, `EQUI_TargetWindow.xml`).
* **`OP_GuildStatus`'s reply is composed server-side** with the name already
  substituted (`/mnt/projects/akk-stack/code/zone/client_packet.cpp:8728`).
* **The overhead name tag is the oracle's one remaining surface and has NO
  measured format.** fable draws it as a single `addTextSceneNode` line
  (`src/client/graphics/entity_mesh_build.cpp`, both creation sites) with
  Irrlicht's built-in font, which has no newline handling -- a second line
  needs a second node. The whole `review/` tree and every `refactor/*.md` were
  searched: nothing records the official client's tag content, the tag has no
  `EQUI_*.xml` definition (the 3D renderer draws it, not the SIDL window
  system), and `eqstr_us.txt` has no bare `<%1>` row -- only **1083**
  `Guild <%1> is now at Peace with Guild <%2>` and **1084** (war), which are
  chat sentences. Adding a tag line would invent a format; T363 forbids it.
  Boarded as **B459** (class C: it needs a capture with a GUILDED character,
  and `review/T132/README.md` / `review/T100/README.md:43` record that the
  test characters have none, so a guild must be GM-created first).

So B418 closes on its own stated close condition (the parse), and the display
it "unblocks" is B459. `Entity::guildId` stays write-only and keeps its
`check_write_only_fields.py` exemption -- **its REASON text was rewritten**,
because the old one said fable "has no guild-id-to-name source at all", which
this unit made false.

**A trap worth recording.** `check_write_only_fields.py` matches member names
tree-wide **by identifier alone**. Naming the lookup's parameter `guildId`
made `Entity::guildId` look READ, and `WriteOnlyFields` failed with
`STALE exemption (no longer write-only): Entity::guildId`. The checker's own
docstring says a collision "can only HIDE a write-only field, never invent
one" -- this is that hiding, caught only because the ratchet also fails on a
stale exemption. The parameter is `id` now, with a comment saying why.

### 2. B454 -- ESC on a server-owned window

`StaticUIInput::processKey()` takes a `TradeskillPanelState&` and asks it
FIRST: the container is the last centre popup drawn
(`renderer_frame_render.cpp` draws it after the loot popup), so it is the
topmost. It pushes `TradeskillCloseIntent{tradeskill.objectId}` and does NOT
touch `isOpen`; only `applyTradeskillContainerClosed()` clears it.

The entry said `processKey()` "has no established pattern for a dismissal that
goes through the bridge". It does: the loot arm, added by T290. The container
uses the same one, and the Close button's existing intent -- **ESC is a second
producer for one existing intent, not a new packet.**

| window | ESC pushes | panel closes on | where |
|---|---|---|---|
| tradeskill container | `TradeskillCloseIntent` | `TradeskillContainerClosed` | `processKey()` -- **T443** |
| loot | `CloseLootIntent` | `LootWindowClosed` | `processKey()` (T290) |
| bank | `CloseBankIntent` | `BankWindowClosed` | `FrameComponent` escape handler (T436) |
| merchant | `VendorToggleIntent` | `VendorWindowClosed` | `FrameComponent` escape handler (T436) |

**The plan's merchant and bank half was already done by T436** and is pinned
here as an invariant instead (`B454Census.EscapeOnTheBankAndMerchantStillSendsTheirCloseIntents`,
which also asserts neither arm hides its panel locally). The bank and merchant
arms stay one level up because neither is a centre popup and `processKey()`
never sees them.

**What the ORACLE does on ESC is still unmeasured.** The plan pointed at
T342's captures; T342 recorded merchant open, select, buy and sell
(`review/T342/T342_events.tsv`) and contains **no ESC event of any kind**. So
fable's shape here is reasoned from T290's desync argument -- which is right
regardless -- and not measured. Boarded as **B460**.

`ORACLE_ROW_H` and the rest of the container's geometry were not touched.

### 3. B453 -- the scene breakdown counts the zone mesh

`ctx_.zoneMeshNode` is now subtracted and inserted into the known set the way
the collision node is. The eleven ad-hoc `otherNodes--; knownNodes.insert(x);`
pairs became one call to `computeSceneNodeCensus()`
(`include/client/graphics/diagnostics/scene_node_census.h`,
`src/client/graphics/diagnostics/scene_node_census.cpp`), which populates both
halves TOGETHER -- so "subtracted but still enumerated" is no longer
expressible. The arithmetic is unchanged: entity subtrees whole
(`countSceneNodes`), two nodes per in-graph door with the pivot collected, sky
and weather by their own counts and collectors, everything else one node.

**What `/dumpscene` should read in a BSP zone** (the question the entry left
open): `unaccounted=0` there too, by the same rule. `zoneMeshNode` is null in a
BSP zone and a null entry is a no-op; the geometry is in `regionMeshNodes`,
which was always subtracted. That is exactly why the count read 0 in every BSP
zone and hid this for as long as the member has existed --
`SceneNodeCensusTest.ABspZonePassesANullZoneMeshAndStillBalances` pins it.

`diagnostics_component.cpp` went 1,159 -> 1,157 lines.

### Proof

**Build.** All background with an exit file, polled with
`scripts/wait_for.sh --exit-file`. `build` (RelWithDebInfo, GLES2 config)
exit 0; `scripts/build-gles2-x86.sh` exit 0; `scripts/build-arm-noble.sh`
exit 0.

One build was killed at the 120 s tool ceiling (exit 143) because a launch
line put `nohup` in the FOREGROUND -- `nohup ... > /dev/null 2>&1; echo $? > f &`
backgrounds only the `echo`. The protocol's post-143 scan was run before
anything else: `libwilleq_core.a` PRESENT with **359** members, `libEtcLib.a`
15, **no** `build/lib/st??????` temp files, and all **341** `build/bin/*`
valid ELF with non-trivial size. No damage -- the kill landed in compilation,
not in the link or `ar`.

**Pre-fix / post-fix.** The three defects were re-injected as source edits
(the fix patch was exported to the scratchpad first; **no `git stash`, no
`git checkout --`**), rebuilt, and run; then restored and the tracked diff
verified byte-identical to the exported patch.

| test | pre-fix | post-fix |
|---|---|---|
| `test_guild_name_table` (13 cases) | **1 FAILED** -- `B418Census.WorldProcessGuildsListActuallyParsesThePacket` | 13/13 |
| `test_esc_server_owned_windows` (9 cases) | **4 FAILED** -- `EscTest.EscapePushesTradeskillCloseIntentAndLeavesThePanelUp`, `EscTest.TheContainerIsDismissedBeforeEveryOtherCentrePopup`, `EscTest.WithNoBridgeTheContainerStaysOpenRatherThanClosingLocally`, `B454Census.EscapeOnTheTradeskillContainerSendsItsCloseIntent` | 9/9 |
| `test_scene_node_census` (10 cases) | **1 FAILED** -- `B453Census.TheBreakdownSubtractsTheZoneMeshNode` | 10/10 |

The first red run had one FEWER failure: the census read the raw source, and
the explanatory COMMENT above the removed arm still named
`TradeskillCloseIntent`, so the census passed against code that did not have
it. All three census helpers now strip comments and string literals before
matching (T442's `codeOnly()` rule) -- a tombstone or a note is not a call
site. That is the second time in this unit a name-only text match gave a false
negative; the first was `check_write_only_fields.py` above.

**ctest.** `ctest --test-dir build -j6`: **4,625 of 4,625 passed, 0 failed,
exit 0** (123 skipped, all pcap/live). Two earlier runs died at gtest
DISCOVERY (`test_spell_manager`, then `test_spell_manager_timing`,
"Process terminated due to timeout") -- a different binary each time, which is
the contention artefact, not truncation: `--gtest_list_tests` on the named
binary exited 0 and its ELF magic was `7f 45 4c 46`. A pass over every
`build/bin/test_*` with `--gtest_list_tests` warmed the cache and the next run
was clean.

The run before that had two reds, both explained and both now green:
`visual_gate` (gfaydark p95 process-CPU 10.843 ms vs ceiling 10.672 -- the
documented under-load artefact; standalone it reads **5.205**) and
`WriteOnlyFields` (the `guildId` parameter-name collision above).

**Visual gate.** Standalone `scripts/ci/visual_gate.sh` exit 0,
`=== VISUAL GATE: PASS ===`, after rebuilding `build-gles2` from this tip.
qeynos2 p95 9.144 (ceiling 10.108), gfaydark 5.205 (10.672), befallen 5.664
(10.948). **No image golden moved** -- `git status` shows no change under
`tests/goldens/` or the gate's reference images, and no statement-order golden
(G05, G06, and the six order/coverage goldens) is in this branch's file
territory.

**File sizes.** `scripts/refactor/check_file_sizes.sh fable` from the task
branch: 13 files checked, **13 ok, 0 warn, 0 fail**. The seven new files are
83 / 101 / 48 / 35 / 238 / 244 / 281 lines.

**Intent and sender pins.** No new intent and no new sender: the container's
ESC reuses `TradeskillCloseIntent`, so `DirectSenderCensus`,
`ReachabilitySweep` and the intent-table doc counts are unchanged and green.

### Deviations

1. **The B418 display half was not implemented**, on measurement, not on
   scope. See §1 -- every candidate surface was eliminated and the last one
   has no oracle capture. Boarded **B459**.
2. **The B454 merchant/bank half was already done** (T436). Pinned as an
   invariant rather than re-implemented.
3. **The oracle's ESC behaviour could not be read from T342** -- there is no
   ESC event in those captures. Boarded **B460**.
4. **B453 was fixed by extraction, not by one line.** The plan asked for a
   hand-built EDT_NULL scene showing `unaccounted=0`; the arithmetic was
   unreachable without a live renderer, so it moved to a device-free function
   first. The extraction is faithful (same subtraction rules, same known-set
   contents) and `diagnostics_component.cpp` got smaller.
5. **Live proof is OWED for all three items** -- this unit held no lock.
   Specifically: `/dumpscene` in Plane of Sky reading `unaccounted=0`; ESC on
   an open forge closing the container only after the server replies; and a
   guilds-list packet from a server with at least one guild reaching
   `GuildNames()`.
