#!/usr/bin/env python3
"""
T407 -- the written-never-read struct/field census.

Why this exists: T404 (board B254 work) found `MixChannel::sampleRate` written
by every producer and read by nobody, for years, two lines below a TSAN banner
that discussed the atomicity of its neighbours.  A field nothing reads is the
same reachability defect class as an uncalled function, and nothing in the tree
looked for it.

What it does:
  1. Parses every struct/class body under include/client/ and src/client/ and
     collects its DATA MEMBERS (declaration lines: no parentheses, ending in
     `;`, at exactly one brace level inside the type).
  2. Sweeps src/ include/ tests/ tools/ benchmarks/ for every occurrence of
     those names, comment- and string-stripped, classifying each as a WRITE
     (`.name =`, `->name =`, `name +=`, a `: name(...)` / `, name{...}`
     constructor initialiser) or a READ (anything else).
  3. Reports the fields with writes and zero reads, and the fields with no
     occurrence at all outside their own declaration ("unused").
  4. `--check` ratchets BOTH halves: a write-only field missing from EXEMPT
     and an unused field missing from EXEMPT_UNUSED both fail, and a table
     entry whose field is no longer in that state also fails, so neither
     table can rot.

Accuracy note -- deliberately asymmetric.  Member names are matched by
IDENTIFIER, not by receiver type, so a name shared with another class (`name`,
`id`, `x`) collects that class's occurrences too.  Every such collision can
only ADD reads, so it can only HIDE a write-only field, never invent one.  The
report is therefore a floor: everything it names is worth looking at, and it
misses some.  Sort by how distinctive the name is when triaging.

Usage:
    python3 scripts/refactor/check_write_only_fields.py [--repo ROOT]
                [--min-writes N] [--include-unused] [--json OUT]
"""

import argparse
import os
import re
import sys
from collections import defaultdict

DECL_DIRS = ("include/client", "src/client")
SWEEP_DIRS = ("src", "include", "tests", "tools", "benchmarks")
EXTS = (".h", ".hpp", ".cpp", ".cc", ".inl")

# Names that are almost always something else as well; they are still reported,
# but the collision makes their read counts meaningless.
NOISE = {"x", "y", "z", "w", "r", "g", "b", "a", "i", "j", "k", "n", "m",
         "id", "name", "type", "size", "count", "data", "value", "index",
         "first", "second", "begin", "end", "length", "width", "height"}

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRINGS = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'")

TYPE_OPEN = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?"
                       r"(struct|class|union)\s+(?:\w+_API\s+)?(\w+)\b")

DECL = re.compile(
    r"^\s*(?:mutable\s+|static\s+|constexpr\s+|inline\s+|volatile\s+|"
    r"thread_local\s+|const\s+)*"          # storage/cv prefixes
    r"[A-Za-z_][\w:<>,\s\*&]*?"            # the type
    r"[\s\*&]"                             # separator before the name
    r"(\w+)"                               # the member name
    r"\s*(?:\[[^\]]*\])*"                  # optional array extents
    r"\s*(?:=[^;]*|\{[^;]*\}|:\s*\d+)?"    # optional initialiser / bitfield
    r"\s*;\s*$")

SKIP_DECL_PREFIX = ("typedef", "using", "friend", "return", "static_assert",
                    "public:", "private:", "protected:", "enum", "namespace",
                    "template", "operator", "delete", "throw", "case", "goto")


def strip_noncode(text):
    """Blank out block comments, line comments and string/char literals."""
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRINGS.sub('""', text)
    return text


def walk(root, dirs):
    for d in dirs:
        base = os.path.join(root, d)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames
                           if x not in ("third_party", "build", ".git")]
            for fn in filenames:
                if fn.endswith(EXTS):
                    yield os.path.join(dirpath, fn)


