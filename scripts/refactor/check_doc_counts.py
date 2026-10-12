#!/usr/bin/env python3
"""Assert the hand-maintained COUNTS in the orientation docs against the tree.

T373 (board B130). `DocClaims` (check_doc_claims.py) is a NAME table: it
catches a doc that still names a deleted symbol. It is blind to the other
half of doc rot T371 measured -- the NUMBERS. Every one of the six counts
T371 checked had drifted, and each was written with a "counted <date>" or a
commit hash, so the staleness is invisible without re-counting by hand.

This is the regression net for that. A table of rows, each

    (a measuring function over the TREE,
     one or more (doc, regex-with-one-capture-group) sites that state it)

The capture group is the stated number, in digits ("**143**") or as an
English word ("eleven component directories"). Every site must match at
least once -- a doc that quietly loses the sentence FAILS rather than
silently passing (the T37 rule) -- and every match must equal the measured
value.

Two counts are deliberately NOT netted here: the registered-test total and
the `owner_` census. Both drift with ordinary work, so T371 replaced the
number with the command that measures it. For those, this test asserts the
opposite thing: that the doc still names the COMMAND (the NAMED_COMMAND
table below). Netting a number that is expected to change every unit would
just make the suite fail on unrelated work.

Usage:
    check_doc_counts.py [--repo REPO_ROOT]

Exit 0 if every stated count matches the tree (always printing what was
checked); exit 1 on any mismatch, missing site, or unparseable source.
"""
import argparse
import os
import re
import sys

NUMBER_WORDS = {
    "zero": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5,
    "six": 6, "seven": 7, "eight": 8, "nine": 9, "ten": 10,
    "eleven": 11, "twelve": 12, "thirteen": 13, "fourteen": 14,
    "fifteen": 15, "sixteen": 16, "seventeen": 17, "eighteen": 18,
    "nineteen": 19, "twenty": 20, "thirty": 30, "forty": 40, "fifty": 50,
}


class SourceError(Exception):
    pass


def read(repo, rel):
    path = os.path.join(repo, rel)
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError as exc:
        raise SourceError("%s unreadable (%s)" % (rel, exc))


