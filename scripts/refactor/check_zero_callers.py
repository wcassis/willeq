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
green run is not a proof that the tree has no dead functions.

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

Usage:
    python3 scripts/refactor/check_zero_callers.py [--repo ROOT]
                [--bucket zero|tests|tools|all] [--json OUT] [--check]
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
# `PageAlignedAllocator::allocate` / `::deallocate` (include/common/util/ksm.h)
# are the tree's live example -- every `std::vector<T, PageAlignedAllocator<T>>`
# reaches them and not one source line writes the name.
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
# so a lead that survives it is already type-shaped.  These are the two
# expression forms it still lets through: a LONE colon (a ternary arm, a
# constructor initialiser list, a label, a range-for) and `&&`.
LONE_COLON = re.compile(r"(?<!:):(?!:)")
BAD_IN_LEAD = ("&&",)


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
    return TYPEISH.match(lead) is not None


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
    "B499":
        "audio query API with no caller -- the music/3D/vendor state "
        "predicates, the mixer's sfx volume, the MP3 index and the PCM buffer "
        "load",
    "B500":
        "the IGameMode implementations' accessors with no caller -- "
        "getGameState, consoleInput, consoleRenderer and AutomatedMode's "
        "whole command queue, which T410's scenario lane replaced with "
        "setAutomationCallback",
    "B501":
        "common/ utility API with no caller -- the performance-metrics timing "
        "and zone-load marks, the KSM heap helpers, ClampLower, Shuffle and "
        "the logging mutex accessor",
    "B502":
        "gameplay-domain manager API with no caller -- SkillManager's "
        "ready/category/toggle queries, SpellAnimations' lookups, "
        "BuffManager's database accessor, the chat buffer's recent-message "
        "API, the movement helpers and PerformEmote",
}

