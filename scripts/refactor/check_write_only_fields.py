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
     occurrence at all outside their own declaration.

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
    ("TradeskillContainerClosedEvent", "wasWorldObject"):
        ("boarded", "B321: T415 Leg A drew the window and either close event hides it, so the world-vs-inventory provenance of the CLOSE is still unread (T445 fixed the board id here: this row cited B336, which is the detail view distance and has nothing to do with containers)"),
    # T421 closed B298's item half: the fourteen fields that HAVE an oracle
    # line are drawn by the item display window, and their exemptions are
    # gone. These nine are what is left, and they are a DIFFERENT defect.
    ("ItemInstance", "currentSlot"):
        ("boarded", "B298 (item half CLOSED by T421): the oracle has no element for it. EQUI_ItemDisplay.xml is a title, an icon and one STMLbox; `Slot:` (eqstr 3000) is the EQUIPPABLE list, drawn from `slots`. Nothing shows where an item currently sits."),
    # T425 (B298 spell half): the spell display window EXISTS now
    # (BuildSpellDisplayText, drawn in the oracle's one ItemDisplayWindow).
    # These four have no line in it because the official client's string
    # table has no format string for them -- measured by grepping
    # /mnt/projects/EverQuestP1999/eqstr_us.txt, not assumed. Same shape as
    # ItemInstance::currentSlot's row: the oracle does not show it.
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
    ("MemoryStats", "loadedModelCount"):
        ("report", "B337: /pmem aggregates a loader fills and the report never prints"),
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
    ("ConstrainedTextureCache", "bgThreadPool_"):
        ("boarded", "B338: setter supplies a real pool, no async decode reads it"),
    ("ConstrainedTextureCache", "compressedTexturesAvailable_"):
        ("boarded", "B338: four writes probe ETC1 support, nothing consults the answer"),
    ("WeatherEffectsController", "targetIntensity_"):
        ("boarded", "B339: the smooth weather transition it was for does not exist"),
    ("ZoneLifecycleComponent", "entitiesLoaded_"):
        ("boarded", "B340: L09 deleted the zero-caller accessor and left the member"),
    ("ZoneLifecycleComponent", "expectedEntityCount_"):
        ("boarded", "B340: L09 deleted the zero-caller accessor and left the member"),
    ("OverlaysComponent", "currentTargetInfo_"):
        ("boarded", "B341: target-overlay state cached per frame and never drawn from"),
    ("OverlaysComponent", "currentTargetLevel_"):
        ("boarded", "B341: target-overlay state cached per frame and never drawn from"),
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
    ("BridgeComponent", "m_audio_config_vendor_music"):
        ("boarded", "B344: the vendor-music config reaches no audio decision"),
    ("PlayerComponent", "m_duel_target_id"):
        ("boarded", "B344: /duel stores the target id; the reply path never consults it"),
    ("SkillManager", "m_player_race"):
        ("boarded", "B344: race stored for skill gating that does not gate on race"),
    ("CameraController", "orbitAngleInitialized_"):
        ("boarded", "B345: orbit-camera latch with no reader"),
    ("ConsoleRenderer", "m_curEnd"):
        ("boarded", "B345: headless console renderer state with no reader"),
    ("ConsoleRenderer", "m_playerHeading"):
        ("boarded", "B345: headless console renderer state with no reader"),
    ("ConsoleRenderer", "m_startTime"):
        ("boarded", "B345: headless console renderer state with no reader"),
    ("ConflictInfo", "binding1"):
        ("boarded", "B345: hotkey conflict report fields nothing formats"),
    ("ConflictInfo", "binding2"):
        ("boarded", "B345: hotkey conflict report fields nothing formats"),
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



ASSIGN = re.compile(r"^\s*(?:\+|-|\*|/|%|\||&|\^|<<|>>)?=(?!=)")
CTOR_INIT_PREFIX = re.compile(r"[:,]\s*$")


def sweep(root, names):
    """name -> {'w': [(rel, lineno)], 'r': [(rel, lineno)]}"""
    hits = defaultdict(lambda: {"w": [], "r": []})
    pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names),
                                              key=len, reverse=True)) + r")\b")
    for path in walk(root, SWEEP_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        for lineno, line in enumerate(src.split("\n"), 1):
            for m in pat.finditer(line):
                nm = m.group(1)
                before = line[:m.start()]
                after = line[m.end():]
                is_write = False
                if ASSIGN.match(after):
                    is_write = True
                elif (after[:1] in ("(", "{")
                      and CTOR_INIT_PREFIX.search(before)):
                    is_write = True
                hits[nm]["w" if is_write else "r"].append((rel, lineno))
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
                         "from EXEMPT, or if any EXEMPT entry is no longer "
                         "write-only. Used by the WriteOnlyFields ctest test.")
    args = ap.parse_args()
    root = os.path.abspath(args.repo)

    members = collect_members(root)
    names = {nm for (_t, nm) in members}
    sys.stderr.write("declared members: %d (%d distinct names) in %s\n"
                     % (len(members), len(names), ", ".join(DECL_DIRS)))
    hits = sweep(root, names)

    write_only, unused = [], []
    for (tname, nm), (rel, lineno) in sorted(members.items()):
        h = hits.get(nm, {"w": [], "r": []})
        # the declaration itself, if it carried an initialiser, is not a use
        w = [p for p in h["w"] if not (p[0] == rel and p[1] == lineno)]
        r = [p for p in h["r"] if not (p[0] == rel and p[1] == lineno)]
        if not r and len(w) >= args.min_writes:
            write_only.append((tname, nm, rel, lineno, w))
        elif not r and not w:
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
            return 1
        print("write-only census clean: %d fields, all %d classified in EXEMPT."
              % (len(found), len(EXEMPT)))
        return 0

    print("=" * 78)
    print("WRITE-ONLY FIELDS (written somewhere, read nowhere)")
    print("=" * 78)
    for tname, nm, rel, lineno, w in write_only:
        flag = "  [common name -- read count unreliable]" if nm in NOISE else ""
        print("\n%s::%s   %s:%d%s" % (tname, nm, rel, lineno, flag))
        for p, l in w[:12]:
            print("    W  %s:%d" % (p, l))
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
                 "writes": [{"file": a, "line": b} for a, b in w]}
                for t, n, rel, ln, w in write_only],
                "unused": [{"type": t, "field": n, "file": rel, "line": ln}
                           for t, n, rel, ln in unused]}, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
