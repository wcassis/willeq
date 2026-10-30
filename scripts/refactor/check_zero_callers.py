#!/usr/bin/env python3
"""
T455 -- the zero-caller census, taken over the whole tree as a script.

Why this exists: the dominant defect class on this project is
implemented-but-unreached.  T366, T414, T429, T442, T448 and T452 each found
more of it BY HAND, one identifier at a time, and every one of those censuses
died with the unit that took it.  `check_write_only_fields.py` made the FIELD
half of the defect class a ratchet (a field written and never read); this is
the FUNCTION half (a function declared and never called).

What it does:
  1. Parses every file under include/ and src/ and collects
       * every free function declared at namespace scope, and
       * every PUBLIC or PROTECTED member function of a class/struct/union,
     recording the declaring file, line, owner type and access.
  2. Sweeps src/ include/ tests/ tools/ benchmarks/ docker/ for every
     occurrence of those names -- comment- and string-stripped -- and
     classifies each occurrence as a DECLARATION/DEFINITION site or a USE.
     An occurrence NOT followed by `(` counts as a use, which is how a
     function reached only through a dispatch table, a command registry or a
     member-function pointer is correctly seen as REACHED (board B166's rule,
     generalised).
  3. Reports the functions with zero uses, split into buckets:
       zero      no use anywhere in the six directories
       tests     used only from tests/
       tools     used only from tools/ or benchmarks/  (board B166/B161:
                 `tools/model_viewer.cpp` was the one caller of
                 setSpellDatabase(), and T380 boarded it as "zero callers
                 tree-wide" because its census skipped tools/)

Directories swept: src/ include/ tests/ tools/ benchmarks/ docker/.  A
"zero callers" claim that does not include tools/, benchmarks/ and docker/ is
not tree-wide -- board B166, class P.

Accuracy note -- deliberately asymmetric, in the SAFE direction.  Names are
matched by IDENTIFIER, not by receiver type, so a name shared with another
class collects that class's occurrences too.  Every such collision can only
ADD uses, so it can only HIDE a dead function, never invent one.  The report
is a FLOOR: everything it names is worth looking at, and it misses some.  A
green run is not a proof that the tree has no dead functions.  The field-name
half of that blind spot is real (B1028): EntityManager::isPet(uint16_t) sat
zero-caller while the Entity::isPet FIELD kept lighting the name up, so the
census saw uses that were never calls of the method -- a member and a field
sharing a name defeat the identifier sweep entirely.

Two further limits, stated so a green is not over-read:
  * A caller count is not reachability.  T442/B451: checkCollisionIrrlicht()
    had FIVE call sites and was still unreachable, because every one was the
    dead arm of a ternary whose selector was always true.
  * "Nothing calls it" and "nothing CAN call it because the route is broken"
    look identical from a grep, and this script sees neither.  T452/B474:
    getArmorTexture() had live callers and returned nullptr for six months.

Exclusion rules (by rule, never by hand -- T455 plan step 1):
  * constructors, destructors and operators -- reached by construction,
    destruction and expression syntax, not by name;
  * `main` and the tool entry points;
  * anything virtual: a declaration carrying `virtual`, `override`, `final`
    or `= 0`, AND any name declared `virtual` anywhere in the tree, since an
    override reached through a base pointer never names the override;
  * private members (the plan scopes the census to public/protected);
  * standard-library customisation points (an allocator's allocate/deallocate,
    a container's begin/end/swap): the library calls them and no source line
    names them, so no caller census can see the call;
  * out-of-line definitions (`Owner::name`) are not re-collected as
    declarations, and neither definitions nor re-declarations count as uses.

The ratchet (--check) has two halves since T458: the zero bucket against
EXEMPT, and the tests bucket against EXEMPT_TESTS (seam / observer / a
boarded B6xx row -- see the comment above TESTS_CATEGORY_REASONS).  A row
that changes bucket fails BOTH ways: NEW in the table it now belongs to and
STALE in the one it left.  Deleting dead code exposes more (board B503): after
each deletion round re-run --bucket all until it stops producing rows.

Usage:
    python3 scripts/refactor/check_zero_callers.py [--repo ROOT]
                [--bucket zero|tests|tools|all] [--json OUT]
                [--check [--tests-only]]
"""

import argparse
import os
import re
import sys
from collections import defaultdict

DECL_DIRS = ("include", "src")
SWEEP_DIRS = ("src", "include", "tests", "tools", "benchmarks", "docker")
EXTS = (".h", ".hpp", ".cpp", ".cc", ".inl")

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRINGS = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'")
TOKEN = re.compile(r"[A-Za-z_]\w*")

TYPE_OPEN = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?"
                       r"(struct|class|union)\s+(?:\w+_API\s+)?(\w+)\b")
NS_OPEN = re.compile(r'^\s*(?:namespace\b|extern\s+"C")')
ACCESS = re.compile(r"^\s*(public|private|protected)\s*:")

# Statement keywords that can precede a `(` and would otherwise look like a
# return type.  `return foo(1);` must be a USE, not a definition.
STMT_KEYWORDS = {
    "return", "case", "else", "do", "try", "throw", "new", "delete",
    "co_return", "co_await", "co_yield", "and", "or", "not", "sizeof",
    "typeid", "alignof", "noexcept", "decltype", "static_cast",
    "dynamic_cast", "const_cast", "reinterpret_cast", "goto", "using",
    "typedef", "friend", "template", "explicit", "operator",
}
# Keywords that are never a function name.
NON_NAMES = {
    "if", "for", "while", "switch", "return", "catch", "sizeof", "throw",
    "new", "delete", "case", "do", "else", "and", "or", "not", "alignof",
    "decltype", "noexcept", "static_assert", "typeid", "template",
    "explicit", "constexpr", "consteval", "operator", "typename", "using",
    "namespace", "struct", "class", "union", "enum", "public", "private",
    "protected", "friend", "typedef", "static_cast", "dynamic_cast",
    "const_cast", "reinterpret_cast", "co_await", "co_return", "co_yield",
    "defined", "assert", "EXPECT_TRUE", "ASSERT_TRUE",
}
SKIP_LINE_PREFIX = ("#", "template", "friend", "using", "typedef", "return",
                    "case", "static_assert", "enum", "namespace", "extern",
                    "public:", "private:", "protected:", "//", "*")

# Entry points: called by the runtime/loader, never by name from the tree.
ENTRY_POINTS = {"main"}

# Standard-library customisation points.  The library calls these; no line in
# the tree ever names them, so a caller census cannot see the call.
# `PageAlignedAllocator::allocate` / `::deallocate` (the `ksm.h` header,
# deleted by B941 with its last user) were the tree's example -- every
# `std::vector<T, PageAlignedAllocator<T>>` reached them and not one source
# line wrote the name.
STDLIB_CUSTOMIZATION = {
    "allocate", "deallocate", "construct", "destroy", "max_size",
    "begin", "end", "cbegin", "cend", "rbegin", "rend", "swap", "hash",
}

TYPEISH = re.compile(r"^[\w:<>,&*\s\[\]]+$")


def strip_noncode(text):
    """Blank out block comments, line comments and string/char literals,
    preserving line numbering."""
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRINGS.sub('""', text)
    return text


def walk(root, dirs):
    for d in dirs:
        base = os.path.join(root, d)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames
                           if x not in ("third_party", "build", ".git")
                           and not x.startswith("build")]
            for fn in sorted(filenames):
                if fn.endswith(EXTS):
                    yield os.path.join(dirpath, fn)


def split_call(code, pos_of_paren):
    """Given a line and the index of a `(`, return (head, name, prefix) where
    `name` is the identifier immediately before the paren."""
    head = code[:pos_of_paren]
    m = re.search(r"(~?\w+)\s*$", head)
    if not m:
        return None
    return head[:m.start()], m.group(1)


def last_word(s):
    m = re.search(r"(\w+)\W*$", s)
    return m.group(1) if m else ""


# TYPEISH already rejects `(`, `=`, `.`, `-`, `!`, `?`, `#`, `"`, `{`, `;`,
# so a lead that survives it is already type-shaped.  These are the
# expression forms it still lets through: a LONE colon (a ternary arm, a
# constructor initialiser list, a label, a range-for), `&&`, and a stream
# or shift operator.  T458: `outFile << SerializeUiPreferences(p);`
# (src/client/graphics/ui/ui_preferences.cpp) scored as a DEFINITION, because
# `<`, `>` and spaces are all type characters, and so the census reported a
# live function as reached only from tests.  The operator must be
# WHITESPACE-BOUNDED: a bare `>>` also closes a nested template
# (`std::vector<std::pair<int16_t, const ItemInstance*>> getBankItems(`), and
# the first cut of this rule scored four such DEFINITIONS as uses, hiding four
# genuinely dead functions (InventoryManager::getBankItems/getSharedBankItems/
# getSellableItems, SkyLoader::getCelestialBodiesForSkyType).
LONE_COLON = re.compile(r"(?<!:):(?!:)")
BAD_IN_LEAD = ("&&",)
STREAM_OP = re.compile(r"(?:^|\s)(?:<<|>>)(?:\s|$)")
# T486: a lead ending in `,` or in a whitespace-bounded `&` / `*` / `<` is a
# continuation line's binary operator, and what follows is a USE.
CONTINUATION_OP = re.compile(r"(?:,\s*|\s[&*<]\s+)$")


def classify_occurrence(code, start, end):
    """DECL/DEF site, or a USE?  `code` is the comment/string-stripped line,
    [start,end) the identifier.  Returns True when the occurrence is a
    declaration or definition of the function rather than a use of it.

    The discriminator is the LEAD: the text before the whole qualified-id the
    identifier terminates.  `eqt::util::StringFormat("x");` and
    `const std::string eqt::util::StringFormat(const char*, ...)` differ only
    in whether anything precedes the qualifier chain, and reading the second
    form's rule onto the first would hide a real call -- which is the one
    direction this census must never get wrong, since a hidden call invents a
    dead function.  Everything ambiguous is therefore resolved to USE.
    """
    after = code[end:]
    if not after.lstrip().startswith("("):
        # A bare mention: a dispatch-table entry, a member-function pointer,
        # a std::bind/std::function target.  That IS reaching the function.
        return False
    # Walk back over the qualified-id (`A::B<T>::name`) to find the lead.
    i = start
    while i > 0 and (code[i - 1].isalnum() or code[i - 1] in "_:<>"):
        i -= 1
    lead = code[:i]
    if not lead.strip():
        # `foo(...)` / `ns::foo(...)` at statement start -- a call.  A
        # definition whose return type sits on the PREVIOUS line lands here
        # too and is scored as a use; that hides a dead function rather than
        # inventing one, which is the safe direction.
        return False
    if last_word(lead) in STMT_KEYWORDS:
        return False
    for bad in BAD_IN_LEAD:
        if bad in lead:
            return False
    if LONE_COLON.search(lead):
        return False
    if STREAM_OP.search(lead):
        return False
    if not re.search(r"[A-Za-z_]", lead):
        # A definition's lead always names a type.  A lead of punctuation
        # only -- `    , foo(x)` continuing an argument list, `    * f(y)`
        # continuing an expression -- is a call (T458).
        return False
    if CONTINUATION_OP.search(lead):
        # T486: a lead that ENDS in a binary operator is an expression, not a
        # type -- `        value, foo(x)` continuing an argument list,
        # `        a & foo(x)` / `        a * foo(x)` / `        a < foo(x)`
        # continuing a condition on a wrapped line.  TYPEISH let all four
        # through because `,`, `&`, `*`, `<` and spaces are all type
        # characters (`std::map<int, Foo>`, `Foo&`, `Foo*`).  A definition's
        # name never follows a comma; and the reference / pointer / template
        # forms are whitespace-bounded the same way STREAM_OP is (`Foo& bar(`
        # and `Foo &bar(` keep their definition score, `a & foo(` does not).
        return False
    return TYPEISH.match(lead) is not None