# (owner, name) -> the board entry that owns it.  Same contract as
# check_write_only_fields.py's EXEMPT: a zero-caller function NOT listed
# here fails --check, and a listed function that HAS a caller again also
# fails, so the table cannot outlive its rows.  All 417 rows are class
# "boarded" -- T455 deleted the residue it could prove dead outright
# (common/util/strings, common/util/file, three position.h overload sets)
# and boarded the rest rather than wiring a feature mid-unit (the T39/B024
# rule).
EXEMPT_GROUPS = {
    ("(free)", "getCreatureTypeName"): "B490",
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
    ("(free)", "getAnimationSourceFromConfig"): "B491",
    ("(free)", "getAnimationSourceS3DFileByCode"): "B491",
    ("(free)", "getRobeClkTextureNumber"): "B491",
    ("(free)", "getRobeIndex"): "B491",
    ("(free)", "isNPCOnlyTexture"): "B491",
    ("(free)", "isPlayerRaceTexture"): "B491",
    ("(free)", "isVeliousTexture"): "B491",
    ("(free)", "normalToColor"): "B491",
    ("EQAnimatedMeshSceneNode", "getBoneIndexByName"): "B491",
    ("EQAnimatedMeshSceneNode", "getInstanceMeshIndexCount"): "B491",
    ("EquipmentModelLoader", "getMappingCount"): "B491",
    ("EquipmentModelLoader", "getTextureRef"): "B491",
    ("EquipmentModelLoader", "loadItemModelMappingStatic"): "B491",
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
    ("SkeletalAnimator", "pauseAnimation"): "B491",
    ("SkeletalAnimator", "removeFrameTrigger"): "B491",
    ("SkeletalAnimator", "resetPlaybackSpeed"): "B491",
    ("SkeletalAnimator", "resumeAnimation"): "B491",
    ("SkeletalAnimator", "setBlendDuration"): "B491",
    ("SkeletalAnimator", "setBlendingEnabled"): "B491",
    ("SkeletalAnimator", "setEventCallback"): "B491",
    ("SkyLoader", "getCelestialBodiesForSkyType"): "B491",
    ("SkyLoader", "getCelestialBodyCount"): "B491",
    ("SkyLoader", "getLayerCount"): "B491",
    ("WldLoader", "getTrackRef"): "B491",
    ("ZoneMeshBuilder", "buildAtlasedMesh"): "B491",
    ("ZoneMeshBuilder", "buildCompositeMesh"): "B491",
    ("ZoneMeshBuilder", "buildTexturedMeshFromUploaded"): "B491",
    ("ZoneMeshBuilder", "getConstrainedTextureCache"): "B491",
    ("ZoneMeshBuilder", "getFallbackBufferMap"): "B491",
    ("ZoneMeshBuilder", "registerLazyTexture"): "B491",
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
    ("CharacterModelView", "getCurrentGender"): "B493",
    ("CharacterModelView", "getCurrentRaceId"): "B493",
    ("CharacterModelView", "getPrimaryWeaponId"): "B493",
    ("CharacterModelView", "getRotationY"): "B493",
    ("CharacterModelView", "getSecondaryWeaponId"): "B493",
    ("CharacterModelView", "hasCharacterModel"): "B493",
    ("CharacterModelView", "hasPrimaryWeapon"): "B493",
    ("CharacterModelView", "hasSecondaryWeapon"): "B493",
    ("CharacterModelView", "isDragging"): "B493",
    ("CharacterModelView", "onMouseDown"): "B493",
    ("CharacterModelView", "onMouseMove"): "B493",
    ("CharacterModelView", "onMouseUp"): "B493",
    ("CharacterModelView", "setRotationY"): "B493",
    ("GraphicalRenderer", "getRenderQuality"): "B493",
    ("AnimatedTextureManager", "getAnimatedTextureCount"): "B494",
    ("AnimatedTextureManager", "getCurrentTexture"): "B494",
    ("CameraController", "adjustOrbitAngle"): "B494",
    ("CameraController", "getActualFollowDistance"): "B494",
    ("CameraController", "getHeadingEQ"): "B494",
    ("CameraController", "getMoveSpeed"): "B494",
    ("CameraController", "getOrbitAngle"): "B494",
    ("CameraController", "getPitch"): "B494",
    ("CameraController", "getRotateSpeed"): "B494",
    ("CameraController", "getYaw"): "B494",
    ("CameraController", "setOrbitAngle"): "B494",
    ("CameraController", "setOrientation"): "B494",
    ("CameraController", "setRotateSpeed"): "B494",
    ("ConstrainedTextureCache", "freeze"): "B494",
    ("ConstrainedTextureCache", "getAvailableMemory"): "B494",
    ("ConstrainedTextureCache", "getCompressedUploadCount"): "B494",
    ("ConstrainedTextureCache", "isFrozen"): "B494",
    ("ConstrainedTextureCache", "isPending"): "B494",
    ("ConstrainedTextureCache", "setLedgerEnabled"): "B494",
    ("DiagnosticsComponent", "toggleFog"): "B494",
    ("DoorManager", "isDoorMeshBuilt"): "B494",
    ("DoorManager", "rebuildPlaceholderDoors"): "B494",
    ("DoorManager", "recomputeAllBspRegions"): "B494",
    ("EQE1TextureIndex", "nameCount"): "B494",
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
    ("EntityRenderer", "getEntityPoseState"): "B494",
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
    ("EntityRenderer", "setEntityWeaponDelay"): "B494",
    ("EntityRenderer", "setGlobalAnimationSpeed"): "B494",
    ("EntityRenderer", "setNameTagDistance"): "B494",
    ("EntityRenderer", "setOcclusionCuller"): "B494",
    ("EntityRenderer", "setPlayerEntityVisible"): "B494",
    ("EntityRenderer", "stopEntityAnimation"): "B494",
    ("EntityRenderer", "updateInterpolation"): "B494",
    ("FrameBudgetGovernor", "canStartTask"): "B494",
    ("FrameBudgetGovernor", "hasTimeBudget"): "B494",
    ("GLES2EGLHelper", "uploadETC1AsSharedTexture"): "B494",
    ("GraphicsArchiveIndex", "getCachedArchive"): "B494",
    ("IrrlichtRenderer", "loadGlobalAssets"): "B494",
    ("PlayerCameraComponent", "getFirstPersonVerticalFov"): "B494",
    ("RDPServer", "isAudioReady"): "B494",
    ("RendererContext", "Slash"): "B494",
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
    ("SpellVisualFX", "removeAllForEntity"): "B494",
    ("SpellVisualFX", "removeBuffAura"): "B494",
    ("SpellVisualFX", "setParticleMultiplier"): "B494",
    ("TextBatch", "addText3D"): "B494",
    ("TextBatch", "addTextW"): "B494",
    ("TextureAtlas", "finalizePreload"): "B494",
    ("TextureAtlas", "getAtlasHeight"): "B494",
    ("TextureAtlas", "getAtlasWidth"): "B494",
    ("TextureAtlas", "getGPUMemoryUsage"): "B494",
    ("TextureAtlas", "getPageCount"): "B494",
    ("TextureAtlas", "getTileCount"): "B494",
    ("TextureAtlas", "preloadFromFile"): "B494",
    ("TextureAtlas", "setPageTexture"): "B494",
    ("TextureAtlas", "uploadPreloadedPage"): "B494",
    ("TextureAtlas", "uploadPreloadedPageAsync"): "B494",
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
    ("ZoneShaderManager", "appendAtlasPageTextures"): "B494",
    ("ZoneShaderManager", "atlasTileScale"): "B494",
    ("ZoneShaderManager", "clearPointLights"): "B494",
    ("ZoneShaderManager", "getMaterialTypeLWWindAlphaTestRGBA"): "B494",
    ("ZoneShaderManager", "getMaterialTypeLWWindSolid"): "B494",
    ("ZoneShaderManager", "getMaterialTypeWindAlphaTestRGBA"): "B494",
    ("ZoneShaderManager", "getMaterialTypeWindSolid"): "B494",
    ("ZoneShaderManager", "setAtlasPageTextures"): "B494",
    ("ZoneShaderManager", "setAtlasTileScale"): "B494",
    ("(free)", "calcSharedBankBagSlotId"): "B495",
    ("(free)", "getBagIndex"): "B495",
    ("(free)", "getBankBagIndexAny"): "B495",
    ("(free)", "getClassAbbrev"): "B495",
    ("(free)", "getParentBankSlotAny"): "B495",
    ("(free)", "getWeaponTypeAbbrev"): "B495",
    ("(free)", "isAnyBankBagSlot"): "B495",
    ("(free)", "isAnyBankSlot"): "B495",
    ("(free)", "isContainerParentSlot"): "B495",
    ("(free)", "isContainerSlot"): "B495",
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
    ("InventoryManager", "getCursorMoneyAmount"): "B495",
    ("InventoryManager", "getCursorMoneyType"): "B495",
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
    ("TradeState", "hasPartnerItem"): "B496",
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
    ("Application", "getGameMode"): "B497",
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
    ("TradeManager", "hasPartnerItem"): "B497",
    ("TradeManager", "isPartnerAccepted"): "B497",
    ("TradeManager", "removeItemFromTrade"): "B497",
    ("TradeManager", "setOwnMoney"): "B497",
    ("TradeMoney", "toCopper"): "B497",
    ("WilleqDatabase", "getAnimationSound"): "B497",
    ("ZoneLines", "addZonePoint"): "B497",
    ("ZoneLines", "getZonePoint"): "B497",
    ("ConsoleInputHandler", "isKeyboardMode"): "B498",
    ("ConsoleInputHandler", "isStdinTty"): "B498",
    ("GraphicsInputHandler", "injectLootRequest"): "B498",
    ("GraphicsInputHandler", "injectTargetRequest"): "B498",
    ("NullInputHandler", "injectChatMessage"): "B498",
    ("NullInputHandler", "injectFace"): "B498",
    ("NullInputHandler", "injectFaceEntity"): "B498",
    ("NullInputHandler", "injectHotbar"): "B498",
    ("NullInputHandler", "injectLoot"): "B498",
    ("NullInputHandler", "injectMoveCommand"): "B498",
    ("NullInputHandler", "injectMoveToEntity"): "B498",
    ("NullInputHandler", "injectRawCommand"): "B498",
    ("NullInputHandler", "injectSpellCast"): "B498",
    ("NullInputHandler", "injectStopMovement"): "B498",
    ("NullInputHandler", "injectTarget"): "B498",
    ("AudioManager", "isAutoAttackMusicPlaying"): "B499",
    ("AudioManager", "isUse3DAudio"): "B499",
    ("AudioManager", "isVendorBankMusicPlaying"): "B499",
    ("AudioMixer", "getSfxVolume"): "B499",
    ("EffLoader", "getMp3Index"): "B499",
    ("MusicPlayer", "enableSoftwareRendering"): "B499",
    ("MusicPlayer", "isSoftwareRendering"): "B499",
    ("MusicPlayer", "reinitializeOpenAL"): "B499",
    ("SoundBuffer", "loadFromPCM"): "B499",
    ("WeatherAudio", "getThunderTimer"): "B499",
    ("ZoneSoundEmitter", "getXmiIndex1"): "B499",
    ("ZoneSoundEmitter", "getXmiIndex2"): "B499",
    ("AutomatedMode", "getGameState"): "B500",
    ("AutomatedMode", "getNextCommand"): "B500",
    ("AutomatedMode", "hasPendingCommands"): "B500",
    ("AutomatedMode", "queueCommand"): "B500",
    ("AutomatedMode", "setEventCallback"): "B500",
    ("GraphicalMode", "getGameState"): "B500",
    ("GraphicalMode", "getRendererType"): "B500",
    ("HeadlessMode", "consoleInput"): "B500",
    ("HeadlessMode", "consoleRenderer"): "B500",
    ("HeadlessMode", "getGameState"): "B500",
    ("(free)", "ClampLower"): "B501",
    ("(free)", "MarkHeapStart"): "B501",
    ("(free)", "MeasureHeapUsage"): "B501",
    ("(free)", "PageAlignVectorAligned"): "B501",
    ("(free)", "getCategoryName"): "B501",
    ("LogManager", "GetMutex"): "B501",
    ("PerformanceMetrics", "getElapsedMs"): "B501",
    ("PerformanceMetrics", "getTimings"): "B501",
    ("PerformanceMetrics", "markZoneLoadEnd"): "B501",
    ("PerformanceMetrics", "markZoneLoadStart"): "B501",
    ("PerformanceMetrics", "resetGameplay"): "B501",
    ("Random", "Shuffle"): "B501",
    ("(free)", "GetMovementSpeed"): "B502",
    ("(free)", "IsSwimming"): "B502",
    ("(free)", "WaterStateRef"): "B502",
    ("(free)", "getChannelName"): "B502",
    ("(free)", "getSkillCategoryName"): "B502",
    ("BuffManager", "getSpellDatabase"): "B502",
    ("ChatMessageBuffer", "clearNewMessageFlag"): "B502",
    ("ChatMessageBuffer", "getRecentMessages"): "B502",
    ("ChatMessageBuffer", "hasNewMessages"): "B502",
    ("PlayerComponent", "PerformEmote"): "B502",
    ("SkillData", "getValueString"): "B502",
    ("SkillManager", "getActivatableSkills"): "B502",
    ("SkillManager", "getSkillsByCategory"): "B502",
    ("SkillManager", "isSkillActive"): "B502",
    ("SkillManager", "isSkillReady"): "B502",
    ("SkillManager", "toggleSkill"): "B502",
    ("SpellAnimations", "getAnimationDuration"): "B502",
    ("SpellAnimations", "getSchoolCastAnimation"): "B502",
    ("SpellAnimations", "getTargetAnimation"): "B502",
    ("SpellAnimations", "isValidAnimation"): "B502",
    ("SpellData", "isInstantCast"): "B502",
}