def strip_comments(text):
    """Remove // line comments and /* */ blocks (no string literals here)."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def split_top_level(body):
    """Split a comma list, ignoring commas nested in <>, () or {}."""
    parts, depth, cur = [], 0, []
    for ch in body:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def enum_body(text, name):
    m = re.search(r"\benum\s+class\s+%s\b[^{]*\{" % re.escape(name), text)
    if not m:
        raise SourceError("enum class %s not found" % name)
    start = m.end()
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
    raise SourceError("enum class %s is unterminated" % name)


SENTINELS = {"Count", "COUNT", "kCount", "Max", "MAX", "None_"}


def count_enumerators(repo, rel, name):
    """Enumerators in `enum class <name>`, excluding a trailing sentinel."""
    body = enum_body(strip_comments(read(repo, rel)), name)
    names = []
    for part in split_top_level(body):
        ident = part.split("=")[0].strip()
        if ident:
            names.append(ident)
    if names and names[-1] in SENTINELS:
        names.pop()
    if not names:
        raise SourceError("enum class %s in %s parsed to zero enumerators"
                          % (name, rel))
    return len(names)


def count_variant_alternatives(repo, rel, alias):
    """Alternatives in `using <alias> = std::variant< ... >;`."""
    text = strip_comments(read(repo, rel))
    m = re.search(r"\busing\s+%s\s*=\s*std::variant\s*<" % re.escape(alias),
                  text)
    if not m:
        raise SourceError("using %s = std::variant<...> not found in %s"
                          % (alias, rel))
    start = m.end()
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "<":
            depth += 1
        elif text[i] == ">":
            depth -= 1
            if depth == 0:
                alts = split_top_level(text[start:i])
                if not alts:
                    raise SourceError("%s parsed to zero alternatives" % alias)
                return len(alts)
    raise SourceError("%s variant is unterminated in %s" % (alias, rel))


def count_calls(repo, rel, literal):
    """Occurrences of a call literal in a TU, comments stripped."""
    text = strip_comments(read(repo, rel))
    n = text.count(literal)
    if n == 0:
        raise SourceError("'%s' does not occur in %s" % (literal, rel))
    return n


def count_calls_under(repo, dirs, literal):
    """Occurrences of a call literal in every *.cpp/*.h under DIRS, comments
    stripped (B1039: a helper whose call sites span several TUs)."""
    n = 0
    for d in dirs:
        for root, _subdirs, files in os.walk(os.path.join(repo, d)):
            for name in files:
                if name.endswith((".cpp", ".h")):
                    rel = os.path.relpath(os.path.join(root, name), repo)
                    n += strip_comments(read(repo, rel)).count(literal)
    if n == 0:
        raise SourceError("'%s' does not occur under %s" % (literal, ", ".join(dirs)))
    return n


def count_step_functions(repo, rel, prefix):
    """Distinct `<prefix>N_Name(` function names declared in a header."""
    text = strip_comments(read(repo, rel))
    names = set(re.findall(r"\b(%s[A-Za-z0-9_]*)\s*\(" % re.escape(prefix),
                           text))
    if not names:
        raise SourceError("no %s* functions in %s" % (prefix, rel))
    return len(names)


def count_family_components(repo, family):
    """`<Name>Component` headers under include/client/<family>/*/ .

    One component per directory is Batch K's and Batch L's shape: a
    `*_component.h` inside its own family directory.
    """
    root = os.path.join(repo, "include", "client", family) if family \
        else os.path.join(repo, "include", "client")
    if not os.path.isdir(root):
        raise SourceError("no such directory: include/client/%s" % family)
    found = []
    for entry in sorted(os.listdir(root)):
        sub = os.path.join(root, entry)
        if not os.path.isdir(sub):
            continue
        for name in sorted(os.listdir(sub)):
            if name.endswith("_component.h"):
                found.append(os.path.join(entry, name))
    if not found:
        raise SourceError("no *_component.h under include/client/%s" % family)
    return len(found)


def count_test_cmake_fragments(repo):
    """The per-domain fragments tests/CMakeLists.txt include()s.

    T371 added hygiene.cmake (FileSizeCeiling, DocCitations, DocClaims) and
    T373 put DocCounts in it; the module map still listed the pre-T371
    eighteen and omitted hygiene by name, so the count and the list were
    both wrong (T388).
    """
    root = os.path.join(repo, "tests", "cmake")
    if not os.path.isdir(root):
        raise SourceError("no such directory: tests/cmake")
    frags = [n for n in os.listdir(root) if n.endswith(".cmake")]
    if not frags:
        raise SourceError("no *.cmake under tests/cmake")
    return len(frags)



def count_hygiene_tests(repo):
    """The tests registered in tests/cmake/hygiene.cmake.

    T450 added DbReproducible -- the third derived asset's pin -- and found
    the module map still listing the four T371/T373 tests, three units after
    TexturesReproducible, PackReproducible and WriteOnlyFields had joined
    them. The hygiene fragment is the tree's own ratchet drawer; a doc that
    under-states what is in it under-states what a red run is telling you.
    """
    rel = os.path.join("tests", "cmake", "hygiene.cmake")
    text = read(repo, rel)
    names = re.findall(r"ADD_TEST\s*\(\s*NAME\s+([A-Za-z_]\w*)",
                       text, re.IGNORECASE)
    if not names:
        raise SourceError("no ADD_TEST(NAME ...) in %s" % rel)
    return len(set(names))


def count_constexpr_int(repo, rel, name):
    """The integer value of a `constexpr[ int] <name> = <digits>;` in a header.

    T444: several counts a doc states are already single constants in the
    tree (kVolumeSteps, kSheetCount, ...). Reading the constant is a stronger
    net than re-deriving it, because the constant is what the code uses.
    """
    text = strip_comments(read(repo, rel))
    consts = {}
    for m in re.finditer(
            r"\bconstexpr\s+(?:[A-Za-z_][\w:]*\s+)?([A-Za-z_]\w*)\s*=\s*"
            r"([^;{]+);", text):
        consts[m.group(1)] = m.group(2).strip()
    if name not in consts:
        raise SourceError("no `constexpr ... %s = ...;` in %s" % (name, rel))

    # The RHS is often an expression over the header's own constants
    # (kPopulatedIconCount = 33 * kCellsPerSheet - 1 + 13). Resolve names,
    # then evaluate the arithmetic -- and refuse anything that is not
    # arithmetic, so this can never run header text as code.
    def resolve(expr, seen):
        expr = re.sub(r"\b(\d+)[uUlL]+\b", r"\1", expr)
        for ident in sorted(set(re.findall(r"[A-Za-z_]\w*", expr)),
                            key=len, reverse=True):
            if ident in seen:
                raise SourceError("%s is circular in %s" % (ident, rel))
            if ident not in consts:
                raise SourceError("%s = %s in %s names %s, which is not a "
                                  "constexpr in the same header"
                                  % (name, consts[name], rel, ident))
            expr = re.sub(r"\b%s\b" % re.escape(ident),
                          "(%s)" % resolve(consts[ident], seen | {ident}),
                          expr)
        return expr

    expr = resolve(consts[name], {name})
    if not re.fullmatch(r"[0-9+\-*/() ]+", expr):
        raise SourceError("%s in %s resolves to %r, which is not integer "
                          "arithmetic" % (name, rel, expr))
    try:
        value = eval(expr, {"__builtins__": {}}, {})  # arithmetic only
    except Exception as exc:
        raise SourceError("%s in %s: %s" % (name, rel, exc))
    return int(value)


def count_array_rows(repo, rel, array, marker):
    """Occurrences of `marker` inside `<array>[] = { ... };`.

    One marker per row is the shape of the constexpr control tables (T434's
    OptionsLayout::kControls uses `Setting::` exactly once per control), and
    counting the marker is brace-safe where counting `{` is not: the rows
    nest rect initialisers.
    """
    text = strip_comments(read(repo, rel))
    m = re.search(r"\b%s\s*\[\s*\]\s*=\s*\{" % re.escape(array), text)
    if not m:
        raise SourceError("no `%s[] = {` in %s" % (array, rel))
    start = m.end()
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                n = text[start:i].count(marker)
                if n == 0:
                    raise SourceError("'%s' does not occur inside %s[] in %s"
                                      % (marker, array, rel))
                return n
    raise SourceError("%s[] is unterminated in %s" % (array, rel))


def count_shipped_bmps(repo, only_32bit=False):
    """The shipped data/textures/*.bmp files, optionally only the 32-bit ones.

    T444: root CLAUDE.md states both the total and the 32-bit share, and both
    had drifted (T417 and T430 each added a sheet after the sentences were
    written). Bits-per-pixel is the BMP header's 16-bit LE field at offset 28.
    """
    import struct
    root = os.path.join(repo, "data", "textures")
    if not os.path.isdir(root):
        raise SourceError("no such directory: data/textures")
    names = sorted(n for n in os.listdir(root) if n.endswith(".bmp"))
    if not names:
        raise SourceError("no *.bmp under data/textures")
    if not only_32bit:
        return len(names)
    n32 = 0
    for name in names:
        with open(os.path.join(root, name), "rb") as fh:
            head = fh.read(30)
        if len(head) < 30:
            raise SourceError("data/textures/%s is not a BMP header" % name)
        if struct.unpack_from("<H", head, 28)[0] == 32:
            n32 += 1
    return n32


# --- the table ------------------------------------------------------------
# (label, measurer, why-it-drifts, [(doc, regex with ONE capture group), ...])
#
# Regexes run over the WHOLE file text (not line by line) because several of
# these sentences wrap across a line break; the line number is recovered from
# the match offset.
ROWS = [
    ("InputAction enumerators",
     lambda r: count_enumerators(
         r, "include/client/input/input_handler.h", "InputAction"),
     "T365/T368 deleted seven consumer-less enumerators (B090/B100)",
     [("src/client/input/CLAUDE.md",
       r"`InputAction`\s+enum\s+\(\*\*(\w+)\*\*")]),

    ("HotkeyAction enumerators",
     lambda r: count_enumerators(
         r, "include/client/input/hotkey_manager.h", "HotkeyAction"),
     "the key table grows with every UI panel",
     [("src/client/input/CLAUDE.md",
       r"`HotkeyAction`\s+enum\s+\(\*\*(\w+)\*\*")]),

    ("registerCommand({ calls in registry 2",
     lambda r: count_calls(
         r, "src/client/action/command_processor_registration.cpp",
         "registerCommand({"),
     "T347 inserted /cleartarget; every new slash command moves it",
     [("src/client/input/CLAUDE.md",
       r"\*\*(\w+)\*\*\s+`registerCommand\(\{"),
      ("include/client/graphics/ui/CLAUDE.md",
       r"\*\*(\w+)\*\*\s+`registerCommand\(\{")]),

    # T439 (board B432): four docs stated "100 event types" and a new
    # enumerator would have left all four stale silently. This is exactly the
    # drift T373 built this file for, and the enum was simply not on the list.
    ("GameEventType enumerators",
     lambda r: count_enumerators(
         r, "include/client/state/event_types.h", "GameEventType"),
     "every new panel or feed adds one; T439 added VolumesChanged, B892 LevitationChanged",
     [("CLAUDE.md",
       r"EventBus,\s+(\w+)\s+event types"),
      ("CLAUDE.md",
       r"switch over the\s+(\w+)\s+`GameEventType` enumerators"),
      ("CLAUDE.md",
       r"—\s+(\w+)\s+event types covering all game state changes"),
      ("include/client/state/CLAUDE.md",
       r"\*\*(\w+)\s+event types\*\*"),
      ("include/client/state/CLAUDE.md",
       r"has a case for all\s+(\w+)\s+in its switch")]),

    ("RendererIntent variant alternatives",
     lambda r: count_variant_alternatives(
         r, "include/client/events/renderer_intents.h", "RendererIntent"),
     "T328 Leg B added PlaySoundIntent; T268 deleted one",
     [("include/client/bridge/CLAUDE.md",
       r"##\s+Intent\s+Types\s+\((\w+)\)")]),

    ("loadZoneStep* functions",
     lambda r: count_step_functions(
         r, "include/client/graphics/zone_load/zone_load_component.h",
         "loadZoneStep"),
     "8a-8f and 12b were split out of steps 8 and 12; the numbering lies",
     [("src/client/graphics/CLAUDE.md",
       r"and its\s+(\w+)\s+steps"),
      ("src/client/graphics/CLAUDE.md",
       r"It runs \*\*(\w+)\*\*\s+`loadZoneStepN_\*\(\)`\s+bodies")]),

    ("renderer components (include/client/graphics/*/*_component.h)",
     lambda r: count_family_components(r, "graphics"),
     "Batch L extracted them one unit at a time",
     [("CLAUDE.md",
       r"\+\s+(\w+)\s+component directories"),
      ("CLAUDE.md",
       r"everything else was extracted into\s+(\w+)\s"),
      ("CLAUDE.md",
       r"in the\s+(\w+)\s+renderer components"),
      ("docs/module_map.md",
       r"plus the\s+(\w+)\s+components Batch L extracted"),
      ("docs/module_map.md",
       r"the\s+(\w+)\s+component constructions"),
      ("docs/module_map.md",
       r"residue across the\s+(\w+)\s+components")]),

    ("EverQuest components (include/client/*/*_component.h)",
     lambda r: count_family_components(r, ""),
     "Batch K extracted them one family at a time",
     [("docs/module_map.md",
       r"Batch K extracted\s+(\w+)\s+components")]),

    # --- T444 rows. Each of these was corrected BY HAND in T444 because the
    # doc had drifted from the tree; the rule is that a hand-corrected count
    # gets a row here so the next drift is a red instead of a re-read.

    ("VisionType enumerators",
     lambda r: count_enumerators(
         r, "include/client/graphics/vision_type.h", "VisionType"),
     "T428 appended FroglokVision as a fourth level and closed B060; "
     "docs/decisions.md had said Froglok was left on Normal",
     [("docs/decisions.md",
       r"`VisionType`\s*\n?now has\s+(\w+)\s+enumerators")]),

    ("audio volume steps (kVolumeSteps)",
     lambda r: count_constexpr_int(
         r, "include/client/audio/volume_steps.h", "kVolumeSteps"),
     "T439 introduced the oracle's absolute 0..N slider unit; the UI, the "
     "two intents and the [Options] ini keys all carry it",
     [("src/client/audio/CLAUDE.md",
       r"`eqt::audio::kVolumeSteps\s*=\s*(\d+)`"),
      ("CLAUDE.md",
       r"step 0\.\.(\d+)\s*\n?\(`include/client/audio/volume_steps\.h`")]),

    ("Options window control rows (OptionsLayout::kControls)",
     lambda r: count_array_rows(
         r, "include/client/graphics/ui/options_layout.h",
         "kControls", "Setting::"),
     "T434 quoted every control EQUI_OptionsWindow.xml declares; wiring one "
     "more moves the greyed remainder with it",
     [("README.md",
       r"\*\*(\w+)\s+control rows\*\*"),
      ("docs/future/ui_fixes.md",
       r"\*\*(\w+)\s+control rows\*\*")]),

    ("item icon source sheets (kSheetCount)",
     lambda r: count_constexpr_int(
         r, "include/client/graphics/ui/item_icon_atlas.h", "kSheetCount"),
     "T430 quoted EQUI_Animations.xml's A_DragItem list",
     [("CLAUDE.md",
       r"\*\*(\w+)\s+sheets of 6x6 40x40 cells"),
      ("include/client/graphics/ui/CLAUDE.md",
       r"The layout — (\w+) sheets of 6x6 40x40 cells")]),

    ("item icon cells that carry art (kPopulatedIconCount)",
     lambda r: count_constexpr_int(
         r, "include/client/graphics/ui/item_icon_atlas.h",
         "kPopulatedIconCount"),
     "dragitem34 stops after thirteen and dragitem21 has an empty cell "
     "mid-grid; a re-lift of the art would move this",
     [("CLAUDE.md",
       r"\n(\w+) of the 1224 cells carry art")]),

    ("shipped data/textures BMPs",
     count_shipped_bmps,
     "T417 added the spell sprite atlas and T430 the two item icon sheets; "
     "four sentences in CLAUDE.md stated the pre-T417 count",
     [("CLAUDE.md",
       r"`data/textures/\*\.bmp`\s+\((\d+)\s+files\)"),
      ("CLAUDE.md",
       r"every one of\s*\n?the\s+(\d+)\s+shipped BMPs is now byte-reproducible"),
      ("CLAUDE.md",
       r"The pack's inputs are the\s+(\d+)\s+BMPs")]),

    ("shipped 32-bit BMPs (alpha in band)",
     lambda r: count_shipped_bmps(r, only_32bit=True),
     "T406 wrote '60 of the 64'; T417 and T430 each added a 32-bit sheet",
     [("CLAUDE.md",
       r"\*\*(\d+) of the 67 shipped 32-bit with their")]),

    ("hygiene ctest tests",
     count_hygiene_tests,
     "T450 added DbReproducible; the module map still named only the four "
     "T371/T373 tests, three units after TexturesReproducible, "
     "PackReproducible and WriteOnlyFields joined them",
     [("docs/module_map.md",
       r"`hygiene` \((\w+) tests:")]),

    ("tests/cmake per-domain fragments",
     count_test_cmake_fragments,
     "T371/T373 added hygiene.cmake; a new test domain adds another",
     [("docs/module_map.md",
       r"`include\(\)`s \*\*(\w+)\*\* per-domain")]),

    ("eqt::setCurrentThreadName call sites",
     lambda r: count_calls_under(r, ["src", "include"],
                                 "eqt::setCurrentThreadName("),
     "B1039 routed the T504 thread names through one helper; a new named "
     "thread adds one",
     [("docs/macos.md",
       r"The tree has the\s+(\w+)\s+thread names set this way")]),
]

