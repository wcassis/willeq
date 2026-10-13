# B888-CC1 — Character Creation FacePick (#204)

This unit makes the Set Face control on the existing creation page open the
stock `EQUI_FacePick.xml` modal. The modal edits a draft; Accept commits it,
Cancel discards it, and Random samples each visible picker uniformly. The
committed appearance is serialized through the existing Titanium
`OP_CharacterCreate` builder. The world-script `--create-character` path uses
the same shipped race/gender ranges for its optional appearance keys.

## Oracle method and evidence

Raw official-client evidence is under
`/home/user/kb-evidence/kb-cc1-facepick/oracle/`. The original
`UseLuclin*=FALSE` client configuration shows only FACE on Human male and Dark
Elf female (`08_human_facepick.png`, `15_darkelf_facepick.png`, their stepped
stills and videos). After enabling the official client's Luclin character
models, the additional XML picker rows appear; the mode distinction matters
when reading a hidden row. The original INI was restored byte-for-byte after
the packet leg (`cmp` exit 0, SHA-256
`a54ab291128cdcc8d38aa31faff29a8f102e9fa4e031684426a9f7febe7568ff`).

The corrected Luclin-enabled matrix4 records a still after each short
DirectInput up pulse, one down pulse, and a video for each in-era race/gender
pair. Its live arrow-brightness probe
records which rows are visible. `oracle/check_face_ranges.py` checks that
capture against the server's `RaceAppearance` bounds
(`/mnt/projects/akk-stack/code/common/races.cpp`) as a *secondary* constraint,
then emits candidate `facepick.tsv` and `char_create_faces.tsv` files. All 26
pair videos and 182 visibility rows are present; the raw corpus is on the
oracle VM under `/home/user/eq-oracle/facepick-matrix4/`. Local review copies
are under `oracle/matrix4-review-videos/`, `oracle/matrix4-contacts/`, and
`oracle/facepick-matrix4-compact/`. The copy in
`data/char_create_faces.tsv` is **provisional** for build and UI verification.
The server bounds alone are not an oracle measurement; some numeric wraps are
not yet proved by the stepped visuals or wire. Do not ship that table as
measured.

The first fast matrix attempt is explicitly marked INVALID in
`oracle/INVALID-matrix-attempt.txt`: a selected race failed visual
verification. The later guarded capture had two harness failures that remain
in their executor job logs: FFmpeg refused an existing pilot video, then
whole-popup OCR failed on Erudite's hidden HAIR COLOR row. A separate resume
also exposed unreliable net-zero Cancel drags; the oracle rig's established
`jclick` closed it. No row from those failed transitions is treated as a
range measurement. The corrected matrix also had two preserved failed guard
jobs: its first run stopped before High Elf female, and its first resume
stopped before Iksar female. The second resume finished at Wood Elf female
with exit 0. A separate Dark Elf female Random-3 video and three stills are
in `oracle/darkelf-female-random3.mp4` and
`oracle/darkelf-female-random-*.png`.

The first official-client create pcap (`oracle/facepick-create.pcap`) has
default appearance fields only. The later `oracle/facepick-nondefault.pcap`
captures a Barbarian male created after FacePick Accept. Its decoded
`OP_CharacterCreate` is the 80-byte payload in
`oracle/facepick-nondefault-fields.txt`: haircolor 10 at byte offset 4,
beardcolor 0 at 8, beard 0 at 12, race 2 at 20, hairstyle 0 at 28, face 15
at 64, and both eye fields 10 at 68 and 72. The packet's tutorial field is
1, while fable deliberately sends 0 under the existing no-tutorial decision;
`OfficialFacePickCreateMatchesCapturedWordsExceptTutorial` pins the remaining
19 words. The field offsets are in
`/mnt/projects/akk-stack/code/common/patches/titanium_structs.h` at
`CharCreate_Struct`; the source comment on eyecolor2 says `0073`, but its
contiguous `uint32` position after eyecolor1 is byte offset `72`.
`oracle/facepick-color-probe.pcap` contains two further official Dark Elf male
creates: the no-FacePick baseline reached the DB with haircolor 17, while a
three-up-click create reached it with haircolor 14. These packet samples are
compatible with the candidate 13..18 range, but cannot by themselves prove
its endpoints. A duplicate-name attempt was refused at name approval and
produced no create packet. The four test characters used by these probes were
removed by exact IDs after client and world stopped; the character-linked
table census was zero afterward, the stack was taken down, and the lane-1
lock was released.

## Implementation seams

- `include/client/graphics/ui/face_pick_layout.h` quotes the XML rects and
  labels; `FacePickLayout.EveryXmlRectAndLabelMatches` parses the stock XML.