# ---------------------------------------------------------------------------
# Self-test of the classifier (T486).  --self-test runs every probe below
# through classify_occurrence() and exits 1 on the first disagreement.  Each
# probe is a line shape the census has scored wrongly at some point, or the
# definition shape that a fix for one of those must NOT hide: a call scored as
# a definition invents a dead function (the direction this census must never
# get wrong -- the next hygiene unit deletes it), a definition scored as a
# call hides a dead one (safe, but it is what T458's first cut of STREAM_OP
# did four times over).  Wired as the ZeroCallersSelfTest ctest.
# ---------------------------------------------------------------------------

# (line, identifier, expected: True = declaration/definition, False = use)
CLASSIFY_PROBES = [
    # Plain call and qualified call at statement start.
    ("    foo(x);", "foo", False),
    ("    ns::foo(x);", "foo", False),
    # Definitions: a type-shaped lead.
    ("void foo(int x) {", "foo", True),
    ("const std::string eqt::util::StringFormat(const char* fmt, ...)", "StringFormat", True),
    ("std::vector<std::pair<int16_t, const ItemInstance*>> getBankItems(", "getBankItems", True),
    ("std::map<int, Foo> lookup(int key);", "lookup", True),
    ("Foo& bar(int x);", "bar", True),
    ("Foo &bar(int x);", "bar", True),
    ("Foo* bar(int x);", "bar", True),
    ("Foo *bar(int x);", "bar", True),
    # T458: a stream operator is an expression.
    ("    outFile << SerializeUiPreferences(p);", "SerializeUiPreferences", False),
    # T458: punctuation-only leads continue an expression.
    ("    , foo(x)", "foo", False),
    ("    * f(y)", "f", False),
    # Statement keywords and expression characters.
    ("    return foo(x);", "foo", False),
    ("    x = foo(y);", "foo", False),
    ("    if (a && foo(y))", "foo", False),
    ("    cond ? foo(x) : bar(y);", "foo", False),
    # T486: an identifier followed by a binary operator on a continuation
    # line -- the four shapes that scored as DEFINITIONS.
    ("        value, foo(x)", "foo", False),
    ("        a & foo(x)", "foo", False),
    ("        a * foo(x)", "foo", False),
    ("        a < foo(x)", "foo", False),
    # A bare mention (no paren) is a use: a dispatch table, a bound member.
    ("    {HC_OP_Foo, foo},", "foo", False),
]


def self_test():
    failed = 0
    for line, name, expected in CLASSIFY_PROBES:
        code = strip_noncode(line)
        hits = [m for m in TOKEN.finditer(code) if m.group(0) == name]
        if not hits:
            print("PROBE BROKEN (identifier not found): %r" % line)
            failed += 1
            continue
        m = hits[-1]
        got = classify_occurrence(code, m.start(), m.end())
        if got != expected:
            print("FAIL %-60r %s scored %s, expected %s"
                  % (line, name, "DEF" if got else "USE",
                     "DEF" if expected else "USE"))
            failed += 1
    print("classify_occurrence self-test: %d probes, %d failed"
          % (len(CLASSIFY_PROBES), failed))
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# Declaration collection
# ---------------------------------------------------------------------------

class Decl(object):
    __slots__ = ("owner", "name", "rel", "line", "access", "virtual")

    def __init__(self, owner, name, rel, line, access, virtual):
        self.owner, self.name = owner, name
        self.rel, self.line = rel, line
        self.access, self.virtual = access, virtual