# Directories whose every source file must be NAMED in a doc (T444).
#
# DocCounts nets NUMBERS and DocClaims nets retired NAMES. Neither catches the
# third shape: a doc that is a MAP, whose defect is a file it does not mention.
# T444 found docs/module_map.md -- the doc CLAUDE.md tells a new agent to
# consult "instead of scanning" -- missing two TUs, and both had been there for
# several units: src/client/graphics/frame/renderer_frame_options.cpp (T434)
# and src/client/graphics/player/ground_query.cpp (T437/T440). A count would
# not have found either, because the map states no count.
#
# (label, directory relative to the repo, suffix, doc, why)
COVERAGE = [
    ("renderer component TUs", "src/client/graphics/frame", ".cpp",
     "docs/module_map.md",
     "FrameComponent's TU list and the renderer TU table both enumerate "
     "these by name; T434 added one and neither learned it"),
    ("renderer component TUs", "src/client/graphics/player", ".cpp",
     "docs/module_map.md",
     "PlayerCameraComponent's TU list; T437 added ground_query.cpp"),
    ("renderer component TUs", "src/client/graphics/zone_load", ".cpp",
     "docs/module_map.md", "ZoneLoadComponent's five TUs"),
    ("renderer component TUs", "src/client/graphics/zone_lifecycle", ".cpp",
     "docs/module_map.md", "ZoneLifecycleComponent's three TUs"),
    ("renderer component TUs", "src/client/graphics/zone_stream", ".cpp",
     "docs/module_map.md", "ZoneStreamComponent's two TUs"),
    ("renderer component TUs", "src/client/graphics/overlays", ".cpp",
     "docs/module_map.md", "OverlaysComponent's two TUs"),
    ("renderer component TUs", "src/client/graphics/diagnostics", ".cpp",
     "docs/module_map.md", "DiagnosticsComponent's two TUs"),
    ("renderer component TUs", "src/client/graphics/renderer", ".cpp",
     "docs/module_map.md", "what is left of class IrrlichtRenderer"),
]