EXEMPT = {k: ("boarded", BOARD_REASONS[v] + " " + v)
          for k, v in EXEMPT_GROUPS.items()}

def load_exempt():
    return EXEMPT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--bucket", default="zero",
                    choices=("zero", "tests", "tools", "all"))
    ap.add_argument("--json")
    ap.add_argument("--check", action="store_true",
                    help="ratchet mode: exit 1 if any zero-caller function is "
                         "missing from EXEMPT, or if any EXEMPT entry is no "
                         "longer zero-caller.  Used by the ZeroCallers ctest "
                         "test.")
    args = ap.parse_args()
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
        found = {(d.owner, d.name) for d in zero}
        exempt = load_exempt()
        unlisted = sorted(found - set(exempt))
        stale = sorted(set(exempt) - found)
        for o, n in unlisted:
            d = candidates[(o, n)]
            print("NEW zero-caller function: %s::%s   %s:%d"
                  % (o, n, d.rel, d.line))
        for o, n in stale:
            print("STALE exemption (no longer zero-caller): %s::%s -- %s"
                  % (o, n, exempt[(o, n)][1]))
        if unlisted or stale:
            print("")
            print("%d unlisted, %d stale." % (len(unlisted), len(stale)))
            if unlisted:
                print("A function declared and never called is this project's "
                      "dominant defect class (T366/T414/T442/T448/T452).\n"
                      "Wire it, delete it, or add it to EXEMPT in this script "
                      "with a category and a reason.")
            if stale:
                print("An exemption whose function now has a caller must be "
                      "removed from EXEMPT -- the table is not allowed to "
                      "outlive its rows.")
            return 1
        print("zero-caller census clean: %d functions, all %d classified in "
              "EXEMPT." % (len(found), len(exempt)))
        return 0

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
