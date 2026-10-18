#!/usr/bin/env python3
"""B1053: audit every OptionsLayout table row (main pages AND the nested
ODP_ParticlesTabs sub-pages) against the oracle XML.

Prints:
  rows=<n>  rows per tab
  rect mismatches vs the XML <Location>/<Size> (main pages)
  sub-page membership and bounds violations
  the XML page pieces absent from the table (expect ODP_SetFullscreenButton,
  shared rect with ODP_SetWindowedButton per options_layout.h:203-206)
"""
import re
import sys
import xml.etree.ElementTree as ET

HEADER = '/home/user/src/willeq-wt/kb-b1053-options-oracle/include/client/graphics/ui/options_layout.h'
XML = '/mnt/projects/EverQuestP1999/uifiles/default/EQUI_OptionsWindow.xml'

header = open(HEADER).read()
root = ET.parse(XML).getroot()

PAGES = ['OptionsGeneralPage', 'OptionsDisplayPage', 'OptionsMousePage',
         'OptionsKeyboardPage', 'OptionsChatPage', 'OptionsColorPage',
         'OptionsMailPage']
TABS = ['General', 'Display', 'Mouse', 'Keys', 'Filters', 'Colors', 'Mail']

# --- table rows --------------------------------------------------------
pat = re.compile(r'\{Tab::(\w+),\s*Kind::\w+,\s*"([^"]+)",\s*'
                 r'(\\?-?\d+),\s*(\\?-?\d+),\s*(\d+),\s*(\d+),')
rows = pat.findall(header)
print(f'rows={len(rows)}')
per_tab = {t: 0 for t in TABS}
for tab, *_ in rows:
    per_tab[tab] += 1
print('per_tab:', repr(per_tab))

# --- XML geometry --------------------------------------------------------
def num(el, tag):
    c = el.find(tag)
    return int(c.text) if c is not None and c.text and c.text.lstrip('-').isdigit() else None

elements = {}
for e in root.iter():
    if e.get('item'):
        elements[e.get('item')] = e

PAGE_W, PAGE_H = 387, 402
SUBBOX = elements['ODP_ParticlesTabs']
SUBBOX_X = num(SUBBOX.find('Location'), 'X')
SUBBOX_Y = num(SUBBOX.find('Location'), 'Y')
SUBBOX_W = num(SUBBOX.find('Size'), 'CX')
SUBBOX_H = num(SUBBOX.find('Size'), 'CY')
SUB_STRIP = 22  # the nested box's own tab strip (the main page's <Location>(0,22))
SUBPAGE_W = SUBBOX_W
SUBPAGE_H = SUBBOX_H - SUB_STRIP
print(f'subbox=({SUBBOX_X},{SUBBOX_Y}) {SUBBOX_W}x{SUBBOX_H} '
      f'subpage {SUBPAGE_W}x{SUBPAGE_H}')

mismatches = []
unowned = []
sub_bounds = []
table_items = set()
SUBS = ['SpellParticlesPage', 'EnvironmentParticlesPage', 'ActorParticlesPage']
SUB_ITEM = 2  # subPage column in a table row
SUB_TAB_INDEX = {'SpellParticlesPage': 0, 'EnvironmentParticlesPage': 1,
                 'ActorParticlesPage': 2}

# Which XML element owns each item (main page pieces, sub-page pieces, box)
owner = {}
for page_name in PAGES:
    for piece in elements[page_name].findall('Pieces'):
        name = piece.text.replace('TabBox:', '')
        owner[name] = page_name
for s in SUBS:
    for piece in elements[s].findall('Pieces'):
        owner[piece.text] = s

for r in rows:
    tab_name, item, x, y, w, h = (r[0], r[1], int(r[2]), int(r[3]),
                                  int(r[4]), int(r[5]))
    table_items.add(item)
    el = elements.get(item)
    if el is None or item not in owner:
        # e.g. ODP_ParticlesTabs is the box itself
        if item == 'ODP_ParticlesTabs':
            continue
        unowned.append(item)
        continue
    kind = owner[item]
    loc = el.find('Location')
    size = el.find('Size')
    if kind in PAGES:
        lx = num(loc, 'X') if loc is not None else None
        ly = num(loc, 'Y') if loc is not None else None
        cx = num(size, 'CX') if size is not None else None
        cy = num(size, 'CY') if size is not None else None
        if None not in (lx, ly, cx, cy):
            if (lx, ly, cx, cy) != (x, y, w, h):
                mismatches.append((item, (x, y, w, h), (lx, ly, cx, cy)))
            # bounds
            if not (0 <= x and x + w <= PAGE_W and 0 <= y and y + h <= PAGE_H):
                sub_bounds.append((item, f'page {tab_name}', (x, y, w, h)))
    elif kind in SUBS:
        lx = num(loc, 'X')
        ly = num(loc, 'Y')
        cx = num(size, 'CX')
        cy = num(size, 'CY')
        if None not in (lx, ly, cx, cy):
            # XML sub-page offsets are page-relative in the table:
            # (SUBBOX_X, SUBBOX_Y + SUB_STRIP) + (lx, ly)
            if (SUBBOX_X + lx, SUBBOX_Y + SUB_STRIP + ly, cx, cy) != (x, y, w, h):
                mismatches.append((item, (x, y, w, h),
                                   (SUBBOX_X + lx, SUBBOX_Y + SUB_STRIP + ly, cx, cy)))
            if not (0 <= lx and lx + cx <= SUBPAGE_W and 0 <= ly and ly + cy <= SUBPAGE_H):
                sub_bounds.append((item, f'subpage {kind}', (lx, ly, cx, cy)))

print(f'rect mismatches={len(mismatches)}')
for m in mismatches:
    print('  ', m)
print(f'rows with no XML owner={len(unowned)}')
for u in unowned:
    print('  ', u)
print(f'bounds violations={len(sub_bounds)}')
for b in sub_bounds:
    print('  ', b)

absent = []
for name in owner:
    if name.startswith('TabBox:'):
        continue
    if name not in table_items:
        absent.append((name, owner[name]))
print(f'XML pieces absent from table={len(absent)}')
for name, o in sorted(absent):
    print(f'  {name} ({o})')

sys.exit(1 if (mismatches or sub_bounds or unowned) else 0)