# Counts that MUST NOT be pinned to a number: they change with ordinary work.
# T371 replaced each with the command that measures it; assert the command
# survived rather than netting the number.
NAMED_COMMAND = [
    ("README.md", "ctest --test-dir build -N | tail -1",
     "the registered-test total; do not restore a number here"),
    ("refactor/BOARD.md", "ctest --test-dir <slot>/build -N | tail -1",
     "the board header's suite line; do not restore a number here"),
    ("refactor/BOARD.md", "git -C <slot> log --oneline -1",
     "the board header's tip line (T445, B458): the hash sat stale for "
     "roughly sixty units; do not restore one here"),
    ("CLAUDE.md", "re-measure, do not",
     "the owner_ census in the IrrlichtRenderer section"),
    ("src/client/graphics/CLAUDE.md", "re-measure rather than",
     "the owner_ census in the Batch L component table"),
]


# Patterns that must NOT appear. A not-a-number assertion only proves the
# measuring command is still SOMEWHERE in the file; T388 found README.md
# carrying the measuring command in its Tests section while a stale "2746
# registered tests" sat in its Building block twelve lines earlier, and
# DocCounts was green throughout. A doc that has been told to name the
# command must not also restate the number.
FORBIDDEN = [
    ("refactor/BOARD.md",
     r"\*\*Tree confirmed at:\*\*\s*`[0-9a-f]{7,40}`",
     "the board header's tip hash was replaced by its measuring command "
     "(T445, B458); a hash stamped once is stale by the next merge, and this "
     "one sat unchanged for roughly sixty units"),
    ("README.md",
     r"\(\s*[\d][\d,]{2,}\s+registered tests\s*\)",
     "the registered-test total was replaced by its measuring command "
     "(T371); this is a second site restating it as a literal (T388)"),
]