def collect_functions(root):
    decls = {}                      # (owner, name) -> Decl
    virtual_names = set()
    for path in walk(root, DECL_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        stack = []                  # [{'kind','name','access'}]
        for lineno, raw in enumerate(src.split("\n"), 1):
            code = raw
            stripped = code.strip()

            a = ACCESS.match(code)
            if a and stack and stack[-1]["kind"] == "class":
                stack[-1]["access"] = a.group(1)

            pending = "other"
            tm = TYPE_OPEN.match(code)
            if NS_OPEN.match(code):
                pending = "ns"
            elif tm and not stripped.endswith(";") and "(" not in code:
                pending = ("class", tm.group(2),
                           "private" if tm.group(1) == "class" else "public")

            # --- is this line a function declaration at a scope we collect?
            if (stripped and "(" in code
                    and not stripped.startswith(SKIP_LINE_PREFIX)):
                in_class = bool(stack) and stack[-1]["kind"] == "class"
                at_ns = all(f["kind"] == "ns" for f in stack)
                if in_class or at_ns:
                    sc = split_call(code, code.index("("))
                    if sc:
                        prefix, name = sc
                        if (name not in NON_NAMES
                                and not name.startswith("~")
                                and "::" not in prefix.rstrip()[-2:]
                                and TYPEISH.match(prefix or " ")):
                            pb = prefix.rstrip()
                            ok = bool(pb) and last_word(pb) not in STMT_KEYWORDS
                            if ok:
                                owner = (stack[-1]["name"] if in_class
                                         else "(free)")
                                if name != owner:      # constructor
                                    acc = (stack[-1]["access"] if in_class
                                           else "public")
                                    virt = bool(
                                        re.search(r"\bvirtual\b", code)
                                        or re.search(r"\boverride\b", code)
                                        or re.search(r"\bfinal\b", code)
                                        or re.search(r"=\s*0\s*;", code))
                                    if virt:
                                        virtual_names.add(name)
                                    decls.setdefault(
                                        (owner, name),
                                        Decl(owner, name, rel, lineno, acc,
                                             virt))

            # --- brace bookkeeping
            first = True
            for ch in code:
                if ch == "{":
                    if first and isinstance(pending, tuple):
                        stack.append({"kind": "class", "name": pending[1],
                                      "access": pending[2]})
                    elif first and pending == "ns":
                        stack.append({"kind": "ns", "name": "", "access": ""})
                    else:
                        stack.append({"kind": "other", "name": "",
                                      "access": ""})
                    first = False
                elif ch == "}":
                    if stack:
                        stack.pop()
    return decls, virtual_names


# ---------------------------------------------------------------------------
# The EXEMPT table.  Same contract as check_write_only_fields.py's: every row
# the census reports must be listed here with a CATEGORY and a REASON, and a
# listed row the census NO LONGER reports also fails -- so an exemption cannot
# outlive the thing it excuses.
#
# Categories:
#   boarded  a real unreached feature with an open board entry that owns it.
#            The reason names the entry; do not re-board.
#   kept     deliberately kept zero-caller code; the reason says why deleting
#            it would destroy evidence (L03 correction 5 / B045).
#   tools    reached only from tools/ or benchmarks/ -- a developer-facing
#            entry point, not dead (B166/B161).
#   tests    reached only from tests/ and deliberately so.
#   api      a boundary/vocabulary function whose value is the boundary.
# ---------------------------------------------------------------------------


# Board entries owning the zero-caller groups this census reports.  Every
# EXEMPT row below names one of these; the reason lives here once instead of
# being repeated 417 times.
BOARD_REASONS = {
    "B1128":
        "the spell projectile (B165: 85 spells carry TravelType == 3): "
        "SpellVisualFX::createProjectile, reached only from a test until "
        "GitLab B1128 wires it into the client's cast sequence",
    "B490":
        "the environment/particle subsystem's query and control API -- "
        "emitter, flock, boids, overlay and storm-cloud accessors and setters "
        "that no renderer pass, no config load and no slash command reaches",
    "B491":
        "the S3D/WLD asset-loader query API -- loader accessors, "
        "skeletal-animator playback and frame-trigger controls, and the "
        "equipment/race texture helpers, none of which the load path calls",
    "B492":
        "the detail/foliage subsystem's query and control API -- season "
        "override, footprint config, disturbance sources, surface-map bounds, "
        "wind magnitude -- with no caller",
    "B493":
        "the paperdoll's (CharacterModelView) mouse and rotation API plus the "
        "output-renderer factory: the panel is drawn, but never driven "
        "through these",
    "B494":
        "renderer component and helper API with no caller -- "
        "entity_renderer's debug offset/rotation controls and per-entity "
        "animation queries, the camera/zone-shader/texture-atlas/sky/weather "
        "accessors, and the renderer-facade residue T448's ten deletions did "
        "not reach",
    "B495":
        "InventoryManager's bank, cursor-queue, swap/destroy and "
        "callback-registration API plus the unreached inventory_constants "
        "slot helpers -- the bank and inventory windows read GameState, not "
        "the manager",
    "B496":
        "GameState module setters and getters with no caller -- the seven "
        "stat setters, bind heading, zone-load progress/status, and the "
        "trade/spell/group fields nothing publishes or reads",
    "B497":
        "top-level manager API with no caller -- Application's four component "
        "accessors, CombatManager's spell-set save/load and ally scan, "
        "TradeManager's slot API, DoorStateManager's radius query, and the "
        "zone-line/database/string helpers",
    "B498":
        "the input-injection API (NullInputHandler and GraphicsInputHandler "
        "inject*): the automation surface T410's scenario lane does not use, "
        "because ScenarioRunner drives EverQuest through IScenarioWorld "
        "instead",
}

BOARD_REASONS.update({
    "B600":
        "the audio sound-category layer that is never constructed -- CombatMusicManager (in the client target, referenced by nothing outside its own two files), and CreatureSounds, UISounds, PlayerSounds and WeatherAudio (whose .cpp files are in no client target at all; only their tests link them) -- plus DoorSounds' unused static lookups and AudioManager::preloadSound",
    "B601":
        "GameState module mutators with no production writer -- the target/pet HP updaters, the group member updaters, three InventoryState setters, PlayerState::loadFromProfile/setCurHP/setPositionAndHeading, the SpellState memorize/scribe mutators, WorldState::setZoneId/setZoning and GameState::resetForZoneChange",
    "B899":
        "B442's published interface for the bag (container) window, built by the concurrent unit B899 -- StaticUIInput::cursorGesture() is how that window's slot arm reaches the shared gesture; it has no caller until that branch merges",
    "B603":
        "gameplay-manager control API with no production caller -- CombatManager's auto-loot/aggro/flee/auto-fire configuration, SpellManager's cast-by-id, gem swap and two callback setters, BuffManager's removals and ActiveBuff's formatting, the group-decline and move-speed mutators, ZoneLines::checkMovement, WhoAllEntry::isLinkdead; the hotbar-slot mutators and calcBankBagSlotId reclassified seam in B603 (issue #187) -- their tests pin live behaviour",
    "B605":
        "input, command and console API with no production caller -- CommandProcessor's default-channel/echo setters and unregister, HotkeyManager's movement-key and mode-name helpers, ConsoleRenderer's status interval (B606 deleted B605's HotkeyBinding::toString: a dead delegate whose only tests/ callers were the SpellVisualSource toString lines B606 removed)",
    "B899":
        "the T415/T419 recipe window (TradeskillPanelState, EQUI_TradeskillWnd) -- since B899 (#215, Will 2026-09-23: the recipe half is out of baseline) no event opens it, so its close applier is reached only from its tests; deleting the window is a follow-up, not done mid-unit",
    "B972":
        "the particle VBO path's GL-handle accessor -- the draw reads the member itself; the accessor (name_tag_batch's buffer-handle convention) exists for the stream-buffer test suite's observation window, and no production or test caller has landed yet",
})

# (owner, name) -> the board entry that owns it.  Same contract as
# check_write_only_fields.py's EXEMPT: a zero-caller function NOT listed
# here fails --check, and a listed function that HAS a caller again also
# fails, so the table cannot outlive its rows.  All 417 rows are class
# "boarded" -- T455 deleted the residue it could prove dead outright
# (common/util/strings, common/util/file, three position.h overload sets)
# and boarded the rest rather than wiring a feature mid-unit (the T39/B024
# rule).
EXEMPT_GROUPS = {
    ("StaticUIInput", "cursorGesture"): "B899",
    ("(free)", "resistToParticleColor"): "B490",
    ("BoidsManager", "getCurrentBiome"): "B490",
    ("BoidsManager", "getQuality"): "B490",
    ("BoidsManager", "getTotalActiveCreatures"): "B490",
    ("EnvironmentState", "isFoggy"): "B490",
    ("FlockController", "getCreatures"): "B490",
    ("FlockController", "getDestination"): "B490",
    ("FlockController", "getTimeAlive"): "B490",
    ("FlockController", "hasExitedBounds"): "B490",
    ("FlockController", "scatter"): "B490",
    ("FlockController", "setCollisionDetection"): "B490",
    ("FlockController", "setDestination"): "B490",
    ("FlockController", "setZoneBounds"): "B490",
    ("ParticleEmitter", "getDensityMultiplier"): "B490",
    ("ParticleEmitter", "getMaxParticles"): "B490",
    ("ParticleManager", "getAmbientColor"): "B490",
    ("ParticleManager", "getAtlasTexture"): "B490",
    ("ParticleManager", "getCurrentBiome"): "B490",
    ("ParticleManager", "getParticleBudget"): "B490",
    ("ParticleManager", "getQuality"): "B490",
    ("ParticleManager", "getSpellSpriteAtlasTexture"): "B490",
    ("ParticleManager", "getTimeOfDay"): "B490",
    ("ParticleManager", "getUnifiedActiveCount"): "B490",
    ("ParticleManager", "getWeather"): "B490",
    ("ParticleManager", "getWeatherLights"): "B490",
    ("ParticleManager", "registerExternalEmitter"): "B490",
    ("ParticleManager", "setWeatherLights"): "B490",
    ("ParticleManager", "setWind"): "B490",
    ("ParticleManager", "unregisterExternalEmitter"): "B490",
    ("RainOverlay", "getSettings"): "B490",
    ("SnowOverlay", "getSettings"): "B490",
    ("StormCloudLayer", "getCurrentOpacity"): "B490",
    ("StormCloudLayer", "getSettings"): "B490",
    ("UnifiedParticleRenderer", "getMaxPointSize"): "B490",
    # B972 (#290): the bounded VBO's GL handle -- the observation window the
    # stream-buffer test uses to verify the point draw's vertices live in the
    # reused VBO (a zero in production, which keeps the handle itself tests-
    # only in the sense the check looks for, not dead: the draw reads it).
    ("UnifiedParticleRenderer", "vertexBuffer"): "B972",
    # B1084 (#404) removed the zero-caller exemptions for isNPCOnlyTexture /
    # isPlayerRaceTexture / getRobeIndex / getRobeClkTextureNumber:
    # test_robe_texture_range now calls them (the measured-range table) and
    # production calls getRobeClkTextureNumber() (mesh_building,
    # entity_prep_worker, renderer_zone_load_entities, equipment_textures);
    # every consumer routes through isRobeTexture(). The three tests-only
    # predicates are classified in EXEMPT_TESTS_GROUPS below.
    ("(free)", "isVeliousTexture"): "B491",
    ("EQAnimatedMeshSceneNode", "getBoneIndexByName"): "B491",
    ("EQAnimatedMeshSceneNode", "getInstanceMeshIndexCount"): "B491",
    # B824 (kanban t_20a88e62): getMappingCount and loadItemModelMappingStatic
    # are DELETED (the item-to-model JSON map went; the lookup is
    # willeq.db-only). An exemption cannot outlive its row, so both rows go
    # with the functions -- ItemModelMapDeletion (check_item_model_map.py)
    # is the ratchet that keeps them gone.
    ("EquipmentModelLoader", "getTextureRef"): "B491",
    ("Placeable", "getFileName"): "B491",
    ("Placeable", "getRotateXRad"): "B491",
    ("Placeable", "getRotateYRad"): "B491",
    ("Placeable", "getRotateZRad"): "B491",
    ("Placeable", "setFileName"): "B491",
    ("S3DLoader", "getGeometry"): "B491",
    ("S3DLoader", "getLightCount"): "B491",
    ("S3DLoader", "getObjectCount"): "B491",
    ("S3DLoader", "getObjects"): "B491",
    ("S3DLoader", "loadZoneFromPreParsed"): "B491",
    ("SkeletalAnimator", "addFootstepTriggers"): "B491",
    ("SkeletalAnimator", "addHitFrameTrigger"): "B491",
    ("SkeletalAnimator", "clearEventCallback"): "B491",
    ("SkeletalAnimator", "clearFrameTriggers"): "B491",
    ("SkeletalAnimator", "clearQueuedAnimation"): "B491",
    ("SkeletalAnimator", "getBlendDuration"): "B491",
    ("SkeletalAnimator", "isBlending"): "B491",
    ("SkeletalAnimator", "isBlendingEnabled"): "B491",
    ("SkeletalAnimator", "isVerboseLogging"): "B491",
    # T485 Leg B: pauseAnimation now has a caller (tools/hwcaps/held_item_candidate_search
    # pins the pose for its 24-candidate sweep), so its B491 exemption is gone --
    # the table is not allowed to outlive its rows. resumeAnimation below is still
    # unreached and stays listed.
    ("SkeletalAnimator", "removeFrameTrigger"): "B491",
    # B1157: resetPlaybackSpeed now has a caller (spell_cast_animation.h's
    # playCastAnim resets the model to speed 1.0 before the cast track), so
    # its B491 exemption is gone -- the table is not allowed to outlive its
    # rows.
    ("SkeletalAnimator", "resumeAnimation"): "B491",
    ("SkeletalAnimator", "setBlendDuration"): "B491",
    ("SkeletalAnimator", "setBlendingEnabled"): "B491",
    ("SkeletalAnimator", "setEventCallback"): "B491",
    ("SkyLoader", "getCelestialBodiesForSkyType"): "B491",
    ("SkyLoader", "getCelestialBodyCount"): "B491",
    ("SkyLoader", "getLayerCount"): "B491",
    ("WldLoader", "getTrackRef"): "B491",
    ("DetailChunk", "applyWindAndDisturbance"): "B492",
    ("DetailChunk", "getBounds"): "B492",
    ("DetailChunk", "getKey"): "B492",
    ("DetailChunk", "hasGeneratedPlacements"): "B492",
    ("DetailConfigLoader", "loadConfigForZone"): "B492",
    ("DetailManager", "adjustDensity"): "B492",
    ("DetailManager", "clearSeasonOverride"): "B492",
    ("DetailManager", "getActiveChunkCount"): "B492",
    ("DetailManager", "getCurrentSeason"): "B492",
    ("DetailManager", "getFootprintConfig"): "B492",
    ("DetailManager", "getTotalPlacementCount"): "B492",
    ("DetailManager", "isFoliageDisturbanceEnabled"): "B492",
    ("DetailManager", "setFootprintConfig"): "B492",
    ("DetailTextureAtlas", "createAtlas"): "B492",
    ("FoliageDisturbanceManager", "addDisturbanceSource"): "B492",
    ("FoliageDisturbanceManager", "clearSources"): "B492",
    ("FoliageDisturbanceManager", "getSourceCount"): "B492",
    ("SeasonalController", "getOverride"): "B492",
    ("SeasonalController", "hasOverride"): "B492",
    ("SurfaceMap", "getMaxX"): "B492",
    ("SurfaceMap", "getMaxY"): "B492",
    ("SurfaceMap", "getMinX"): "B492",
    ("SurfaceMap", "getMinY"): "B492",
    ("WindController", "getWindMagnitude"): "B492",
    ("(free)", "createRenderer"): "B493",
    ("CharacterModelView", "getPrimaryWeaponId"): "B493",
    # B888-CC2 (#204): getRotationY is still zero-caller -- the preview host
    # OWNS its own spin angle and only ever SETS the model to it (never
    # reads), so nothing in production or tests calls the getter.
    ("CharacterModelView", "getRotationY"): "B493",
    ("CharacterModelView", "getSecondaryWeaponId"): "B493",
    ("CharacterModelView", "hasPrimaryWeapon"): "B493",
    ("CharacterModelView", "hasSecondaryWeapon"): "B493",
    ("CharacterModelView", "isDragging"): "B493",
    ("CharacterModelView", "onMouseDown"): "B493",
    ("CharacterModelView", "onMouseMove"): "B493",
    ("CharacterModelView", "onMouseUp"): "B493",
    ("GraphicalRenderer", "getRenderQuality"): "B493",
    ("AnimatedTextureManager", "getAnimatedTextureCount"): "B494",
    ("AnimatedTextureManager", "getCurrentTexture"): "B494",
    # B533 (issue #180) removed the CameraController group's ten rows
    # (2026-10-11): adjustOrbitAngle / getHeadingEQ / getMoveSpeed /
    # getOrbitAngle / getPitch / getRotateSpeed / getYaw / setOrbitAngle /
    # setOrientation / setRotateSpeed are all deleted with the fly/orbit
    # state (refactor/B533/README.md). B494 keeps its other groups.
    ("ConstrainedTextureCache", "freeze"): "B494",
    ("ConstrainedTextureCache", "getAvailableMemory"): "B494",
    ("ConstrainedTextureCache", "getCompressedUploadCount"): "B494",
    ("ConstrainedTextureCache", "isFrozen"): "B494",
    ("ConstrainedTextureCache", "isPending"): "B494",
    ("ConstrainedTextureCache", "setLedgerEnabled"): "B494",
    ("DiagnosticsComponent", "toggleFog"): "B494",
    ("DoorManager", "rebuildPlaceholderDoors"): "B494",
    ("DoorManager", "recomputeAllBspRegions"): "B494",
    ("EntityRenderer", "adjustOffsetX"): "B494",
    ("EntityRenderer", "adjustOffsetY"): "B494",
    ("EntityRenderer", "adjustOffsetZ"): "B494",
    ("EntityRenderer", "adjustRotationX"): "B494",
    ("EntityRenderer", "adjustRotationY"): "B494",
    ("EntityRenderer", "adjustRotationZ"): "B494",
    ("EntityRenderer", "debugLogPlayerVisibility"): "B494",
    ("EntityRenderer", "getDebugTargetId"): "B494",
    ("EntityRenderer", "getEntityAnimation"): "B494",
    ("EntityRenderer", "getEntityAnimationList"): "B494",
    ("EntityRenderer", "getGlobalAnimationSpeed"): "B494",
    ("EntityRenderer", "getOffsetX"): "B494",
    ("EntityRenderer", "getOffsetY"): "B494",
    ("EntityRenderer", "getOffsetZ"): "B494",
    ("EntityRenderer", "getPlayerModelYOffset"): "B494",
    ("EntityRenderer", "getRotationX"): "B494",
    ("EntityRenderer", "getRotationY"): "B494",
    ("EntityRenderer", "getRotationZ"): "B494",
    ("EntityRenderer", "hasEntityAnimation"): "B494",
    ("EntityRenderer", "invalidateOcclusionCache"): "B494",
    ("EntityRenderer", "isEntityCasting"): "B494",
    ("EntityRenderer", "isEntityPlayingThrough"): "B494",
    ("EntityRenderer", "isPlayerInFirstPersonMode"): "B494",
    ("EntityRenderer", "loadNumberedGlobals"): "B494",
    ("EntityRenderer", "resetOffsets"): "B494",
    ("EntityRenderer", "setGlobalAnimationSpeed"): "B494",
    ("EntityRenderer", "setNameTagDistance"): "B494",
    ("EntityRenderer", "setOcclusionCuller"): "B494",
    ("EntityRenderer", "setPlayerEntityVisible"): "B494",
    ("EntityRenderer", "stopEntityAnimation"): "B494",
    ("EntityRenderer", "updateInterpolation"): "B494",
    ("FrameBudgetGovernor", "canStartTask"): "B494",
    ("FrameBudgetGovernor", "hasTimeBudget"): "B494",
    ("GLES2EGLHelper", "uploadETC1AsSharedTexture"): "B494",
    ("PlayerCameraComponent", "getFirstPersonVerticalFov"): "B494",
    # B1063 (#383): RendererContext::Slash left this table -- FrameComponent's
    # deferred F12 arm now calls it (ctx_.Slash().runScreenshot("")), the one
    # body the typed /screenshot shares.
    ("SimulationWorker", "updateObjectLightData"): "B494",
    ("SimulationWorker", "updateTreeData"): "B494",
    ("SimulationWorker", "updateZoneLightData"): "B494",
    ("SkyConfig", "getConfiguredZones"): "B494",
    ("SkyConfig", "getDefaultConfig"): "B494",
    ("SkyConfig", "getZoneCount"): "B494",
    ("SkyRenderer", "beginStripUpload"): "B494",
    ("SkyRenderer", "computeState"): "B494",
    ("SkyRenderer", "continueStripUpload"): "B494",
    ("SkyRenderer", "finalizeStripUpload"): "B494",
    ("SkyRenderer", "getCurrentSkyColors"): "B494",
    ("SkyRenderer", "getCurrentSkyType"): "B494",
    ("SkyRenderer", "isStripActive"): "B494",
    ("SoftwareOcclusionCuller", "computeBufferFillStats"): "B494",
    ("SoftwareOcclusionCuller", "getStatsMutable"): "B494",
    ("SpellVisualFX", "adjustParticleMultiplier"): "B494",
    ("SpellVisualFX", "getParticleMultiplier"): "B494",
    ("SpellVisualFX", "setParticleMultiplier"): "B494",
    ("TextBatch", "addText3D"): "B494",
    ("TextBatch", "addTextW"): "B494",
    ("TextureUploadLedger", "uploads"): "B494",
    ("TreeWindController", "getCurrentStrength"): "B494",
    ("TreeWindController", "getWeather"): "B494",
    ("TreeWindController", "setWindDirection"): "B494",
    ("WeatherConfigLoader", "getQualityPresetName"): "B494",
    ("WeatherEffectsController", "getLightningFlashIntensity"): "B494",
    ("WeatherEffectsController", "getSnowFogSettings"): "B494",
    ("WeatherEffectsController", "isLightningFlashActive"): "B494",
    ("WeatherEffectsController", "isRainOverlayEnabled"): "B494",
    ("WeatherEffectsController", "isSnowOverlayEnabled"): "B494",
    ("WeatherEffectsController", "setLightningCallback"): "B494",
    ("WeatherQualityManager", "getAllPresetNames"): "B494",
    ("WeatherQualityManager", "setPresetByName"): "B494",
    ("WeatherSystem", "getCurrentWeather"): "B494",
    ("WeatherSystem", "getTargetWeather"): "B494",
    ("WeatherSystem", "getWindIntensity"): "B494",
    ("WeatherSystem", "isSimulationEnabled"): "B494",
    ("WeatherSystem", "isTransitioning"): "B494",
    ("WeatherSystem", "removeListener"): "B494",
    ("WeatherSystem", "setSimulationEnabled"): "B494",
    ("WeatherSystem", "transitionToWeather"): "B494",
    ("(free)", "calcSharedBankBagSlotId"): "B495",
    ("(free)", "getBankBagIndexAny"): "B495",
    ("(free)", "getClassAbbrev"): "B495",
    ("(free)", "getParentBankSlotAny"): "B495",
    ("(free)", "getWeaponTypeAbbrev"): "B495",
    ("(free)", "isAnyBankBagSlot"): "B495",
    ("(free)", "isAnyBankSlot"): "B495",
    ("(free)", "isWorldContainerSlot"): "B495",
    ("InventoryManager", "addQuantityToExistingStack"): "B495",
    ("InventoryManager", "clearBankSlots"): "B495",
    ("InventoryManager", "destroyCursorItem"): "B495",
    ("InventoryManager", "destroyItem"): "B495",
    ("InventoryManager", "dumpInventory"): "B495",
    ("InventoryManager", "getBagSize"): "B495",
    ("InventoryManager", "getBankBagSize"): "B495",
    ("InventoryManager", "getBankItems"): "B495",
    ("InventoryManager", "getChangeCounter"): "B495",
    ("InventoryManager", "getPlayerClass"): "B495",
    ("InventoryManager", "getPlayerLevel"): "B495",
    ("InventoryManager", "getPlayerRace"): "B495",
    ("InventoryManager", "getScribeSpellId"): "B495",
    ("InventoryManager", "getSellableItems"): "B495",
    ("InventoryManager", "getSharedBankItems"): "B495",
    ("InventoryManager", "isBankBag"): "B495",
    ("InventoryManager", "isBankBagEmpty"): "B495",
    ("InventoryManager", "isWorldContainerOpen"): "B495",
    ("InventoryManager", "popCursorItem"): "B495",
    ("InventoryManager", "setDeleteItemCallback"): "B495",
    ("InventoryManager", "setEquipmentChangedCallback"): "B495",
    ("InventoryManager", "setMoveItemCallback"): "B495",
    ("InventoryManager", "swapItems"): "B495",
    ("ItemInstance", "countAugmentSlots"): "B495",
    ("ItemInstance", "hasEffects"): "B495",
    ("ItemInstance", "hasResists"): "B495",
    ("ItemInstance", "hasStats"): "B495",
    ("ItemInstance", "isSpellScroll"): "B495",
    ("Entity", "isNPCCorpse"): "B496",
    ("Entity", "isPlayerCorpse"): "B496",
    ("GroupState", "updateMemberStats"): "B496",
    ("InventoryState", "equipmentATK"): "B496",
    ("InventoryState", "equipmentHP"): "B496",
    ("InventoryState", "equipmentMana"): "B496",
    ("PlayerState", "bindHeading"): "B496",
    ("PlayerState", "characterId"): "B496",
    ("PlayerState", "setAGI"): "B496",
    ("PlayerState", "setCHA"): "B496",
    ("PlayerState", "setCharacterId"): "B496",
    ("PlayerState", "setDEX"): "B496",
    ("PlayerState", "setINT"): "B496",
    ("PlayerState", "setMovementSequence"): "B496",
    ("PlayerState", "setSTA"): "B496",
    ("PlayerState", "setSTR"): "B496",
    ("PlayerState", "setVelocity"): "B496",
    ("PlayerState", "setWIS"): "B496",
    ("SpellState", "castingTargetId"): "B496",
    ("SpellState", "memorizingSpellId"): "B496",
    ("SpellState", "setScribedSpellCount"): "B496",
    ("TradeState", "partnerItemId"): "B496",
    ("TradeState", "partnerItemName"): "B496",
    ("WorldState", "setWeatherIntensity"): "B496",
    ("WorldState", "setZoneLoadProgress"): "B496",
    ("WorldState", "setZoneLoading"): "B496",
    ("WorldState", "setZoneName"): "B496",
    ("WorldState", "zoneLoadProgress"): "B496",
    ("WorldState", "zoneLoadStatus"): "B496",
    ("(free)", "GetChatTypeName"): "B497",
    ("(free)", "getCombatSkillAnimation"): "B497",
    ("Application", "getActionDispatcher"): "B497",
    ("Application", "getCommandProcessor"): "B497",
    ("Application", "getEverQuestClient"): "B497",
    ("CombatManager", "GetMemorizedSpells"): "B497",
    ("CombatManager", "GetNearbyAllies"): "B497",
    ("CombatManager", "LoadSpellSet"): "B497",
    ("CombatManager", "SaveSpellSet"): "B497",
    ("DatabaseStats", "avgQueryTimeUs"): "B497",
    ("DoorStateManager", "clearPendingClicks"): "B497",
    ("DoorStateManager", "getDoorsInRadius"): "B497",
    ("DoorStateManager", "removePendingClick"): "B497",
    ("StringDatabase", "getQueryCount"): "B497",
    ("TradeManager", "addItemToTrade"): "B497",
    ("TradeManager", "getMySpawnId"): "B497",
    ("TradeManager", "handlePartnerItemRemoved"): "B497",
    ("TradeManager", "removeItemFromTrade"): "B497",
    ("TradeManager", "setOwnMoney"): "B497",
    ("WilleqDatabase", "getAnimationSound"): "B497",
    ("ZoneLines", "addZonePoint"): "B497",
    ("ConsoleInputHandler", "isKeyboardMode"): "B498",
    ("ConsoleInputHandler", "isStdinTty"): "B498",
    ("GraphicsInputHandler", "injectLootRequest"): "B498",
    ("GraphicsInputHandler", "injectTargetRequest"): "B498",
    ("NullInputHandler", "injectChatMessage"): "B498",
    ("NullInputHandler", "injectFace"): "B498",
    ("NullInputHandler", "injectFaceEntity"): "B498",
    ("NullInputHandler", "injectLoot"): "B498",
    ("NullInputHandler", "injectMoveCommand"): "B498",
    ("NullInputHandler", "injectMoveToEntity"): "B498",
    ("NullInputHandler", "injectRawCommand"): "B498",
    ("NullInputHandler", "injectSpellCast"): "B498",
    ("NullInputHandler", "injectStopMovement"): "B498",
    ("NullInputHandler", "injectTarget"): "B498",
    # B1015 (#336): the tip's test_sfx_cache_eviction.cpp carried a free
    # function named preloadSound that kept this one tests-only; the B1015
    # rewrite dropped that helper, so the name is now called from nowhere.
    # (The B499/B500/B501 rows that flanked this one on the MR side were
    # deleted with their functions by 5b913256 on fable and do not survive
    # the merge.)
    ("AudioManager", "preloadSound"): "B600",
}

EXEMPT_TESTS_GROUPS = {
    # B1176 (#495): the client's first- and third-person camera height above
    # the player's z, (6.2 - 3.125) x s (005701c0:284524 via FUN_00489bf5;
    # actor_scale.h:149). The player eye code implements the same rule inline
    # from the NODE SCALE (animated_mesh_eye.cpp:128,169) -- it applies the
    # fable posture drop in model space before the scale, so it cannot call
    # the (race, gender, size) overload; test_actor_scale's
    # TheB1176CameraHeights is the rule's observation window.
    ("(free)", "clientCameraHeightAbovePlayerZ"): "observer",
    ("(free)", "ComputeHelpCoverage"): "seam",
    # B1138 (#458): tools/model_viewer was the one non-test caller of
    # getSpellColorForSpell and now runs the client's own cast sequence
    # (spell_cast_fx.cpp); getSpellColorForSpell is T381's test window on the
    # noted vocabulary's fallback colour (test_spell_visual_vocabulary).
    # B1152 (#472): createProjectile is GONE -- the missile actor replaced it,
    # and B1170 (#489) removed that record-driven missile too, so its exemption
    # (boarded as B1128, which was to wire it) is retired. missileCount was the
    # test's observation window on the missile actor; B1174 (#493) gave it a
    # non-test caller (the FX harness's per-frame fx_activity.tsv), so its
    # exemption is retired. The travel-stage Projectile preset has no production
    # caller (the 0x4033 missile object builds its Middle emitter directly,
    # SourceForSpell + PROJECTILE_PATH), but test_spell_landing_volume still
    # exercises the PROJECTILE_PATH emitter it defines.
    ("(free)", "Projectile"): "seam",
    ("SpellVisualFX", "getSpellColorForSpell"): "observer",
    # B1181 (#500): whether the i-th missile object is drawing its +0x65 IT
    # model (type != 9 with a found IT mesh, never a type-9 object). The
    # production path reads m_missile_objects directly; test_b1181_item_missile
    # is the rule's observation window (drawn per frame, gone at the
    # +0x68 lifetime, absent for a type-9 object and for a non-IT name).
    ("SpellVisualFX", "missileModelDrawn"): "observer",
    # B1150 (#470): the census test's observation window onto every file entry
    # (zero-based, entry 0 included -- a real shape-0 point that the 1-based
    # slot accessors reject by design, since spellsnew.eff's slot numbers are
    # 1-based with 0 = empty slot). test_spell_spawn_shapes's
    # CensusOfTheShapeWord counts all 2118 entries through it.
    ("SpellsNewEddReader", "shapeAt"): "observer",
    # B1151 (#471): the census test's observation window onto every file entry
    # (zero-based, entry 0 included -- the 1-based slot accessors reject 0 by
    # design). test_spell_edd_motion's CensusOfTheMotionWords counts all 2118
    # entries through it (the B1150 shapeAt pattern).
    ("SpellsNewEddReader", "motionAt"): "observer",
    # B1153 (#473): the measured rows are retired (the runtime reads
    # geometry/timing/population from spellsnew.edd); sourceRecordCount() and
    # targetRecordCount() are the tests' window proving the config carries no
    # per-record rows any more (test_spell_anim_rows / test_spell_source_anchors /
    # test_spell_target_anchors assert both return 0).
    ("SpellEffectsConfig", "sourceRecordCount"): "observer",
    ("SpellEffectsConfig", "targetRecordCount"): "observer",
    # B1083 (#403): the test's window onto which pack served an (archive, name)
    # EQE1 request. Its production WRITER is recordEQE1Request()/dumpEQE1RequestLog()
    # at zone load (T214); the reader is test_eqe1_source_archive, which asserts
    # the object's tree70 was served by its own pack, not the configured-order
    # scan. An observer on the request log.
    ("ConstrainedTextureCache", "eqe1ServedByStem"): "observer",
    # B1075 (#395): the layout table's row -> rect map. Every arm forwards to
    # the UILayout member or <Window>Layout::windowRect() the window's own
    # render body and hit test call; the reader is the co-open census
    # (test_ui_layout_coopen), which asserts the required sets over it.
    ("(free)", "windowRectFor"): "observer",
    # B1076 (#396): the spellbook's first step exempted its twelve layout
    # accessors as test-only observers. The render/input step drew and
    # hit-tested from the same table (renderSpellbookPopup / the
    # StaticUIInput spellbook arm), so all twelve moved to production and
    # the exemptions were stale -- the table is not allowed to outlive its
    # rows. The geometry they expose stays pinned by test_spellbook_layout.
    # B1069 (#389): the playback DEVICE rate. The production path is
    # main.cpp -> setConfiguredDeviceSampleRate -> AudioManager ->
    # MiniaudioBackend::setDeviceSampleRate -> init/start (whose INFO line
    # reports the same values these read); the accessors are
    # test_audio_device_rate's observation window on it, setUseNullBackend
    # the seam that opens miniaudio's null device on a host with no audio.
    ("AudioManager", "getDeviceSampleRate"): "observer",
    ("DeviceRateAdapter", "mixerFramesPulled"): "observer",
    ("MiniaudioBackend", "configuredSampleRate"): "observer",
    ("MiniaudioBackend", "configuredFormatName"): "observer",
    ("AudioManager", "getDeviceFormat"): "observer",
    ("MiniaudioBackend", "isResampling"): "observer",
    ("MiniaudioBackend", "requestedDeviceSampleRate"): "observer",
    ("MiniaudioBackend", "setUseNullBackend"): "seam",
    # B976 (#294): the zone depth tie-break's pure C++ decision -- the C++
    # mirror of the GLSL line EQT_DEPTH_TIE_VS_LINES that production executes
    # in the region vertex programs. Its only caller is the unit test
    # (tests/test_zone_depth_tie_break.cpp), which drives it without a GPU;
    # the production counterpart is the GLSL splice plus uDepthTieBiasForFlag
    # (live in zone_shader_callbacks.h). A test seam, like ComputeHelpCoverage.
    ("(free)", "biasedStoredDepth"): "seam",
    # B430/B433: the Options window's per-control classification table is a
    # CENSUS, not a runtime input -- its reader is the test that fails on an
    # unclassified control row (OptionsClassification.*), which is the point.
    ("(free)", "controlClassifications"): "observer",
    # B1084 (#404): the robe-range predicates are now tests-only in production
    # -- every real consumer routes through isRobeTexture(). The three
    # non-robe predicates (NPC-only, player-race, robe-index) are the test's
    # observation window on the MEASURED range: test_robe_texture_range's
    # 0..24/240 table is the net that a second hard-coded range spelling is a
    # regression against, so the test IS the point (observer, not dead).
    ("(free)", "isNPCOnlyTexture"): "observer",
    ("(free)", "isPlayerRaceTexture"): "observer",
    ("(free)", "getRobeIndex"): "observer",
    ("(free)", "controlClassificationCount"): "observer",
    ("(free)", "findControlClass"): "observer",
    ("(free)", "dispositionText"): "observer",
    ("(free)", "applyTradeskillContainerClosed"): "B899",
    # B888-CC3 (#204): the era data's full lists and row sources are the tests'
    # window onto the oracle rows the production lookups filter. (B998 hosted
    # the creation screen on the character-select screen, which made its
    # click resolver, Get Name round trip and refusal applier live.)
    ("CharCreateData", "allCities"): "observer",
    ("CharCreateData", "allClasses"): "observer",
    ("CharCreateData", "allDeities"): "observer",
    ("CharCreateData", "citySource"): "observer",
    # B918: the process-wide FATAL-exit record is a one-way latch in a real
    # run (set once, read by main()); a test that trips it on purpose must
    # clear it so later cases in the same binary start clean.
    ("(free)", "ResetFatalExitForTest"): "seam",
    # B1099 (#419): the could-not-connect record and the quit-signal latch
    # are the same one-way process-wide shape as B918's FATAL record; a test
    # that trips one on purpose clears it for the next case.
    ("(free)", "ResetCouldNotConnectForTest"): "seam",
    ("(free)", "ResetQuitSignalsForTest"): "seam",
    # B1099: the test's windows onto the pre-zone connect bound -- the text
    # the loading screen shows now (production writer: SetLoadingPhase's
    # override, read by the bridge's setProgress/ZoneLoading publish from the
    # member) and the watchdog itself (production driver: TickLoginConnect).
    ("BridgeComponent", "CurrentLoadingStatusText"): "observer",
    ("LoginWorldComponent", "LoginConnectBound"): "observer",
    # B894 (#210): the in-flight fishing latch is gated live by
    # stopFishingOnMove/stopFishingOnPositionState (the state-bus listeners
    # that end a cast with eqstr 165) and by getSkillUseError's
    # forageOrFishingRefusal() already-fishing gate; its ONLY remaining reader
    # outside that logic is the test that pins the refusal table, so the
    # accessor is the test's observation window on a live class.
    ("SkillManager", "isFishing"): "observer",
    # B1092 (#412): the queue's running_ latch is written live by stop() and
    # read by the worker's top-of-loop and drain-loop checks; the only
    # outside reader is StopWhileItemsPending, which blocks the in-flight
    # item on a gate and must observe the stop landed (running_ false) before
    # opening the gate so the drain loop's running_ check is ordered ahead of
    # the next pop. The accessor is the test's observation window on a live
    # class; removing it would force the test back to a wall-time bound (the
    # very assumption that flaked).
    ("BackgroundWorkQueue", "isStopping"): "observer",
    # T504: the original client's LOAD order (globals first) as opposed to
    # the loader's lookup order (resolverOrder); the era-sources test pins it.
    ("EraModelSources", "orderedArchives"): "observer",
    # B1019 (#340): the layout-equality net's core side -- the sizeof
    # of ZoneMeshBuilder as willeq_core's zone_geometry.cpp saw it. Its
    # only caller is test_zone_geometry_atlas_layout, which is the test's
    # observation window on the core's macro state (the public define's
    # whole point is that a consumer sees the same class the core built).
    ("ZoneMeshBuilder", "layoutSize"): "observer",
    # B928 (#246) deleted the traceConfigure/traceReport rows: the benchmark's
    # per-point trace instrument (tools/hwcaps/zone_bench_loadpoint_trace.cpp)
    # now calls both, so they are no longer tests-only -- a real tools/ caller
    # moved them bucket, and an exemption may not outlive its row.
    # T504: the entity-load callers moved to the instance form
    # (extractEquipmentModelCached, one open handle + one parsed WLD across
    # items); the static form is the held-item test's own-handle seam and
    # shares the body (extractFromParsed) with the live path.
    ("EquipmentModelLoader", "extractEquipmentModelOffThread"): "seam",
    # T504 fix 1: the custom SoundFont is loaded lazily and freed on the way
    # back to GM. B1014 moved the release to a real end and made
    # isCustomSoundFontResident a production caller (the stop paths' release
    # guard), so its observer exemption is gone; the test's window is now on
    # the release side (drainPendingFontRelease). setCustomSoundFontPath is
    # B1014's failed-reload seam: it re-points the on-demand load so
    # test_soundfont_release can exercise a missing font file without
    # deleting the real asset. Production never calls it (the path is fixed
    # at init).
    ("MidiPlayer", "setCustomSoundFontPath"): "seam",
    # B989 (#308): the raw-PCM construction seam. Production decodes only
    # from files/memory archives; test_sfx_cache_eviction builds a
    # SoundBuffer from deterministic raw PCM (no archive, no EQ client files
    # needed) to pin the shared-buffer cache's LRU mechanics.
    ("SoundBuffer", "loadFromPCM"): "seam",
    # B1015 (#336): the test's observation window on the one decoded-PCM
    # cache -- a non-loading residency read that lets the eviction tests see
    # what was evicted without triggering a re-decode (getOrLoad re-loads on
    # a miss, so it cannot tell "evicted" from "resident").
    ("SoundBufferCache", "peek"): "observer",
    # B1074 (#394): the emitter loop tests' observation window on the
    # looping channel a zero-cooldown emitter owns (does it hold one, and
    # which token -- the tests check the mixer channel behind it). The
    # emitter reads loop_ itself; no production caller needs the accessors.
    ("ZoneSoundEmitter", "isLooping"): "observer",
    ("ZoneSoundEmitter", "getLoopHandle"): "observer",
    # B972 (#290): the bounded-VBO/EBO seams on the two self-contained cut
    # sites. setVboPathEnabled is the SEAM -- the pixel-identity test flips it
    # to prove the VBO and client-array paths render the same bytes; production
    # keeps the default (VBO on) and never calls it. isVboPathEnabled and the
    # byte-count accessors (vertexBufferBytes / vboBytes) are the test's
    # OBSERVATION window: the logical-tally assertions read the byte count the
    # VBO path reports into the driver's tally.
    ("UnifiedParticleRenderer", "setVboPathEnabled"): "seam",
    ("UnifiedParticleRenderer", "isVboPathEnabled"): "observer",
    ("UnifiedParticleRenderer", "vertexBufferBytes"): "observer",
    ("ImpostorBatchNode", "setVboPathEnabled"): "seam",
    ("ImpostorBatchNode", "isVboPathEnabled"): "observer",
    ("ImpostorBatchNode", "vboBytes"): "observer",
    # B997 (#317): the per-material client-array batch of the PVS doors. The
    # unit test (test_scene_graph_batch.cpp) drives addNode/build/render on the
    # EDT_NULL driver and needs two things production never uses: a way to
    # override the per-type transparency classification (the null driver
    # reports every built-in type opaque, so the opaque-only guard is
    # unobservable without it) and the baked client array exactly as
    # render() would draw it (the transform + index-remap assertions).
    # (groupCount left this list when the final leg made it production: the
    # A/B's proof line reads it via DoorManager::sgBatchGroups().)
    ("SceneGraphBatch", "setTransparencyClassifier"): "seam",
    ("SceneGraphBatch", "lastBakedDraw"): "observer",
    # B944: whether one cache entry's GL storage is resident right now (an
    # evicted entry keeps its object and restores on the next bind).
    # test_texture_eviction_residency asserts eviction and restore through it;
    # production only needs the counts (getResidentTextureCount()).
    ("ConstrainedTextureCache", "isResident"): "observer",
    # B947: whether one entry was pinned (no restore source; never evicted
    # again). test_texture_eviction_{residency,character_restore} assert it;
    # production logs the count (getPinnedTextureCount()).
    ("ConstrainedTextureCache", "isPinned"): "observer",
    # Spatial budget overrun (B813 follow-up): the occluder table's reserved
    # heap bytes. test_region_occluders asserts it is exactly 40 B/triangle +
    # 8 B/region -- the net that keeps the per-region-block layout from
    # returning. The gate's ledger measures the same bytes in production.
    ("RegionOccluderTable", "reservedBytes"): "observer",
    # B909b: the placed-object collision set's region index. The ray-identity
    # test checks every region of the old per-instance copy is still present
    # (production only asks rayIntersect(), which answers "no hit" for an
    # absent region).
    ("ObjectCollisionSet", "hasRegion"): "observer",
    # B945: the bridge event/staging queue's retained capacity (bytes), read
    # under the queue's own mutex. A live observation window: production
    # measures the same bytes through the memory ledger's [memledger-trace]
    # rows; the capacity-bounded unit test (test_game_state_sync_bridge)
    # asserts the burst's buffer is returned once it drains.
    ("GameStateBridge", "eventQueueCapacityBytes"): "observer",
    ("GameStateSyncBridge", "forwardQueueCapacityBytes"): "observer",
    # T504b Leg 2b: the unload test's window on the evictable set, and the
    # clock it turns forward so the hysteresis window elapses in a test.
    ("RaceModelLoader", "evictableCountForTest"): "observer",
    ("RaceModelLoader", "sweepEvictableForTest"): "seam",
    # T491: the memory ledger's read-back of the calling thread's current
    # MemoryScope category. (B843 gave it production callers: the S3D and
    # armor texture loaders stamp the allocating scope onto each TextureInfo.)
    # The tests use it to prove scopes nest and restore in LIFO order and
    # that a new thread starts Unattributed. A pure observation window on
    # live state.
    ("(free)", "ConsiderEqStrIds"): "seam",
    ("(free)", "ItemDisplayEqStrIds"): "seam",
    ("(free)", "SpellDisplayEqStrIds"): "seam",
    # B895: the tracking feature's eqstr census list (the shipped-db test),
    # and the controller's two read-only views the route test observes.
    ("(free)", "TrackingEqStrIds"): "seam",
    ("TrackingController", "TrackedSpawnId"): "observer",
    ("TrackingController", "Settings"): "observer",
    ("(free)", "Test_GetLastComputedPositionUpdate"): "seam",
    ("(free)", "Test_SnapToMovementTargetForSeam"): "seam",
    # T490: the PVS store's two observation windows. setPvs() is the pinned,
    # pre-decoded form a HAND-BUILT tree uses (tests and tools construct a
    # BspTree with no WLD behind it); the WLD parser always calls setPvsRle().
    # pvsMemoryBytes() reports one region's share of the arena so a test can
    # assert the storage is linear in regions and not regions^2/8 -- production
    # tallies the whole arena at once through BspTree::pvsArenaBytes().
    ("BspRegion", "setPvs"): "seam",
    ("BspRegion", "pvsMemoryBytes"): "seam",
    ("EquipmentModelLoader", "debugInjectCachedMeshForTest"): "seam",
    ("FrameComponent", "frustumDebugBoxCount"): "seam",
    ("FrameComponent", "frustumDebugDraw"): "seam",
    ("FrameComponent", "renderDebugOverlaysForTest"): "seam",
    ("FrameComponent", "seedFrustumDebugBoxForTest"): "seam",
    ("GmComponent", "Test_GetLastGMHideMe"): "seam",
    ("GmComponent", "Test_GetLastGMSummon"): "seam",
    # B1081 (#401): the OP_GMZoneRequest2 confirm packet, test_gm_component.
    ("GmComponent", "Test_GetLastGMZoneRequest2"): "seam",
    ("LoginWorldComponent", "ReplayLoginPacket"): "seam",
    ("LoginWorldComponent", "ReplayWorldPacket"): "seam",
    # B1081 (#401): the zone-change reconnect window, test_zone_change_no_flash.
    ("LoginWorldComponent", "Test_EnterWorldSentForSeam"): "seam",
    ("OverlaysComponent", "mapOverlayTriangleCount"): "seam",
    ("OverlaysComponent", "navmeshOverlayTriangleCount"): "seam",
    ("OverlaysComponent", "seedMapOverlayTriangleForTest"): "seam",
    ("OverlaysComponent", "seedNavmeshOverlayTriangleForTest"): "seam",
    ("PacketSender", "ReplaySuppressedSendCount"): "seam",
    ("PacketSender", "Test_SetBypassSendGuardForSeam"): "seam",
    ("ParticleManager", "Test_SetUnifiedRendererInitializedForSeam"): "seam",
    ("PlayerCameraComponent", "collisionDebugLineCount"): "seam",
    ("PlayerCameraComponent", "seedCollisionDebugLineForTest"): "seam",
    # B992: the pre-B992 one-layer flush, so test_ui_panel_z_order can show
    # its pixel assertion catches the bleed (setBatchingEnabled's sibling).
    ("UIRenderer", "setLayeringEnabled"): "seam",
    ("PlayerComponent", "Test_GetLastFaceChange"): "seam",
    ("PlayerComponent", "Test_GetLastInspectAnswer"): "seam",
    ("RaceModelLoader", "getChrCacheAccountingForTest"): "seam",
    ("RaceModelLoader", "variantLoadTrace"): "seam",
    ("RenderBarrier", "inRender"): "seam",
    ("ZoneComponent", "Test_BeginGracefulZoneOutForSeam"): "seam",
    ("ZoneComponent", "Test_DisconnectFromZoneForSeam"): "seam",
    ("ZoneComponent", "Test_GetLastZoneChangeRequest"): "seam",
    ("ZoneComponent", "Test_IsZoneOutAcknowledged"): "seam",
    ("ZoneComponent", "Test_IsZoneOutHandshakePending"): "seam",
    ("ZoneComponent", "Test_IsZoneToZoneInProgress"): "seam",
    ("ZoneComponent", "Test_SetZoneToZoneInProgressForSeam"): "seam",
    ("ActionDispatcher", "isReady"): "observer",
    ("AudioManager", "getLoadedSoundCount"): "observer",
    ("AudioManager", "isLoopbackMode"): "observer",
    ("BackgroundThreadPool", "getThreadCount"): "observer",
    ("BackgroundThreadPool", "isShutdown"): "observer",
    ("BackgroundWorkQueue", "getCompletedCount"): "observer",
    ("BoidsManager", "getCreatureMaterial"): "observer",
    ("BuffManager", "getPetBuffs"): "observer",
    ("BuffManager", "getPetBuffsOwnerId"): "observer",
    ("BuffManager", "hasEntityBuff"): "observer",
    ("BuffManager", "hasPlayerBuff"): "observer",
    # B888-CC2 (#204): the pre-zone preview's observation windows. The host
    # class (CharPreviewScene) is thin/render-thread-only, exercised live in
    # the visual gate, not headless; modelRace is the test's window onto its
    # rebuild-on-change bookkeeping. CharacterModelView's race/gender getters
    # are the headless model leg's (test_char_preview.cpp) window onto which
    # body the paperdoll actually stood up -- the "right race model is in
    # the view" assertion; the class's live callers are setCharacter/
    # hasCharacterModel, wired by the preview host and the paperdoll.
    ("CharPreviewScene", "modelRace"): "observer",
    # B888-CC3 (#204): the clz backdrop's observation windows. CharBackdropScene
    # is thin/render-thread-only, exercised live in the visual gate (the
    # showroom stood up in its own scene manager under the pre-zone frame);
    # the headless leg (test_explore_backdrop.cpp) drives the same class under
    # an EDT_NULL device. hasScene is its "the 3D slice stood up" window and
    # characterCount is its "the per-row stands round-trip" window onto the
    # setCharacter/clearCharacter bookkeeping; their live callers are init/
    # setCharacter/renderScene/rowForClick, wired by the char-select host.
    ("CharBackdropScene", "hasScene"): "observer",
    ("CharBackdropScene", "characterCount"): "observer",
    ("CharacterModelView", "getCurrentGender"): "observer",
    ("CharacterModelView", "getCurrentRaceId"): "observer",
    ("CharacterModelView", "isReady"): "observer",
    ("CombatManager", "GetLootSubState"): "observer",
    ("CombatManager", "IsAutoLootEnabled"): "observer",
    ("CombatManager", "IsAutoMovementEnabled"): "observer",
    ("CombatManager", "IsFleeing"): "observer",
    ("CombatManager", "IsLooting"): "observer",
    ("CombatState", "combatTarget"): "observer",
    ("CombatState", "isAutoAttacking"): "observer",
    ("CombatState", "targetHPPercent"): "observer",
    ("CombatState", "targetLevel"): "observer",
    ("CommandProcessor", "getAllCommandNames"): "observer",
    ("CommandProcessor", "getDefaultChannel"): "observer",
    ("CommandProcessor", "hasCommand"): "observer",
    ("CommandProcessor", "isEchoEnabled"): "observer",
    ("CommandRegistry", "getAllCommandNames"): "observer",
    ("DetailManager", "getViewDistanceChunks"): "observer",
    ("DoorManager", "hasDoor"): "observer",
    ("DoorStateManager", "hasDoor"): "observer",
    ("EQE1TextureIndex", "archiveCount"): "observer",
    ("EffLoader", "getEmitSounds"): "observer",
    ("EffLoader", "getLoopSounds"): "observer",
    ("EffLoader", "getMusicEntryCount"): "observer",
    ("EntityRenderer", "isPlayerBodyWithheldFromRenderList"): "observer",
    ("EntityRenderer", "skipTextureUpload"): "observer",
    ("EnvironmentState", "isSnowing"): "observer",
    ("FrameComponent", "hasCommandCompletionProvider"): "observer",
    ("GuildComponent", "HasPendingGuildInvite"): "observer",
    ("InventoryManager", "getCursorSourceSlot"): "observer",
    ("InventoryState", "bankItemCount"): "observer",
    ("InventoryState", "cursorQueueSize"): "observer",
    ("InventoryState", "equipmentAC"): "observer",
    ("InventoryState", "equippedCount"): "observer",
    ("InventoryState", "freeBagSlots"): "observer",
    ("InventoryState", "freeGeneralSlots"): "observer",
    ("InventoryState", "hasBankItem"): "observer",
    ("InventoryState", "hasGeneralItem"): "observer",
    ("IrrlichtRenderer", "getCameraMode"): "observer",  # B534: its only production caller was GraphicalMode::getCameraMode, which B534 deleted; the const accessor (PlayerCamera().cameraMode()) is the camera test suites' observation window on a live class
    ("IrrlichtRenderer", "getPlayerZ"): "observer",
    ("IrrlichtRenderer", "getUIRenderer"): "observer",
    ("IrrlichtRenderer", "getVisionType"): "observer",
    ("IrrlichtRenderer", "isDeferredAssetLoading"): "observer",
    ("IrrlichtRenderer", "isNetworkReady"): "observer",
    ("IrrlichtRenderer", "isNewUIEnabled"): "observer",
    ("LoginWorldComponent", "CharacterSelectReturns"): "observer",  # B1005
    ("LoginWorldComponent", "GetAccountId"): "observer",
    # B1081 (#401): GetCharacterSelectIndex gained a production caller
    # (ApplicationScenarioWorld::CharacterSelectName) -- removed from this
    # tests-only table; it is now classified by its live caller.
    ("LoginWorldComponent", "GetChatConnectionType"): "observer",
    ("LoginWorldComponent", "GetChatServerHost"): "observer",
    ("LoginWorldComponent", "GetChatServerPort"): "observer",
    ("LoginWorldComponent", "GetMailKey"): "observer",
    ("LoginWorldComponent", "GetSessionKey"): "observer",
    ("LoginWorldComponent", "GetWorldServers"): "observer",
    ("LoginWorldComponent", "GuildNames"): "observer",
    ("LoginWorldComponent", "IsWorldReady"): "observer",
    ("LoginWorldComponent", "WorldServerHost"): "observer",  # B1005: ReconnectWorld reads the member
    ("MidiPlayer", "getActiveSoundFont"): "observer",
    ("MidiPlayer", "getConfiguredMaxVoices"): "observer",
    ("MidiPlayer", "getPreallocatedChannelCount"): "observer",
    ("MidiPlayer", "getVolume"): "observer",
    ("MidiPlayer", "isAudioThreadRunning"): "observer",
    ("MusicPlayer", "getVolume"): "observer",
    ("MusicPlayer", "isPaused"): "observer",
    ("PetState", "petHPPercent"): "observer",
    ("PetState", "petLevel"): "observer",
    ("PetState", "petManaPercent"): "observer",
    ("PetState", "petName"): "observer",
    ("PlayerCameraComponent", "getBaseVision"): "observer",
    ("PlayerCameraComponent", "playerPositionIsSeed"): "observer",
    ("PlayerComponent", "HasPendingRezz"): "observer",
    ("PlayerComponent", "IsDraggingCorpse"): "observer",
    ("PlayerComponent", "IsInDuel"): "observer",
    ("PlayerState", "hasMovementTarget"): "observer",
    ("PlayerState", "totalBankCopperValue"): "observer",
    ("PlayerState", "totalCopperValue"): "observer",
    ("RaidRoster", "CountInGroup"): "observer",
    ("RaidRoster", "Find"): "observer",
    ("RaidRoster", "Size"): "observer",
    ("RainOverlay", "getIntensity"): "observer",
    ("S3DLoader", "getCharacterCount"): "observer",
    ("ScenarioRunner", "started"): "observer",
    ("SkillCapTable", "classCanEverHaveSkill"): "observer",
    ("SkillCapTable", "entryCount"): "observer",
    ("SkillManager", "getSkill"): "observer",
    ("SkillManager", "getSkillCooldownRemaining"): "observer",
    ("SkillManager", "getSkillCount"): "observer",
    ("SkillManager", "getSkillMax"): "observer",
    ("SkillManager", "isSkillOnCooldown"): "observer",
    ("SnowOverlay", "getIntensity"): "observer",
    ("SoundAssets", "forEach"): "observer",
    ("SoundAssets", "getAllSoundIds"): "observer",
    ("SoundAssets", "getFilename"): "observer",
    ("SoundAssets", "getVolume"): "observer",
    ("SoundAssets", "hasSound"): "observer",
    ("SoundBuffer", "getDuration"): "observer",
    ("SpellData", "getEffect"): "observer",
    ("SpellData", "getEffectCount"): "observer",
    # B1015 (#336): the /pmem stat's production caller was the deleted
    # SfxManager::getCacheSize (the second LRU); the same-named
    # SpellDatabase accessor dropped to tests-only. It is the test's
    # observation window on the spell LRU's capacity bound.
    ("SpellDatabase", "getCacheSize"): "observer",
    ("SpellDatabase", "getCacheCapacity"): "observer",
    ("SpellDatabase", "getMaxSpellId"): "observer",
    ("SpellManager", "getCastProgress"): "observer",
    ("SpellManager", "getCastTimeRemaining"): "observer",
    ("SpellManager", "getCurrentSpellId"): "observer",
    ("SpellManager", "getGemCooldownProgress"): "observer",
    ("SpellManager", "getScribedSpells"): "observer",
    ("SpellState", "castProgress"): "observer",
    ("SpellState", "castTimeTotal"): "observer",
    ("SpellState", "castingSpellId"): "observer",
    ("SpellState", "getGemCooldownProgress"): "observer",
    ("SpellState", "getGemSpellId"): "observer",
    ("SpellState", "hasSpellMemorized"): "observer",
    ("SpellState", "isGemReady"): "observer",
    ("SpellState", "memorizingGemSlot"): "observer",
    ("SpellState", "scribedSpellCount"): "observer",
    ("TradeState", "pendingPartnerId"): "observer",
    ("TradeState", "pendingPartnerName"): "observer",
    ("TradeskillPanelState", "maxRecipeScroll"): "observer",
    ("WeatherEffectsController", "isSnowing"): "observer",
    ("WldLoader", "getBoneOrientations"): "observer",
    ("WldLoader", "getTotalRegionCount"): "observer",
    ("WldLoader", "hasCharacterData"): "observer",
    ("WldLoader", "hasGlobalAmbientLight"): "observer",
    ("WorldState", "isZoneLoading"): "observer",
    ("WorldState", "isZoning"): "observer",
    ("ZoneAudioManager", "getCurrentZone"): "observer",
    ("ZoneAudioManager", "isZoneLoaded"): "observer",
    ("ZoneLines", "hasExtractedZoneLines"): "observer",
    ("ZoneSoundEmitter", "getSequence"): "observer",
    ("CombatMusicManager", "getCombatDelay"): "B600",
    ("CombatMusicManager", "getFadeOutTime"): "B600",
    ("CombatMusicManager", "getStingerCount"): "B600",
    ("CombatMusicManager", "getStingerFilename"): "B600",
    ("CombatMusicManager", "getVolume"): "B600",
    ("CombatMusicManager", "isInCombat"): "B600",
    ("CombatMusicManager", "onCombatEnd"): "B600",
    ("CombatMusicManager", "onCombatStart"): "B600",
    ("CombatMusicManager", "setCombatDelay"): "B600",
    ("CombatMusicManager", "setFadeOutTime"): "B600",
    ("CreatureSounds", "getRacesWithSounds"): "B600",
    ("CreatureSounds", "getSoundFileVariants"): "B600",
    ("CreatureSounds", "hasSoundFile"): "B600",
    ("DoorSounds", "getDoorSoundFilename"): "B600",
    ("DoorSounds", "getObjectSound"): "B600",
    ("DoorSounds", "getObjectSoundFilename"): "B600",
    ("DoorSounds", "hasSeparateOpenCloseSound"): "B600",
    ("PlayerSounds", "getSoundId"): "B600",
    ("UISounds", "getSoundId"): "B600",
    ("UISounds", "getTypeName"): "B600",
    ("WeatherAudio", "getIntensity"): "B600",
    ("WeatherAudio", "getVolume"): "B600",
    ("WeatherAudio", "getWeatherType"): "B600",
    ("WeatherAudio", "isPaused"): "B600",
    ("WeatherAudio", "isSnowing"): "B600",
    ("WeatherAudio", "isThunderEnabled"): "B600",
    ("WeatherAudio", "setThunderEnabled"): "B600",
    ("CombatState", "updateTargetHP"): "B601",
    ("CombatState", "updateTargetLevel"): "B601",
    ("GameState", "resetForZoneChange"): "B601",
    ("GroupState", "setMemberInZone"): "B601",
    ("GroupState", "updateMemberSpawnId"): "B601",
    ("InventoryState", "setBagInfo"): "B601",
    ("InventoryState", "setCursorQueueSize"): "B601",
    ("InventoryState", "setEquipmentStats"): "B601",
    ("PetState", "updatePetHP"): "B601",
    ("PetState", "updatePetMana"): "B601",
    ("PlayerState", "loadFromProfile"): "B601",
    ("PlayerState", "setCurHP"): "B601",
    ("PlayerState", "setPositionAndHeading"): "B601",
    ("SpellState", "addScribedSpell"): "B601",
    ("SpellState", "clearGem"): "B601",
    ("SpellState", "setMemorizing"): "B601",
    ("SpellState", "updateMemorizeProgress"): "B601",
    ("WorldState", "setZoneId"): "B601",
    ("WorldState", "setZoning"): "B601",
    # B603: the three rows reclassified seam (2026-10-11, issue #187) --
    # tests-only functions whose tests are the net for LIVE behaviour, so
    # the function lives with them (T458's test-seam class).
    # B1025-H3 (GitLab #345): the assignSlot/clearSlot seam rows are GONE --
    # BridgeComponent::onHotbarAssignIntent (the placement: a held hot button
    # or /hotbutton's, the first production caller of assignSlot) and
    # onHotbarClearIntent (the pickup: a hold on an occupied slot cleared its
    # source, the first production caller of clearSlot) are the production
    # callers both comments predicted. Their tests (T156 skill-reachability,
    # T15 input-action-bridge routing, T230 hotbar item route) still drive
    # them as the REAL producer of the HotbarSlotAssigned bridge event.
    # calcBankBagSlotId: the wire-constant arithmetic of the bank-bag
    # sub-slots (2031+), asserted in the bag-move tests against the same
    # constants the server computes.
    ("(free)", "calcBankBagSlotId"): "seam",
    # B1025-H1's setCurrentPage seam row is GONE (H-2, #345): the hotbar
    # window's page buttons produce HotbarPageChangedIntent, and
    # BridgeComponent::onHotbarPageChangedIntent calls it -- the production
    # caller H-1's comment predicted, which removes the exemption.
    ("CommandProcessor", "setDefaultChannel"): "B605",
    ("CommandProcessor", "setEchoEnabled"): "B605",
    ("CommandProcessor", "unregisterCommand"): "B605",
    ("ConsoleRenderer", "setStatusUpdateInterval"): "B605",
    # B605's HotkeyBinding::toString row is GONE: it was a dead one-line
    # delegate to the live static HotkeyManager::bindingToString, and its only
    # tests/ callers were toString(SpellVisualSource::...) lines in
    # test_spell_visual_selection.cpp that B606 deleted (the census attributes
    # a bare toString( call to it). Dead code whose only net was its own test
    # is deleted with the test (T458's rule), not re-exempted.
    ("HotkeyManager", "isMovementKey"): "B605",
    ("HotkeyManager", "modeNameToEnum"): "B605",
    # B1062 (#382): the spec's cost A/B (dladdr on the return address vs
    # backtrace(8) + dladdr per frame), timed once and printed as the
    # [classifier-cost] line. The production hot path is the table binary
    # search (classifyCallerAddress), which never calls it; the only caller
    # is tests/test_gpu_driver_classifier.cpp, which calls it once on the
    # mini to produce the per-call figure quoted in docs/macos.md. A
    # measurement seam like noteRenderedFrame in memory_ledger_b924.cpp.
    ("(free)", "measureClassifierCostNs"): "seam",
    # B606 (#189) is executed: all sixteen rows here were deleted together
    # with their only (tests/) callers. B606 also exposed two functions whose
    # live caller was a deleted row -- eqt::parseFormattedMessage (chat runs
    # parseFormattedMessageArgs) and the B605 HotkeyBinding::toString above --
    # both deleted with their tests rather than re-boarded.
}

EXEMPT = {k: ("boarded", BOARD_REASONS[v] + " " + v)
          for k, v in EXEMPT_GROUPS.items()}

# T458: the tests-only half.  A function whose ONLY callers are under tests/
# is one of four things, and the table says which:
#   seam      a hook that exists on a production class purely so a test can
#             drive or observe private state (`Test_*`, `*ForTest`,
#             `*ForSeam`, the session-replay packet injectors, the debug-draw
#             seed/count pairs).  It cannot move under tests/ -- it needs the
#             state it exposes -- and it is neither dead nor unwired.
#   observer  a const accessor on a class that HAS live production callers:
#             the test's window onto state the reached path writes.  Deleting
#             it guts a true net.  A class-level claim, not a per-row proof:
#             the accessor may still read a field only a boarded (tests-only)
#             mutator writes -- WriteOnlyFields is the net for that.
#   B6xx      a never-wired feature or unreached API, owned by that board
#             entry (the T39/B024 rule: board, do not wire mid-unit).
# Dead production code whose only net was its own test is not listed here: it
# is DELETED together with that test (T458 deleted 31 such rows).
TESTS_CATEGORY_REASONS = {
    "seam":
        "a test seam on a production class -- exists so a test can drive or "
        "observe private state; not dead, not unwired",
    "observer":
        "a const accessor on a class with live production callers -- the "
        "test's observation window on the reached path",
}

EXEMPT_TESTS = {
    k: ((v, TESTS_CATEGORY_REASONS[v]) if v in TESTS_CATEGORY_REASONS
        else ("boarded", BOARD_REASONS[v] + " " + v))
    for k, v in EXEMPT_TESTS_GROUPS.items()}


def load_exempt():
    return EXEMPT


def load_exempt_tests():
    return EXEMPT_TESTS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--bucket", default="zero",
                    choices=("zero", "tests", "tools", "all"))
    ap.add_argument("--json")
    ap.add_argument("--check", action="store_true",
                    help="ratchet mode: exit 1 if any zero-caller function is "
                         "missing from EXEMPT or any tests-only function is "
                         "missing from EXEMPT_TESTS, or if an entry in either "
                         "table no longer belongs to its bucket.  Used by the "
                         "ZeroCallers ctest test.")
    ap.add_argument("--tests-only", action="store_true",
                    help="with --check, check only the tests-only half "
                         "(EXEMPT_TESTS, T458)")
    ap.add_argument("--self-test", action="store_true",
                    help="run the classify_occurrence() probes (T486) and "
                         "exit; no tree is scanned")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    root = os.path.abspath(args.repo)

    decls, virtual_names = collect_functions(root)
    self_rel = os.path.relpath(os.path.abspath(__file__), root)

    candidates = {}
    for key, d in decls.items():
        if d.access == "private":
            continue
        if d.virtual or d.name in virtual_names:
            continue
        if d.name in ENTRY_POINTS or d.name in STDLIB_CUSTOMIZATION:
            continue
        if d.name.startswith("operator"):
            continue
        candidates[key] = d

    names = {n for (_o, n) in candidates}
    sys.stderr.write(
        "declared functions: %d (%d public/protected non-virtual candidates, "
        "%d distinct names) in %s\n"
        % (len(decls), len(candidates), len(names), ", ".join(DECL_DIRS)))

    # name -> {'src':[], 'tests':[], 'tools':[]} of (rel, line) USE sites
    uses = defaultdict(lambda: {"src": [], "tests": [], "tools": []})
    files = 0
    for path in walk(root, SWEEP_DIRS):
        rel = os.path.relpath(path, root)
        if rel == self_rel:
            continue
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        files += 1
        top = rel.split(os.sep)[0]
        bucket = ("tests" if top == "tests"
                  else "tools" if top in ("tools", "benchmarks")
                  else "src")
        for lineno, code in enumerate(src.split("\n"), 1):
            if not code:
                continue
            for m in TOKEN.finditer(code):
                nm = m.group(0)
                if nm not in names:
                    continue
                if classify_occurrence(code, m.start(), m.end()):
                    continue
                uses[nm][bucket].append((rel, lineno))
    sys.stderr.write("swept %d files in %s\n" % (files, ", ".join(SWEEP_DIRS)))

    zero, only_tests, only_tools = [], [], []
    for key, d in sorted(candidates.items()):
        u = uses.get(d.name, {"src": [], "tests": [], "tools": []})
        s = [p for p in u["src"] if not (p[0] == d.rel and p[1] == d.line)]
        t = u["tests"]
        o = u["tools"]
        if not s and not t and not o:
            zero.append(d)
        elif not s and not o and t:
            only_tests.append(d)
        elif not s and not t and o:
            only_tools.append(d)

    if args.check:
        failed = False
        halves = []
        if not args.tests_only:
            halves.append(("zero-caller", zero, load_exempt(), "EXEMPT"))
        halves.append(("tests-only", only_tests, load_exempt_tests(),
                       "EXEMPT_TESTS"))
        for label, rows, exempt, table in halves:
            found = {(d.owner, d.name) for d in rows}
            unlisted = sorted(found - set(exempt))
            stale = sorted(set(exempt) - found)
            for o, n in unlisted:
                d = candidates[(o, n)]
                print("NEW %s function: %s::%s   %s:%d"
                      % (label, o, n, d.rel, d.line))
            for o, n in stale:
                print("STALE %s exemption (no longer %s): %s::%s -- %s"
                      % (table, label, o, n, exempt[(o, n)][1]))
            if unlisted or stale:
                failed = True
                print("")
                print("%s: %d unlisted, %d stale."
                      % (label, len(unlisted), len(stale)))
                if unlisted and label == "zero-caller":
                    print("A function declared and never called is this "
                          "project's dominant defect class "
                          "(T366/T414/T442/T448/T452).\nWire it, delete it, "
                          "or add it to EXEMPT in this script with a category "
                          "and a reason.")
                if unlisted and label == "tests-only":
                    print("A function whose only caller is a test is either "
                          "dead production code with a false net (delete both), "
                          "a test seam, the test's observation window on a "
                          "live class, or a never-wired feature (board it).\n"
                          "Classify it in EXEMPT_TESTS_GROUPS (T458).")
                if stale:
                    print("An exemption whose function has changed bucket "
                          "must be removed from %s -- the table is not "
                          "allowed to outlive its rows." % table)
                print("")
                continue
            cats = defaultdict(int)
            for k in found:
                cats[exempt[k][0]] += 1
            print("%s census clean: %d functions, all classified in %s (%s)."
                  % (label, len(found), table,
                     ", ".join("%s %d" % kv for kv in sorted(cats.items()))))
        return 1 if failed else 0

    def dump(title, rows, extra=None):
        print("=" * 78)
        print(title)
        print("=" * 78)
        for d in rows:
            print("%-40s %s:%d  [%s]"
                  % (d.owner + "::" + d.name, d.rel, d.line, d.access))
            if extra:
                for p, l in uses[d.name][extra][:4]:
                    print("      %s  %s:%d" % (extra, p, l))
        print("\n%d." % len(rows))

    if args.bucket in ("zero", "all"):
        dump("ZERO CALLERS (no use in src/ include/ tests/ tools/ "
             "benchmarks/ docker/)", zero)
    if args.bucket in ("tests", "all"):
        dump("TEST-ONLY CALLERS (production code reached only from tests/)",
             only_tests, "tests")
    if args.bucket in ("tools", "all"):
        dump("TOOLS-ONLY CALLERS (reached only from tools/ or benchmarks/ "
             "-- board B166: NOT dead)", only_tools, "tools")

    if args.json:
        import json
        with open(args.json, "w") as f:
            json.dump({
                "zero": [{"owner": d.owner, "name": d.name, "file": d.rel,
                          "line": d.line, "access": d.access} for d in zero],
                "tests_only": [{"owner": d.owner, "name": d.name,
                                "file": d.rel, "line": d.line}
                               for d in only_tests],
                "tools_only": [{"owner": d.owner, "name": d.name,
                                "file": d.rel, "line": d.line}
                               for d in only_tools],
            }, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