def collect_members(root):
    """{(TypeName, member): (relpath, lineno)} plus a member -> types index."""
    members = {}
    for path in walk(root, DECL_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        depth = 0
        stack = []          # (type_name, depth_at_open)
        pending = None      # a type header seen, brace not yet opened
        for lineno, line in enumerate(src.split("\n"), 1):
            code = line.split("//")[0]
            m = TYPE_OPEN.match(code)
            if m and not code.rstrip().endswith(";") and "(" not in code:
                pending = m.group(2)
            if stack and depth == stack[-1][1] + 1:
                stripped = code.strip()
                if (stripped and "(" not in stripped and ")" not in stripped
                        and not stripped.startswith(SKIP_DECL_PREFIX)):
                    d = DECL.match(code)
                    if d:
                        nm = d.group(1)
                        if nm not in ("struct", "class", "union", "const",
                                      "static", "unsigned", "signed"):
                            members.setdefault((stack[-1][0], nm), (rel, lineno))
            opens = code.count("{")
            closes = code.count("}")
            for _ in range(opens):
                depth += 1
                if pending is not None:
                    stack.append((pending, depth - 1))
                    pending = None
            for _ in range(closes):
                if stack and stack[-1][1] == depth - 1:
                    stack.pop()
                depth = max(0, depth - 1)
        # end file
    return members



# ---------------------------------------------------------------------------
# T414 Leg C (board B290): the triage table.
#
# Every write-only row the census reports must appear here with a CATEGORY and
# a REASON.  A row that is not listed FAILS `--check` (and so the `WriteOnlyFields`
# ctest test): a new field written and never read has to be justified or fixed
# before it can be merged.  A row listed here that the census NO LONGER reports
# also fails, so the table cannot rot -- when a field is wired or deleted, its
# entry goes with it.
#
# Categories:
#   wire     the struct mirrors bytes on disk or on the network; the field must
#            exist for the layout even where this client ignores its value.
#   boarded  a real gap with an open board entry that owns it.  The reason names
#            the entry; do not re-board.
#   owned    another in-flight unit owns the file; hands off.
#   report   a diagnostic aggregate whose consumer is a report that does not
#            currently print this member.  Boarded as a group.
# ---------------------------------------------------------------------------

# T416: the four rows T413 made stale (CastingStateChangedData::
# castTimeRemainingMs / ::castTimeTotalMs, CursorItemChangedData::queueSize,
# SpellMemorizingData::progressMs) are DELETED, not updated -- T413 gave each
# of them a real consumer, and this checker fails on an exemption whose field
# is no longer write-only. `WriteOnlyFields` was RED at 6522c387 for exactly
# that reason; nothing else about T413's merge was incomplete.
EXEMPT = {
    # B533 (issue #180) closed the CameraController::mouseSensitivity_ row
    # (2026-10-11): T461 deleted CameraController::update() with the Free
    # mode, and B533 then deleted the residue update() left write-only --
    # the setter, the field and the init call in irrlicht_renderer.cpp --
    # so the field no longer exists at all.
    # T421 closed B298's item half: the fourteen fields that HAVE an oracle
    # line are drawn by the item display window, and their exemptions are
    # gone. `ItemInstance::currentSlot` (the last item row) lost its
    # exemption the same way T421/T425 deleted the others: B894's
    # tests/test_forage_fishing.cpp read it to pin the caught item's
    # normalised slot, giving it a real consumer. The oracle-shows-nothing
    # half of B298's item census is fully closed.
    # T425 (B298 spell half): the spell display window EXISTS now
    # (BuildSpellDisplayText, drawn in the oracle's one ItemDisplayWindow).
    # These four have no line in it because the official client's string
    # table has no format string for them -- measured by grepping
    # /mnt/projects/EverQuestP1999/eqstr_us.txt, not assumed.
    ("SpellData", "player_tag"):
        ("boarded", "B298 (spell half CLOSED by T425): spells_us.txt internal tag ('PLAYER_1'); no eqstr format string"),
    ("SpellData", "resist_diff"):
        ("boarded", "B298 (spell half CLOSED by T425): eqstr has NO spell-resist line -- every 'Resist' hit is a "
                   "combat message, an AA name, or 9044 the ITEM stun-resist stat"),
    ("SpellData", "teleport_zone"):
        ("boarded", "B298 (spell half CLOSED by T425): eqstr has no Destination/Location format string"),
    ("SpellData", "zone_type"):
        ("boarded", "B298 (spell half CLOSED by T425): eqstr has no Indoor/Outdoor format string"),
    # The other SEVEN rows of B298's spell census. They surfaced as NEW the
    # moment tests/test_spell_display.cpp WROTE them to assert they stay
    # hidden -- the same shape T421 hit with the seven `heroic*`, and the
    # checker doing its job. Each has no eqstr format string either
    # (Indoor/Outdoor, environment, time of day, AoE duration, knockback,
    # med-while-active and dispellability are all absent from the table).
    ("SpellData", "allow_rest"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- the oracle never prints med-while-active"),
    ("SpellData", "aoe_duration"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- the oracle never prints an AoE duration"),
    ("SpellData", "environment_type"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- no environment format string exists"),
    ("SpellData", "is_dispellable"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- no dispel format string exists"),
    ("SpellData", "no_partial_resist"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- the table has no spell-resist line at all"),
    ("SpellData", "push_up"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- the oracle never prints knockback"),
    ("SpellData", "time_of_day"):
        ("boarded", "B298 (spell half CLOSED by T425): no eqstr line -- no time-of-day format string exists"),
    ("SpellEffectSlot", "base2_value"):
        ("boarded", "B298: spell record field; the oracle prints no per-effect "
                    "numbers at all in the display window"),
    # T429 Leg B closed B298's SKILL and ENTITY halves. Neither of these five
    # is a missing DISPLAY, which is what B298 was about: each was measured
    # against the oracle's XML and against eqstr_us.txt, and none of them has
    # a line. The two SkillData rows and the three Entity rows are three
    # DIFFERENT defects, boarded separately (B415/B416/B418).
    # T440 STRUCK the two SkillData rows by DELETING both fields (B415, B416):
    # animation_code was a duplicate of getSkillAnimationCode(), and eqemu has
    # no sitting refusal any fable skill activation can reach. The exemptions
    # went with the fields -- an exemption may not outlive its row.
    ("Entity", "anon_status"):
        ("boarded", "B298 (entity half CLOSED by T429): the oracle's anon lines are the /who "
                    "formats 5023/5024, whose '[ANONYMOUS]' is inside the format itself -- and "
                    "the /who path parses its OWN wire entries (who_response.h), never Entity. "
                    "EQUI_TargetWindow.xml has only a name label, an HP gauge and a '%' label; "
                    "there is no element for it. B418"),
    ("Entity", "guildId"):
        ("boarded", "B459 (was B298/B418): fable HAS a guild-id-to-name source now -- T443 "
                    "parsed OP_GuildsList into eqt::guild::GuildNameTable and B418 is struck -- "
                    "but there is still nowhere it may be DRAWN. The /who guild fragment is a "
                    "wire string eqemu already fills and the /who path never reads Entity; "
                    "EQUI_TargetWindow.xml has no guild element; and the overhead name tag, "
                    "which is the oracle's one remaining surface, has NO measured format in any "
                    "capture, so drawing one would invent it (T363). Closes when the official "
                    "client is captured with a guilded character in view. B459"),
    ("Entity", "deltaHeading"):
        ("boarded", "B298 (entity half CLOSED by T429): a MOVEMENT field, not a display one -- "
                    "the wire's delta heading, the sibling of deltaX/Y/Z that the renderer "
                    "extrapolates with. No eqstr line and no XML element exists for it "
                    "anywhere. B418"),
    ("RaceModelInfo", "genderVariants"):
        ("boarded", "B298: race record field from willeq.db"),
    ("RaceModelInfo", "neuterCode"):
        ("boarded", "B298: race record field from willeq.db"),
    ("GraphicsArchiveIndex", "lazyMode_"):
        ("boarded", "B295: buildIndex() stores its lazyMode argument and never reads it"),
    ("EQE1Header", "reserved"):
        ("wire", "EQE1 header padding; the on-disk layout requires it"),
    ("WldFragment37Header", "param2"):
        ("wire", "WLD 0x37 fragment header field this client does not use"),
    ("SkyLayer", "isCloud"):
        ("wire", "parsed from sky.wld; kept so the parsed record matches the file"),
    ("SkyTrack", "frameDelayMs"):
        ("wire", "parsed from sky.wld; kept so the parsed record matches the file"),
    ("SkyType", "typeId"):
        ("wire", "parsed from sky.wld; kept so the parsed record matches the file"),
    ("DoorVisual", "incline_raw"):
        ("wire", "the server's raw OP_SpawnDoor byte kept beside the decoded value"),
    ("DoorVisual", "initiallyOpen_raw"):
        ("wire", "the server's raw OP_SpawnDoor byte kept beside the decoded value"),
    ("RDPPeerContext", "selectedAudioFormat"):
        ("wire", "FreeRDP negotiates and stores the format; the server side reads it"),
    ("MemoryStats", "animatedMeshCacheCount"):
        ("report", "B337: /pmem aggregates a loader fills and the report never prints"),
    ("MemoryStats", "armorTextureCount"):
        ("report", "B337: /pmem aggregates a loader fills and the report never prints"),
    # T500: loadedModelCount is no longer write-only -- the [modelmem] line
    # (RaceModelLoader::modelMemLogLine) prints it once per zone-in, so its
    # B337 exemption came out. The other four MemoryStats counters below are
    # still filled and never printed.
    ("MemoryStats", "variantMeshCacheCount"):
        ("report", "B337: /pmem aggregates a loader fills and the report never prints"),
    ("MemoryStats", "meshCacheCount"):
        ("report", "B337: /pmem aggregates a loader fills and the report never prints"),
    ("Stats", "excludedMeshCount"):
        ("report", "B337: TreeIdentifier counters filled and never printed"),
    ("Stats", "leafPatternCount"):
        ("report", "B337: TreeIdentifier counters filled and never printed"),
    ("Stats", "treePatternCount"):
        ("report", "B337: TreeIdentifier counters filled and never printed"),
    ("Stats", "zoneTreeMeshCount"):
        ("report", "B337: TreeIdentifier counters filled and never printed"),
    # B339/B340/B341/B344/B345 rows (targetIntensity_, entitiesLoaded_,
    # expectedEntityCount_, currentTargetInfo_, m_audio_config_vendor_music,
    # m_duel_target_id, m_player_race, orbitAngleInitialized_, m_curEnd,
    # m_playerHeading, m_startTime, ConflictInfo::binding1/binding2) went
    # with their fields -- an exemption may not outlive its row.
    ("InventoryManager", "deleteItemCallback_"):
        ("boarded", "B342: three registered callbacks the manager never invokes"),
    ("InventoryManager", "equipmentChangedCallback_"):
        ("boarded", "B342: three registered callbacks the manager never invokes"),
    ("InventoryManager", "moveItemCallback_"):
        ("boarded", "B342: three registered callbacks the manager never invokes"),
    ("ParticleManager", "entityDirCallback_"):
        ("boarded", "B342: registered callback the manager never invokes"),
    ("CharacterModelView", "inventoryManager_"):
        ("boarded", "B343: paperdoll holds the inventory manager and reads nothing from it"),
    ("EQAnimatedMeshSceneNode", "sceneManager_"):
        ("boarded", "B343: stored scene manager with no reader"),
    ("EntityResult", "shouldDeactivate"):
        ("boarded", "B346: SimulationWorker sets it and the drain ignores it"),
    ("SimulationInput", "cameraTarget"):
        ("boarded", "B346: filled per frame, no sim pass reads it"),
    ("EntityVisual", "corpseYOffset"):
        ("boarded", "B346: computed per corpse and never applied"),
    ("ParticleEmitter", "spawnRadius_"):
        ("boarded", "B346: emitter spawn radius set by seven callers, unused in the spawn"),
    ("SpellEffectInstance", "spellID"):
        ("boarded", "B346: FX instance records the spell and nothing looks it up"),
    ("RendererEvent", "intData"):
        ("boarded", "B346: renderer event payload with no consumer"),
    ("SpellActionInfo", "level2"):
        ("boarded", "B346: OP_Action field with no consumer"),
}



# ---------------------------------------------------------------------------
# B840 (kanban t_a817842a): the NEVER-TOUCHED half of the census, as a ratchet.
#
# The write-only census above (T407/T414) collects both halves -- write-only
# (writes, zero reads) AND unused (no occurrence at all outside the
# declaration) -- but its --check mode ratcheted only the write-only half.
# `RaceModelData::decodedTextures` (race_model_loader.h:50) is what that
# blind spot cost: a field with ZERO writers and zero readers, dead since the
# background-decode/main-thread-upload scheme it was for was superseded by
# ConstrainedTextureCache (T500 measured it; the archive row T05 even says
# "Remove RaceModelData::decodedTextures"). Same asymmetry note as above: a
# name shared with another class can only ADD occurrences, so it can only
# HIDE an unused field, never invent one -- the report is a floor.
#
# Every unused row the census reports must appear here with a CATEGORY and a
# REASON. A row that is not listed FAILS --check (and so the WriteOnlyFields
# ctest test): a new field with no writer and no reader has to be justified
# or deleted before it can be merged. A row listed here that the census NO
# LONGER reports also fails, so the table cannot rot -- when a field is
# wired or deleted, its entry goes with it. Same categories as EXEMPT, plus
# two that are the whole story for this half:
#   wire     mirrors a file or wire layout (DDS/WLD/packet/DB); the field
#            must exist for the record even where nothing reads its value.
#   report   a diagnostic aggregate or config knob with no consumer yet.
#   unused   dead -- no writer, no reader. Kept (usually: the owning record
#            is a shipped DB/wire row, or the field is mid-flight) but every
#            row names what would make it live, so "unused" never rots into
#            "forgot".
#
# NOTE: the B840 row (RaceModelData, decodedTextures) is deliberately NOT in
# the committed table: the field still exists at this commit, so the ratchet
# fails on it -- that is the RED this unit captures. The deletion commit
# removes the field, and the row goes with it (it was never added, which is
# the same as adding and removing in one step, but keeps the RED state in
# the diff). The RED/GREEN capture is in refactor/BOARD.md's B840 entry.
#
# The 86 rows below are the typed census's unused list at B840's tip,
# minus RaceModelData::decodedTextures: the 81 rows the old name-matching
# sweep had already seen (its "81 unused" report never included the B840
# field, which the sweep's bare-name matching masked) plus 5 more the
# receiver-typing sweep surfaced. Each of the five was masked in the old
# sweep by a same-named member of a LIVE type that collected the bare-name
# occurrences: SpellActionInfo's force/hit_heading/hit_pitch (the
# combat_actions.cpp:168-171 copies) for the CombatDamage_Struct side,
# TextureAnimationInfo::animationDelayMs (the WLD animation path) for
# TextureInfo's, and WldFragment36Header::size9 (the 0x24 log line) for the
# 0x2C legacy header's. Each is verified genuinely unused in its own type.
# A field the first (line-granular) sweep flagged that the final
# offset-granular sweep sees as used is NOT listed -- listing it would
# STALE the ratchet.
EXEMPT_UNUSED = {
    # Packed wire mirrors: the fields must exist for the record layout even
    # where this client never reads the value.
    ("ActionPacket_Struct", "unknown23"):
        ("wire", "packed action-packet mirror; layout field this client does not decode"),
    ("ActionPacket_Struct", "unknown25"):
        ("wire", "packed action-packet mirror; layout field this client does not decode"),
    ("CombatDamage_Struct", "unknown06"):
        ("wire", "packed combat-damage mirror; layout field this client does not decode"),

    # B840 receiver-typing sweep, final five (verified against the tree):
    ("CombatDamage_Struct", "force"):
        ("wire", "packed combat-damage mirror; the fields are copied to no local at combat_actions.cpp:268-272, read by nothing"),
    ("CombatDamage_Struct", "hit_heading"):
        ("wire", "packed combat-damage mirror; the fields are copied to no local at combat_actions.cpp:268-272, read by nothing"),
    ("CombatDamage_Struct", "hit_pitch"):
        ("wire", "packed combat-damage mirror; the fields are copied to no local at combat_actions.cpp:268-272, read by nothing"),
    ("TextureInfo", "animationDelayMs"):
        ("unused", "S3D texture record field; the S3D records carry no delay (nothing writes it) and the animation path reads the WLD side (TextureAnimationInfo::animationDelayMs), never this one (a TextureInfo-based animation reader would make it live)"),
    ("WldFragment2CHeader", "size9"):
        ("wire", "WLD 0x2C legacy fragment header; the 0x2C parser (wld_fragments_actor.cpp) reads only the size6-era fields -- the size9 log line at wld_fragments_mesh.cpp:37 is the 36Header's"),
    ("Weather_Struct", "pad1"):
        ("wire", "packed weather packet padding; the wire layout requires it"),
    ("Weather_Struct", "pad2"):
        ("wire", "packed weather packet padding; the wire layout requires it"),
    # The DDS header mirrors the on-disk format; the decoder reads flags/
    # fourCC/pitch/height/width/pixelFormat and skips the rest by offset.
    ("DDSHeader", "pitchOrLinearSize"):
        ("wire", "DDS on-disk header field; the decoder addresses the data by offset"),
    ("DDSHeader", "mipMapCount"):
        ("wire", "DDS on-disk header field; the decoder does not walk mips"),
    ("DDSHeader", "reserved1"):
        ("wire", "DDS on-disk header padding; the format requires it"),
    ("DDSHeader", "caps2"):
        ("wire", "DDS on-disk header field; only `caps` is consulted"),
    ("DDSHeader", "caps3"):
        ("wire", "DDS on-disk header field; only `caps` is consulted"),
    ("DDSHeader", "caps4"):
        ("wire", "DDS on-disk header field; only `caps` is consulted"),
    ("DDSHeader", "reserved2"):
        ("wire", "DDS on-disk header padding; the format requires it"),
    ("DDSPixelFormat", "rgbBitCount"):
        ("wire", "DDS pixel-format field; the decoder keys on fourCC, not the bit counts"),
    ("DDSPixelFormat", "rBitMask"):
        ("wire", "DDS pixel-format field; the decoder keys on fourCC, not the masks"),
    ("DDSPixelFormat", "gBitMask"):
        ("wire", "DDS pixel-format field; the decoder keys on fourCC, not the masks"),
    ("DDSPixelFormat", "bBitMask"):
        ("wire", "DDS pixel-format field; the decoder keys on fourCC, not the masks"),
    ("DDSPixelFormat", "aBitMask"):
        ("wire", "DDS pixel-format field; the decoder keys on fourCC, not the masks"),
    # WLD on-disk headers: the fragment parsers address the bytes and skip
    # the unknowns; the test comments that name frag3/frag4/params2 are
    # byte-size arithmetic, not reads of the fields.
    ("WldHeader", "unk3"):
        ("wire", "WLD 0x03 header field; the parser reads past it by offset"),
    ("WldFragment12Header", "rotXNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the rotation"),
    ("WldFragment12Header", "rotYNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the rotation"),
    ("WldFragment12Header", "rotZNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the rotation"),
    ("WldFragment12Header", "shiftXNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the shift"),
    ("WldFragment12Header", "shiftYNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the shift"),
    ("WldFragment12Header", "shiftZNum"):
        ("wire", "WLD 0x0C fragment header field; the fragment parser does not use the shift"),
    ("WldFragment36Header", "frag3"):
        ("wire", "WLD 0x24 fragment header; the mesh builder reads frag1/frag2 and skips by offset"),
    ("WldFragment36Header", "frag4"):
        ("wire", "WLD 0x24 fragment header; the mesh builder reads frag1/frag2 and skips by offset"),
    ("WldFragment36Header", "params2"):
        ("wire", "WLD 0x24 fragment header field; the mesh builder does not decode it"),
    # willeq.db mirrors: the client reads the row, it just does not consume
    # this column yet.
    ("AugmentSlot", "augmentId"):
        ("wire", "augment record from willeq.db; the item path reads the slot type, not the occupant"),
    ("ItemEffect", "recastType"):
        ("wire", "item effect record from willeq.db; only castTime/recastDelay reach a consumer"),
    ("SpellData", "no_expend_reagent"):
        ("wire", "spells record column from willeq.db; the noexpend ids are the live half (T425)"),
    ("SkillData", "min_level"):
        ("wire", "skills record column from willeq.db; no level gate consumes it"),
    ("SkillData", "usable_while_stunned"):
        ("wire", "skills record column from willeq.db; no stun gate consumes it"),
    ("EntityAppearanceData", "hairColor"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "beardColor"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "eyeColor1"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "eyeColor2"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "hairStyle"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "heritage"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "tattoo"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "details"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "drakkinHeritage"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "drakkinTattoo"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "drakkinDetails"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    ("EntityAppearanceData", "equipmentMaterial"):
        ("wire", "output-interface appearance mirror of the wire record; the live appearance is EntityAppearance"),
    # Dead: no writer, no reader. Kept only because a deletion wants its own
    # reachability check and an owner (B468's lesson); each row names what
    # would make it live.
    ("AudioManager", "LOOPBACK_SAMPLE_RATE"):
        ("unused", "loopback output format constant; the audio manager has no loopback path (a loopback sink that reads it would make it live)"),
    ("AudioManager", "LOOPBACK_CHANNELS"):
        ("unused", "loopback output format constant; the audio manager has no loopback path (a loopback sink that reads it would make it live)"),
    ("BuffManager", "MAX_PLAYER_SONGS"):
        ("unused", "song-buff cap constant; no cap check or song-type test consults it (a song cap in addBuff would make it live)"),
    ("Command", "detailedHelp"):
        ("unused", "help text the registry never fills and /help never prints (printing long help in BuildCommandDetail would make it live)"),
    ("EntityVisual", "occlusionHidden"):
        ("unused", "per-frame occlusion cache; the occlusion test that would fill/read it was never wired (a line-of-sight cull in entity culling would make it live)"),
    ("EntityVisual", "occlusionFrame"):
        ("unused", "per-frame occlusion cache; the occlusion test that would fill/read it was never wired (a line-of-sight cull in entity culling would make it live)"),
    ("EntityVisual", "occlusionTestX"):
        ("unused", "per-frame occlusion cache; the occlusion test that would fill/read it was never wired (a line-of-sight cull in entity culling would make it live)"),
    ("EntityVisual", "velocityHeading"):
        ("unused", "interpolation velocity; the movement extrapolator uses velocityX/Y/Z, not a heading (rotating the extrapolation by it would make it live)"),
    ("EntityVisual", "fpAttackTimer"):
        ("unused", "first-person attack animation state; the FP attack animation that would tick it does not exist (drawing the swing with it would make it live)"),
    ("EntityVisual", "fpAttackDuration"):
        ("unused", "first-person attack animation state; the FP attack animation that would tick it does not exist (drawing the swing with it would make it live)"),
    ("EntityVisual", "fpIsAttacking"):
        ("unused", "first-person attack animation state; the FP attack animation that would tick it does not exist (drawing the swing with it would make it live)"),
    ("PlaceableInstance", "geometryIndex"):
        ("unused", "object geometry list index; the placeable path uses the owned Placeable, not a shared geometry list (a mesh cache keyed by it would make it live)"),
    ("RendererConfig", "ambientIntensity"):
        ("unused", "ambient light level; the all-shader pipeline defaults to fullbright and no FS reads an ambient term (uAmbient in the VS would make it live)"),
    ("SkyLoadData", "preDecodedTextures"):
        ("unused", "pre-T14 sky decode path; sky textures now load via ConstrainedTextureCache (T14 deleted the uploader that read it)"),
    ("TextEntry", "hcenter"):
        ("unused", "TextBatch entry flag; the text entry construction that would set it was never written (a centred-draw option in TextBatch::add would make it live)"),
    ("TextEntry", "vcenter"):
        ("unused", "TextBatch entry flag; the text entry construction that would set it was never written (a centred-draw option in TextBatch::add would make it live)"),
    ("ZoneDoorBounds", "closedBounds"):
        ("unused", "door AABB; no collision or render path consults the closed vs open bounds (BSP collision against them would make it live)"),
    ("ZoneDoorBounds", "openBounds"):
        ("unused", "door AABB; no collision or render path consults the closed vs open bounds (BSP collision against them would make it live)"),
    ("ZoneDoorBounds", "liftFloorZMin"):
        ("unused", "lift floor range; the lift movement that would move the floor through it does not exist (animating the lift with it would make it live)"),
    ("ZoneDoorBounds", "liftFloorZMax"):
        ("unused", "lift floor range; the lift movement that would move the floor through it does not exist (animating the lift with it would make it live)"),
    ("ZoneDoorBounds", "liftFloorZCurrent"):
        ("unused", "lift floor range; the lift movement that would move the floor through it does not exist (animating the lift with it would make it live)"),
    ("ZoneLightData", "colorRGBA"):
        ("unused", "zone light colour; the renderer's light data keeps its own fields and this packed colour reaches no shader (passing it to the per-pixel light FS would make it live)"),
    ("ZoneWeatherConfig", "snowChance"):
        ("unused", "snow probability table; the weather system has no snow generator (a snow scheduler reading it would make it live)"),
    ("ZoneWeatherConfig", "snowDuration"):
        ("unused", "snow duration table; the weather system has no snow generator (a snow scheduler reading it would make it live)"),
    # Weather effect config: the particle systems do not parameterise off
    # these; the controller's particle emitter uses its own rates.
    ("Rain", "dropSpeed"):
        ("unused", "rain particle config; the emitter uses its own fall rate, not this knob (parameterising the emitter from the config would make it live)"),
    ("Rain", "spawnRadius"):
        ("unused", "rain particle config; the emitter uses its own spawn footprint, not this knob (parameterising the emitter from the config would make it live)"),
    ("Rain", "spawnHeight"):
        ("unused", "rain particle config; the emitter uses its own spawn ceiling, not this knob (parameterising the emitter from the config would make it live)"),
    ("Rain", "splashEnabled"):
        ("unused", "rain particle config; the splash emitter was never written (a splash system gated on it would make it live)"),
    ("Rain", "splashParticles"):
        ("unused", "rain particle config; the splash emitter was never written (a splash system gated on it would make it live)"),
    ("Snow", "fallSpeed"):
        ("unused", "snow particle config; the snow emitter uses its own rates, not these knobs (parameterising the emitter from the config would make it live)"),
    ("Snow", "accumulationEnabled"):
        ("unused", "snow accumulation flag; the comment says future feature -- the accumulation system does not exist (a snow accumulation pass would make it live)"),
    ("Snow", "maxAccumulationDepth"):
        ("unused", "snow accumulation flag; the comment says future feature -- the accumulation system does not exist (a snow accumulation pass would make it live)"),
    # B893 jump/fall: the documented [ASSUMED, OWED] table slot. The lane-2
    # capture cannot resolve the terminal velocity (the `#goto` fall is a
    # server-side teleport the client integrates locally), and the integrator
    # has no terminal-velocity term; the measured value lands in this slot
    # when obtained (refactor/B893/README.md, owed-constants section).
    ("Arc", "terminalSpeed"):
        ("unused", "B893: documented [ASSUMED, OWED] slot for the measured terminal fall velocity (refactor/B893/README.md); nothing clamps to it yet (a terminal-velocity term in the fall integrator would make it live)"),
    # /perf report aggregates that are filled and never printed.
    ("SceneBreakdown", "withoutZone"):
        ("report", "B337: /perf scene breakdown; the sub-pass timings are filled and the report never prints them"),
    ("SceneBreakdown", "withoutEntities"):
        ("report", "B337: /perf scene breakdown; the sub-pass timings are filled and the report never prints them"),
    ("SceneBreakdown", "withoutObjects"):
        ("report", "B337: /perf scene breakdown; the sub-pass timings are filled and the report never prints them"),
    ("SceneBreakdown", "withoutDoors"):
        ("report", "B337: /perf scene breakdown; the sub-pass timings are filled and the report never prints them"),
}



ASSIGN = re.compile(r"^\s*(?:\+|-|\*|/|%|\||&|\^|<<|>>)?=(?!=)")
CTOR_INIT_PREFIX = re.compile(r"[:,]\s*$")


# ---------------------------------------------------------------------------
# B840: receiver typing. The bare-identifier sweep matches a member name
# against EVERY class that has a member of that name, which can only ADD
# occurrences to a given (type, member) pair. That asymmetry is survivable
# for the write-only half (a collision can only HIDE a write-only field,
# never invent one), but it is FATAL for the unused half: RaceModelData::
# decodedTextures (zero real references) was hidden for years by the
# DIFFERENT member EquipmentPrepData::decodedTextures, written at
# entity_prep_worker.cpp:462 and read at :470 through a variable the census
# could not type (`PrepResult::EquipmentPrepData prepData;` at :426),
# because the bare name `decodedTextures` matched the receiver and the sweep
# had no type to check against.
#
# The fix types the receiver wherever the tree types it, per file (one
# scope: the first declaration of a name wins, which can only ever make a
# name look typed when it is, never the reverse):
#   T x;  /  T x = ...;  /  T x{...};     a declaration or default-init
#   auto x = std::make_shared<T>()        a make_shared/make_unique init
#   x = std::make_shared<T>()             the same, as a plain assignment
#   x = y.find(k) / y.at(k)               where y is a locally-typed map
# A qualified use (`x.name`, `x->name`) whose receiver type is known is
# attributed to THAT type's member. The unused half then classifies a
# (type, member) on the occurrences NOT attributed to a different type that
# also has a member of that name; an occurrence the sweep could not type
# still counts for every type -- the pre-B840 floor -- so the new logic can
# only REMOVE hiding, never invent a false unused. The write-only half keeps
# the bare floor untouched (its 67 classified rows must not move).
# ---------------------------------------------------------------------------

TYPED_DECL = re.compile(
    r"^\s*(const\s+)?([A-Za-z_][\w:]*\s*(?:<[^;{}]*>)?)"
    r"(?:\s*(?:\*|&)+)?\s+(\w+)\s*([=;{])")
AUTO_DECL = re.compile(r"^\s*auto\s+(\w+)\s*=\s*([^;]+);")
ASSIGN_LINE = re.compile(r"^\s*([A-Za-z_]\w*)\s*=(?!=)\s*([^;]+);")
MAKE_INIT = re.compile(r"make_(?:shared|unique)\s*<\s*([A-Za-z_][\w:]*)")
MAP_LOOKUP = re.compile(r"^\s*([A-Za-z_][\w.]*?)\s*\.\s*(?:find|at)\s*\(")


def strip_qual(t):
    """`const std::shared_ptr<RaceModelData>` / `X*` -> the base type text."""
    t = (t or "").strip()
    if t.startswith("const "):
        t = t[len("const "):].strip()
    m = re.match(r"^(?:std::)?(?:shared_ptr|unique_ptr|weak_ptr)\s*<\s*(.+?)\s*>$",
                 t)
    if m:
        t = m.group(1)
    return t.rstrip("*&").strip()


def base_name(t):
    """The last :: segment of a possibly-qualified type name."""
    s = strip_qual(t)
    return s.rsplit("::", 1)[-1] if s else None


CONTAINER_MEMBER = re.compile(
    r"^\s*(?:const\s+)?(?:std::)?vector\s*<\s*([A-Za-z_][\w:]*)"
    r"\s*>\s*(\w+)\s*;")
ARRAY_MEMBER = re.compile(
    r"^\s*(?:const\s+)?([A-Za-z_][\w:]*)\s+(\w+)\s*\[[^\]]*\];")


def build_container_registry(root):
    """container-variable -> element type, for class members declared as
    std::vector<T> name; or T name[N]; under the declaration roots. A
    `const T*`/`T*` member is NOT registered: element typing through a raw
    pointer would need the callee's array size to be meaningful, and the
    floor (untyped) is the safe side."""
    registry = {}
    for path in walk(root, DECL_DIRS):
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        for line in src.split("\n"):
            m = CONTAINER_MEMBER.match(line)
            if m and m.group(2) not in registry:
                registry[m.group(2)] = m.group(1)
                continue
            m = ARRAY_MEMBER.match(line)
            if m and m.group(2) not in registry:
                registry[m.group(2)] = m.group(1)
    return registry


def map_value_type(declared):
    """The second top-level template arg of std::map<K,V>/unordered_map."""
    if not re.match(r"^(?:std::)?(?:unordered_)?map\s*<", declared or ""):
        return None
    i = declared.find("<")
    depth, args, cur = 0, [], ""
    for ch in declared[i:]:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                args.append(cur)
                break
        elif ch == "," and depth == 1:
            args.append(cur)
            cur = ""
        else:
            cur += ch
    return args[1].strip() if len(args) >= 2 else None


INDEXED = re.compile(r"^(\w+)\s*(?:\[.*\]|\.at\s*\([^)]*\))$")


def type_of_initializer(init, var_types, containers):
    """The type an initialiser expression denotes, or None (the floor)."""
    init = init.strip()
    m = MAKE_INIT.search(init)
    if m:
        return m.group(1)
    m = MAP_LOOKUP.match(init)
    if m and m.group(1) in var_types:
        return map_value_type(var_types[m.group(1)])
    m = INDEXED.match(init)
    if m and m.group(1) in containers:
        return containers[m.group(1)]
    if init in var_types:
        return var_types[init]
    return None


def sweep(root, names, containers):
    """name -> {'w': [...], 'r': [...], 'tw': {member_type: [...]},
    'tr': {member_type: [...]}}. The plain w/r lists keep EVERY occurrence
    (the pre-B840 floor, still the basis of the write-only half); the tw/tr
    dicts attribute a qualified use to the receiver's own base type where
    the tree types the receiver, so a use of EquipmentPrepData::
    decodedTextures stops counting as a use of RaceModelData::decodedTextures
    (and nothing stops counting for the type it really belongs to)."""
    hits = defaultdict(lambda: {"w": [], "r": [],
                                "tw": defaultdict(list),
                                "tr": defaultdict(list)})
    pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names),
                                              key=len, reverse=True)) + r")\b")
    for path in walk(root, SWEEP_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        var_types = {}
        for lineno, line in enumerate(src.split("\n"), 1):
            m = TYPED_DECL.match(line)
            if m:
                # Any tail (=, ;, {) is a declaration of the name: type the
                # first declaration seen, keep the rest (the floor).
                vname = m.group(3)
                if vname not in var_types:
                    var_types[vname] = m.group(2)
            else:
                m = AUTO_DECL.match(line)
                if m:
                    vname, init = m.group(1), m.group(2)
                    if vname not in var_types:
                        t = type_of_initializer(init, var_types, containers)
                        if t:
                            var_types[vname] = t
                else:
                    m = ASSIGN_LINE.match(line)
                    if m:
                        vname, rhs = m.group(1), m.group(2)
                        if vname not in var_types:
                            t = type_of_initializer(rhs, var_types, containers)
                            if t:
                                var_types[vname] = t
            for mm in pat.finditer(line):
                nm = mm.group(1)
                before = line[:mm.start()]
                after = line[mm.end():]
                is_write = False
                if ASSIGN.match(after):
                    is_write = True
                elif (after[:1] in ("(", "{")
                      and CTOR_INIT_PREFIX.search(before)):
                    is_write = True
                bucket = "w" if is_write else "r"
                pos = (rel, lineno, mm.start())
                hits[nm][bucket].append(pos)
                if before.endswith("->") or before.endswith("."):
                    rm = re.search(r"([A-Za-z_]\w*)\s*(?:->|\.)\s*$", before)
                    if rm:
                        rt = var_types.get(rm.group(1))
                        if rt:
                            b = base_name(rt)
                            if b:
                                hits[nm]["t" + bucket][b].append(pos)
    return hits



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--min-writes", type=int, default=1)
    ap.add_argument("--include-unused", action="store_true",
                    help="also list members with no occurrence at all")
    ap.add_argument("--json")
    ap.add_argument("--check", action="store_true",
                    help="ratchet mode: exit 1 if any write-only field is missing "
                         "from EXEMPT, any unused (no writer, no reader) "
                         "field is missing from EXEMPT_UNUSED, or any table "
                         "entry is no longer in that state. Used by the "
                         "WriteOnlyFields ctest test.")
    args = ap.parse_args()
    root = os.path.abspath(args.repo)

    members = collect_members(root)
    names = {nm for (_t, nm) in members}
    sys.stderr.write("declared members: %d (%d distinct names) in %s\n"
                     % (len(members), len(names), ", ".join(DECL_DIRS)))
    containers = build_container_registry(root)
    hits = sweep(root, names, containers)

    # B840: for each member name, the declaration lines of EVERY type that
    # has a member of that name (a same-named member of another type is not
    # a use of this one) and, per other type, the occurrences the sweep
    # attributed to it.
    name_decl_lines = defaultdict(set)
    for (t2, nm2), (rel2, ln2) in members.items():
        name_decl_lines[nm2].add((rel2, ln2))
    attributed_to = defaultdict(lambda: defaultdict(set))
    for (t2, nm2) in members:
        h2 = hits.get(nm2)
        if not h2:
            continue
        for b in ("w", "r"):
            typed = h2.get("t" + b, {})
            if t2 in typed:
                attributed_to[nm2][t2] |= set(typed[t2])

    write_only, unused = [], []
    for (tname, nm), (rel, lineno) in sorted(members.items()):
        h = hits.get(nm, {"w": [], "r": [], "tw": {}, "tr": {}})
        # the declaration itself, if it carried an initialiser, is not a use
        w = [p for p in h["w"] if not (p[0] == rel and p[1] == lineno)]
        r = [p for p in h["r"] if not (p[0] == rel and p[1] == lineno)]
        # (p[:2] comparisons above; the write-only report still prints
        # file:line from the triple.)
        if not r and len(w) >= args.min_writes:
            write_only.append((tname, nm, rel, lineno, w))
        # B840: the unused half classifies on the TYPED occurrences. An
        # occurrence counts as a use of (tname, nm) unless the sweep
        # attributed it, at the same file:line, to a DIFFERENT type that
        # also has a member of this name (prepData.decodedTextures at
        # entity_prep_worker.cpp:462 is a use of EquipmentPrepData::
        # decodedTextures, not of RaceModelData::decodedTextures). An
        # occurrence the sweep could not type still counts for every type
        # (the pre-B840 floor), so this can only remove hiding, never invent
        # a false unused.
        other = {o for o in attributed_to[nm] if o != tname}
        attributed_elsewhere = set()
        for o in other:
            attributed_elsewhere |= attributed_to[nm][o]
        effective = [p for p in (w + r)
                     if (p[0], p[1]) not in name_decl_lines[nm]
                     and p not in attributed_elsewhere]
        if not effective:
            unused.append((tname, nm, rel, lineno))

    if args.check:
        found = {(t, n) for t, n, _r, _l, _w in write_only}
        unlisted = sorted(found - set(EXEMPT))
        stale = sorted(set(EXEMPT) - found)
        for t, n in unlisted:
            rel, ln = members[(t, n)]
            print("NEW write-only field: %s::%s   %s:%d" % (t, n, rel, ln))
        for t, n in stale:
            print("STALE exemption (no longer write-only): %s::%s -- %s"
                  % (t, n, EXEMPT[(t, n)][1]))

        # B840: the never-touched half, same ratchet. A field with no writer
        # AND no reader is the same reachability defect as an uncalled
        # function, and this is the half the write-only mode was blind to
        # (RaceModelData::decodedTextures: zero occurrences, invisible to
        # "written and never read").
        found_unused = {(t, n) for t, n, _r, _l in unused}
        unlisted_unused = sorted(found_unused - set(EXEMPT_UNUSED))
        stale_unused = sorted(set(EXEMPT_UNUSED) - found_unused)
        for t, n in unlisted_unused:
            rel, ln = members[(t, n)]
            print("NEW unused field (no writer, no reader): %s::%s   %s:%d"
                  % (t, n, rel, ln))
        for t, n in stale_unused:
            print("STALE unused exemption (no longer unused): %s::%s -- %s"
                  % (t, n, EXEMPT_UNUSED[(t, n)][1]))

        if unlisted or stale:
            print("")
            print("%d unlisted, %d stale." % (len(unlisted), len(stale)))
            if unlisted:
                print("A field written and never read is the same reachability defect "
                      "class as an uncalled function (B290, T404's MixChannel::sampleRate)."
                      "\nWire it, delete it, or add it to EXEMPT in this script with a "
                      "category and a reason.")
            if stale:
                print("An exemption whose field is no longer write-only must be removed "
                      "from EXEMPT -- the table is not allowed to outlive its rows.")
        if unlisted_unused or stale_unused:
            print("")
            print("%d unlisted unused, %d stale unused."
                  % (len(unlisted_unused), len(stale_unused)))
            if unlisted_unused:
                print("A field with no writer and no reader is the same reachability "
                      "defect class as an uncalled function (B840, RaceModelData::"
                      "decodedTextures).\nDelete it, or add it to EXEMPT_UNUSED in this "
                      "script with a category and a reason.")
            if stale_unused:
                print("An exemption whose field is no longer unused must be removed "
                      "from EXEMPT_UNUSED -- the table is not allowed to outlive its "
                      "rows.")
        if unlisted or stale or unlisted_unused or stale_unused:
            return 1
        print("write-only census clean: %d fields, all %d classified in EXEMPT."
              % (len(found), len(EXEMPT)))
        print("unused census clean: %d fields, all %d classified in EXEMPT_UNUSED."
              % (len(found_unused), len(EXEMPT_UNUSED)))
        return 0

    print("=" * 78)
    print("WRITE-ONLY FIELDS (written somewhere, read nowhere)")
    print("=" * 78)
    for tname, nm, rel, lineno, w in write_only:
        flag = "  [common name -- read count unreliable]" if nm in NOISE else ""
        print("\n%s::%s   %s:%d%s" % (tname, nm, rel, lineno, flag))
        for pos in w[:12]:
            print("    W  %s:%d" % (pos[0], pos[1]))
        if len(w) > 12:
            print("    ... %d more writes" % (len(w) - 12))
    print("\n%d write-only fields." % len(write_only))

    if args.include_unused:
        print("\n" + "=" * 78)
        print("FIELDS WITH NO OCCURRENCE OUTSIDE THEIR DECLARATION")
        print("=" * 78)
        for tname, nm, rel, lineno in unused:
            print("%s::%s   %s:%d" % (tname, nm, rel, lineno))
        print("\n%d unused fields." % len(unused))

    if args.json:
        import json
        with open(args.json, "w") as f:
            json.dump({"write_only": [
                {"type": t, "field": n, "file": rel, "line": ln,
                 "writes": [{"file": a, "line": b} for a, b, _c in w]}
                for t, n, rel, ln, w in write_only],
                "unused": [{"type": t, "field": n, "file": rel, "line": ln}
                           for t, n, rel, ln in unused]}, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
