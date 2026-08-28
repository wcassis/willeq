#!/usr/bin/env python3
"""T304 Leg 2: generate the INVENTED placeholder-cube tint column.

These values are NOT sourced from EverQuest data. Will ruled on 2026-08-28
that inventing them is acceptable; the bar is "plausible and distinguishable".
Reached only when no model could be built for a race (T213).
"""
import json, collections
from pathlib import Path

# Repo-relative so this works from any checkout/worktree.
SRC = Path(__file__).resolve().parent.parent / 'config' / 'race_models.json'

# Base tints, keyed on what the row's own NAME says (never on the pre-JSON
# numbering). First matching keyword wins; order matters.
KEYWORDS = [
    # explicit per-name anchors first
    ('invisible man',   (0,   0,   0,   0)),
    ('teleport man',    (0,   0,   0,   0)),
    ('ghost ship',      (150, 170, 190, 210)),
    ('ghost dragon',    (150, 200, 160, 200)),
    ('ghost dwarf',     (150, 200, 200, 255)),
    ('erudite ghost',   (150, 200, 200, 255)),
    ('ghost',           (150, 200, 200, 255)),
    ('spectral',        (150, 170, 210, 220)),
    ('spectre',         (150, 180, 190, 230)),
    ('will-o-wisp',     (170, 230, 240, 170)),
    ('wisp',            (170, 230, 240, 170)),
    # skeletal / undead
    ('skeleton',        (255, 220, 220, 200)),
    ('lich',            (255, 190, 200, 190)),
    ('zombie',          (255, 130, 150, 110)),
    ('reanimated',      (255, 190, 180, 160)),
    ('ghoul',           (255, 150, 160, 130)),
    ('vampire',         (255, 120, 60,  70)),
    ('mummy',           (255, 180, 165, 130)),
    # dragons and drakes
    ('prismatic dragon',(255, 200, 120, 220)),
    ('water dragon',    (255, 60,  130, 190)),
    ('lava dragon',     (255, 200, 70,  50)),
    ('velious dragon',  (255, 160, 190, 220)),
    ('black/white',     (255, 120, 120, 130)),
    ('dragon',          (255, 190, 60,  60)),
    ('drake',           (255, 170, 90,  70)),
    ('wyvern',          (255, 150, 110, 60)),
    ('wurm',            (255, 140, 120, 80)),
    ('trakanon',        (255, 110, 150, 70)),
    # gods / named powers
    ('cazic',           (255, 90,  50,  110)),
    ('innoruuk',        (255, 120, 40,  90)),
    ('rallos',          (255, 130, 90,  60)),
    ('tunare',          (255, 110, 180, 110)),
    ('solusek',         (255, 220, 110, 50)),
    ('erollisi',        (255, 220, 170, 190)),
    ('tribunal',        (255, 150, 150, 170)),
    ('bristlebane',     (255, 200, 190, 110)),
    ('venril',          (255, 140, 150, 120)),
    ('xalgoz',          (255, 120, 140, 150)),
    # playable / humanoid citizens
    ('human',           (255, 200, 180, 160)),
    ('barbarian',       (255, 180, 160, 140)),
    ('erudite',         (255, 160, 140, 120)),
    ('wood elf',        (255, 140, 180, 140)),
    ('high elf',        (255, 220, 220, 200)),
    ('dark elf',        (255, 80,  80,  100)),
    ('half elf',        (255, 180, 180, 160)),
    ('elf',             (255, 170, 180, 170)),
    ('dwarf',           (255, 180, 140, 100)),
    ('troll',           (255, 100, 140, 100)),
    ('ogre',            (255, 140, 120, 100)),
    ('halfling',        (255, 180, 160, 140)),
    ('gnome',           (255, 160, 160, 180)),
    ('iksar',           (255, 100, 140, 100)),
    ('vah shir',        (255, 180, 160, 120)),
    ('froglok',         (255, 80,  160, 80)),
    ('kerran',          (255, 190, 150, 90)),
    ('sarnak',          (255, 150, 130, 70)),
    ('coldain',         (255, 190, 170, 150)),
    ('othmir',          (255, 170, 150, 120)),
    ('ulthork',         (255, 150, 160, 170)),
    ('citizen',         (255, 190, 175, 155)),
    ('guard',           (255, 160, 165, 180)),
    ('beggar',          (255, 150, 140, 120)),
    ('dervish',         (255, 170, 150, 130)),
    ('hag',             (255, 130, 140, 120)),
    ('siren',           (255, 120, 180, 190)),
    ('mermaid',         (255, 110, 175, 180)),
    # giants and big humanoids
    ('giant bat',       (255, 70,  48,  48)),
    ('felguard',        (255, 170, 140, 100)),
    ('frost giant',     (255, 170, 200, 220)),
    ('storm giant',     (255, 140, 160, 200)),
    ('forest giant',    (255, 120, 150, 100)),
    ('giant',           (255, 160, 145, 120)),
    ('minotaur',        (255, 130, 100, 70)),
    ('centaur',         (255, 160, 130, 90)),
    ('gorgon',          (255, 120, 150, 120)),
    ('sphinx',          (255, 200, 180, 110)),
    ('manticore',       (255, 170, 120, 80)),
    ('harpy',           (255, 180, 160, 190)),
    ('drachnid',        (255, 90,  70,  90)),
    ('drixie',          (255, 200, 150, 200)),
    ('felguard',        (255, 170, 140, 100)),
    ('holgresh',        (255, 140, 130, 110)),
    ('geonid',          (255, 130, 130, 140)),
    ('yakkar',          (255, 160, 150, 130)),
    ('abhorrent',       (255, 110, 90,  110)),
    ('amygdalan',       (255, 130, 110, 130)),
    ('burynai',         (255, 160, 140, 90)),
    ('drolvarg',        (255, 130, 110, 80)),
    ('yeti',            (255, 220, 225, 230)),
    ('werewolf',        (255, 120, 100, 80)),
    ('gnoll',           (255, 140, 100, 60)),
    ('goblin',          (255, 100, 140, 60)),
    ('kobold',          (255, 120, 110, 70)),
    ('orc',             (255, 90,  120, 70)),
    ('imp',             (255, 170, 80,  70)),
    ('efreeti',         (255, 210, 120, 60)),
    ('djinn',           (255, 130, 170, 210)),
    ('brownie',         (255, 150, 120, 80)),
    ('pixie',           (255, 210, 200, 230)),
    ('fairy',           (255, 220, 210, 240)),
    ('bixie',           (255, 210, 190, 70)),
    ('ratman',          (255, 120, 100, 80)),
    ('lizard man',      (255, 100, 150, 110)),
    ('gorilla',         (255, 90,  80,  70)),
    ('bloodgills',      (255, 110, 150, 140)),
    ('fungusman',       (255, 130, 150, 110)),
    ('scarecrow',       (255, 190, 170, 100)),
    ('treant',          (255, 110, 130, 80)),
    ('succulent',       (255, 100, 160, 100)),
    ('man-eating plant',(255, 90,  150, 90)),
    ('goo',             (255, 140, 170, 120)),
    ('gelatinous cube', (170, 180, 210, 170)),
    ('gasbag',          (200, 190, 180, 130)),
    ('evil eye',        (255, 170, 110, 140)),
    ('eye of zomm',     (200, 190, 200, 220)),
    ('tentacle terror', (255, 120, 90,  130)),
    ('devourer',        (255, 110, 100, 90)),
    ('mimic',           (255, 140, 110, 70)),
    ('totem',           (255, 160, 130, 90)),
    ('enchanted armor', (255, 170, 180, 200)),
    ('clockwork',       (255, 180, 165, 120)),
    ('golem',           (255, 150, 140, 130)),
    ('gargoyle',        (255, 120, 120, 125)),
    ('griffin',         (255, 190, 160, 90)),
    ('cockatrice',      (255, 190, 140, 110)),
    ('unicorn',         (255, 235, 235, 240)),
    ('pegasus',         (255, 225, 230, 240)),
    ('elemental',       (255, 150, 190, 210)),
    # animals
    ('sabertooth',      (255, 190, 170, 110)),
    ('tiger',           (255, 210, 150, 70)),
    ('lion',            (255, 200, 165, 100)),
    ('puma',            (255, 175, 150, 110)),
    ('dire wolf',       (255, 110, 115, 125)),
    ('wolf',            (255, 140, 135, 120)),
    ('bear',            (255, 130, 100, 70)),
    ('boar',            (255, 120, 100, 80)),
    ('mammoth',         (255, 140, 115, 90)),
    ('brontotherium',   (255, 130, 120, 100)),
    ('rhinoceros',      (255, 135, 135, 130)),
    ('walrus',          (255, 155, 140, 125)),
    ('alligator',       (255, 100, 130, 90)),
    ('armadillo',       (255, 165, 145, 110)),
    ('skunk',           (255, 60,  60,  70)),
    ('snow rabbit',     (255, 230, 232, 235)),
    ('raptor',          (255, 130, 160, 90)),
    ('scorpion',        (255, 120, 100, 60)),
    ('spider',          (255, 70,  65,  70)),
    ('beetle',          (255, 80,  60,  40)),
    ('wasp',            (255, 200, 175, 60)),
    ('mosquito',        (255, 110, 105, 95)),
    ('leech',           (255, 110, 80,  90)),
    ('snake',           (255, 80,  100, 60)),
    ('bat',             (255, 60,  40,  40)),
    ('rat',             (255, 100, 80,  60)),
    ('aviak',           (255, 170, 190, 120)),
    ('seahorse',        (255, 140, 180, 190)),
    ('sea turtle',      (255, 100, 140, 110)),
    ('shark',           (255, 110, 130, 150)),
    ('piranha',         (255, 130, 140, 120)),
    ('swordfish',       (255, 110, 140, 170)),
    ('eel',             (255, 90,  120, 110)),
    ('kedge',           (255, 100, 150, 150)),
    ('fish',            (255, 100, 140, 180)),
    ('tadpole',         (255, 110, 170, 110)),
    # vehicles / scenery
    ('ship',            (255, 130, 110, 85)),
    ('boat',            (255, 130, 110, 85)),
    ('launch',          (255, 140, 120, 95)),
]