- `src/client/login_world/char_create_faces.cpp` parses the shipped range TSV
  for both UI and headless script calls. Hidden rows are absent from input and
  drawing; no appearance maximum is hardcoded in production C++.
- `src/client/graphics/ui/face_pick_panel.cpp` owns the modal draft, rendering,
  wrapping, uniform Random, and hit resolution. Race/gender changes clear the
  committed draft. `char_create_screen.cpp` maps the committed values into
  `CharCreateRequest`; Barbarian WOAD occupies the tens of the face field,
  as the server's bot appearance code does.
- `src/client/login_world/world_script.cpp` accepts `FACE`, `HAIR`,
  `HAIRCOLOR`, `EYES`, `BEARD`, and `BEARDCOLOR`, validates them against the
  same TSV, and sets both eye fields from `EYES`.

## Verification

The XML rect test, synthetic TSV/modal tests, non-default packet-word fixture,
headless Release target build, and all 48 `CharCreate|FacePick` ctests passed
locally against the provisional table. The first headless Debug archive build
failed twice at `ar` (archive-copy error); it was not reclassified as a pass.
The Release rebuild and its focused appearance/packet tests passed. A rendered panel capture is at
`/home/user/kb-evidence/kb-cc1-facepick/preview-provisional/charcreate_facepick_800x600.png`.
The full hygiene rerun passed 30/30 tests. Draft MR !168 exists for review;
it must remain draft while the numeric-range acceptance criterion is unresolved.

## Not verified / left open

- The revised 2026-10-13 calibration method also failed its prerequisite of a
  stable opening state across creations. Three real Barbarian-male creates
  (untouched, then one and two attempted UP pulses per visible row) are in
  `facepick_wire_pilot.tsv`. Each row cites the exact record-header byte offset
  in the raw `oracle/calibration-failed.pcap` under the evidence root; the
  pcap's SHA-256 is
  `2d5612b21bb09330dc21fcdb0352dc68a91bf41dbe4541f9b20d0b5ae27877b3`.
  The first two otherwise-identical Barbarian-male Warrior creation screens
  already differed *before* any FacePick input: red hair/no beard versus
  orange hair/full beard (`oracle/calibration-step1-selected.png` and
  `oracle/calibration-step2-selected.png`). The untouched packet had hair=2,
  haircolor=4, beard=4, eye1/2=5; the one-UP packet had hair=255,
  haircolor=255, beard=0, eye1/2=10; the two-UP packet had hair=0,
  haircolor=7, beard=2, eye1/2=1 (`facepick_wire_pilot.tsv:3-5`). These
  observations cannot form a contiguous sequence from one fixed numeric
  origin. DirectInput also drops some clicks, so attempted pulse counts alone
  cannot prove a wrap. The guarded calibration batch was stopped after its
  third creation began and *before* it submitted a packet. No row has been
  promoted from PROVISIONAL or labeled as a measured endpoint.
- The calibration characters (IDs 87-89) were soft-deleted via the official
  UI, then all their linked DB rows were removed by
  `oracle/calibration-cleanup.sql` with the world container stopped. A
  before/after dump comparison of the `account` and `character_data` INSERT
  statements returned exit 0. The official client exited, pcap stopped with
  zero kernel drops, `eqclient.ini` was restored byte-for-byte (SHA-256 above),
  the stack is down, and lane 1's run-owned lock was released.
- The follow-up wire endpoint pilot did not establish a minimum. With the
  official client on Barbarian male, each visible FacePick row was stepped up
  and down before Accept; the create packet at
  `/home/user/kb-evidence/kb-cc1-facepick/oracle/wire-pilot-create.tsv:1`
  contains face 4 (payload byte offset 64), eye colours 20 (offsets 68/72),
  and 255 for hair style/colour and beard style/colour (offsets 28/4/12/8).
  These are not the provisional first-option numbers. The picker has no
  numeric display and wraps, so returning to the starting appearance cannot
  identify the absolute minimum. This pilot is **not** one of the requested
  52 endpoint packets and must not be cited as a range. Its raw pcap is
  `oracle/wire-pilot-failed.pcap` in the evidence root. The seeded character
  was removed with the world stopped; account/character-table data returned
  to the pre-run snapshot (ignoring only the dump timestamps), the INI was
  restored byte-for-byte, and lane 1 was stopped and unlocked.
- The 3D character preview/sample model is a separate #204 leg. Until it
  lands, this modal shows the numeric choice beside each official picker
  label; it still serializes and validates the selected appearance.
- Ranges whose wrap cannot be corroborated in the live matrix must remain
  explicitly unverified, not silently promoted from the server bounds.