def parse_number(token):
    if token.isdigit():
        return int(token)
    key = token.lower().replace(",", "")
    if key in NUMBER_WORDS:
        return NUMBER_WORDS[key]
    return None


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)

    failures = []
    sites = 0
    docs = set()
    measured = []

    for label, measure, why, targets in ROWS:
        try:
            expected = measure(repo)
        except SourceError as exc:
            failures.append("MEASURE  %s: %s -- the measuring function could "
                            "not read the tree; fix the parser or the source."
                            % (label, exc))
            continue
        measured.append((label, expected))
        for rel, pattern in targets:
            docs.add(rel)
            try:
                text = read(repo, rel)
            except SourceError as exc:
                failures.append("MISSING  %s: %s" % (label, exc))
                continue
            rx = re.compile(pattern)
            hits = list(rx.finditer(text))
            if not hits:
                failures.append(
                    "MISSING  %s: no sentence in %s matches /%s/ -- the doc "
                    "site this count is netted at is gone. Restore the "
                    "sentence or delete the row from check_doc_counts.py "
                    "(nothing was checked, this is not a pass)."
                    % (label, rel, pattern))
                continue
            sites += 1
            for m in hits:
                stated = parse_number(m.group(1))
                n = line_of(text, m.start())
                if stated is None:
                    failures.append(
                        "FAIL  %s:%d states '%s' for %s, which is not a "
                        "number this checker understands (digits or an "
                        "English number word). Measured: %d."
                        % (rel, n, m.group(1), label, expected))
                elif stated != expected:
                    failures.append(
                        "FAIL  %s:%d states %d %s; the tree has %d. "
                        "(%s.) Correct the doc -- the measurement is "
                        "authoritative."
                        % (rel, n, stated, label, expected, why))

    for label, subdir, suffix, rel, why in COVERAGE:
        docs.add(rel)
        root = os.path.join(repo, subdir)
        if not os.path.isdir(root):
            failures.append("MEASURE  %s: no such directory: %s" %
                            (label, subdir))
            continue
        names = sorted(n for n in os.listdir(root) if n.endswith(suffix))
        if not names:
            failures.append("MEASURE  %s: no *%s under %s"
                            % (label, suffix, subdir))
            continue
        try:
            text = read(repo, rel)
        except SourceError as exc:
            failures.append("MISSING  %s" % exc)
            continue
        sites += 1
        for name in names:
            if name not in text:
                failures.append(
                    "FAIL  %s does not name %s/%s -- %s. The map must list "
                    "every TU in the directory, or an agent told to consult "
                    "it instead of scanning will miss this file."
                    % (rel, subdir, name, why))

    for rel, phrase, why in NAMED_COMMAND:
        docs.add(rel)
        try:
            text = read(repo, rel)
        except SourceError as exc:
            failures.append("MISSING  %s" % exc)
            continue
        sites += 1
        if phrase not in text:
            failures.append(
                "FAIL  %s no longer contains '%s' -- %s. This count is "
                "deliberately NOT netted because it drifts every unit; the "
                "doc must name the measuring command instead of a number."
                % (rel, phrase, why))

    for rel, pattern, why in FORBIDDEN:
        docs.add(rel)
        try:
            text = read(repo, rel)
        except SourceError as exc:
            failures.append("MISSING  %s" % exc)
            continue
        sites += 1
        for m in re.finditer(pattern, text):
            failures.append(
                "FAIL  %s:%d restates a count that must not be pinned: "
                "'%s' -- %s. Replace it with the measuring command."
                % (rel, line_of(text, m.start()), m.group(0).strip(), why))

    print("DocCounts: %d counts netted over %d doc sites in %d docs, "
          "%d not-a-number assertions, %d forbidden-restatement assertions, "
          "%d directory-coverage assertions"
          % (len(ROWS),
             sites - len(NAMED_COMMAND) - len(FORBIDDEN) - len(COVERAGE),
             len(docs), len(NAMED_COMMAND), len(FORBIDDEN), len(COVERAGE)))

    for label, value in measured:
        print("  measured %-58s %d" % (label, value))

    if not failures:
        print("OK: every stated count matches the tree.")
        return 0
    for f in failures:
        print(f)
    print("DocCounts: %d failure(s)." % len(failures))
    return 1


if __name__ == "__main__":
    sys.exit(main())