DEFAULT = (255, 150, 150, 150)   # the old switch's generic grey, kept as the
                                 # last resort so an unclassified row is still
                                 # a visible cube.

def base_for(name):
    n = name.lower()
    for kw, argb in KEYWORDS:
        if kw in n:
            return argb, kw
    return DEFAULT, None

def main():
    doc = json.loads(SRC.read_text())
    races = doc['races']

    # Deterministic per-row jitter so two rows sharing a base keyword are still
    # distinguishable as cubes. Jitter is derived from the race id alone, so
    # the same row always gets the same colour.
    used = {}
    out = {}
    for rid in sorted(races, key=int):
        row = races[rid]
        name = row.get('name', '') or row.get('code', '')
        (a, r, g, b), kw = base_for(name)
        if a == 0:
            out[rid] = (0, 0, 0, 0)
            continue
        n = int(rid)
        # spread siblings apart on each channel, bounded so the tint stays
        # recognisably in family
        jr = ((n * 37) % 41) - 20
        jg = ((n * 53) % 41) - 20
        jb = ((n * 71) % 41) - 20
        # first row to claim a keyword keeps the base exactly
        if kw is not None and kw not in used:
            used[kw] = rid
            jr = jg = jb = 0
        c = (a, max(24, min(245, r + jr)),
                max(24, min(245, g + jg)),
                max(24, min(245, b + jb)))
        out[rid] = c

    # De-collide: two rows must never land on the same cube tint (the only
    # deliberate exception is the fully transparent invisible pair). Nudge
    # deterministically until unique.
    taken = {}
    for rid in sorted(out, key=int):
        a, r, g, b = out[rid]
        if a == 0:
            continue
        k = 0
        while (a, r, g, b) in taken:
            k += 1
            d = k if k % 2 else -k
            if k % 3 == 1:   r = max(24, min(245, r + d * 3))
            elif k % 3 == 2: g = max(24, min(245, g + d * 3))
            else:            b = max(24, min(245, b + d * 3))
        taken[(a, r, g, b)] = rid
        out[rid] = (a, r, g, b)

    # collision report
    seen = collections.defaultdict(list)
    for rid, c in out.items():
        seen[c].append(rid)
    dupes = {c: v for c, v in seen.items() if len(v) > 1}
    for c, v in dupes.items():
        print('DUPLICATE', c, v)

    for rid, (a, r, g, b) in out.items():
        races[rid]['color'] = '#%02X%02X%02X%02X' % (a, r, g, b)

    doc['_version'] = '1.4'
    # Idempotent: re-running must not append the provenance note twice.
    note = (
        ' T304 Leg 2 added the "color" column (#AARRGGBB placeholder-cube tint,'
        ' used only when no model could be built for the race, T213). THOSE'
        ' COLOUR VALUES ARE INVENTED, not sourced from EverQuest data: Will'
        ' ruled on 2026-08-28 that inventing them is acceptable and the bar is'
        ' "plausible and distinguishable". They are keyed on each row\'s own'
        ' name, never on the pre-JSON race numbering.')
    if note.strip() not in doc['_notes']:
        doc['_notes'] = doc['_notes'] + note

    SRC.write_text(json.dumps(doc, indent=2) + '\n')
    print('rows:', len(races), 'distinct colours:', len(seen))

main()
