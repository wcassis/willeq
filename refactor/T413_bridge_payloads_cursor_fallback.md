# T413 — bridge payload fields nothing draws (B296); the cursor name (B285); the spell fallback blend (B286)

Class A + B + B, slot1, no server. Read the three board entries, T407's
census (`scripts/refactor/check_write_only_fields.py`, the B296 row
list), T406's §Review (B285/B286).

## Leg A — B296

Ten bridge-event payload fields are filled on the game thread and read
by no renderer consumer. For each: what the official client SHOWS for it
(a target's HP bar, a buff's remaining ticks, a group member's mana, a
spell gem's refresh…) — name the oracle UI element from the default UI
XML (`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_*.xml`) or an
existing review screenshot. Then either draw it (the static-panel seam;
placement scaled from the XML as T393 did) or, if fable has no panel for
it yet, board the panel as its own item (B320+) and leave the field.
Do the ones whose panel exists. Tests pre-fix failing per field drawn.

## Leg B — B285

`createSoftwareCursor()` asks the cache for `softwareCursor` while the
pack entry is `software_cursor`; it works only because the cursor loads
before the pack opens. Make the name the pack's (one name), and a test
that the cursor resolves from the pack after the pack is open (pre-fix:
FATAL). DRM path only — say how you exercised it headlessly.

## Leg C — B286

`spell_particle_fallback` is drawn `GL_ONE, GL_ONE` (additive) with
constant white RGB and the shape in alpha, so it is a white square by
construction. Match the oracle: T389's generic blue dome for
EmptyRecord/NoAffectIndex spells is the thing this fallback stands in
for. Either premultiply in the shader for additive (rgb *= a) or switch
the fallback material to alpha-blend — decide from what the oracle's dome
looks like (T389 frames: soft, translucent). Test pre-fix failing on the
rendered fallback's edge alpha (headless GLES2, T411's harness shape).

## Proof

Build (all trees), `ctest -j6` (no expected failures on this tip),
pre-fix/post-fix, visual gate standalone + ARM (a moved image golden is a
STOP with the contact sheet), file sizes. Strike B285, B286; strike or
annotate B296 per field.

STOP: none expected.
