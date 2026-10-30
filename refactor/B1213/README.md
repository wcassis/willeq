# B1213 -- worn-FX reference set v2: measurement facts

One lane-5 session. A row of 23 server-side characters, each equipped with its
`items.tsv` row (the epic pairs worn together), the Fiery Avenger positive
control at the head. The observer (Oraclewatch, GM) logged in once and walked
the row, holding 20 s per character, in ONE continuous video. This is a
reference set for review: the facts below are what the capture contains. There
is no glow/no-glow ruling and no client-mechanism claim in this leg -- the
client glow mechanism is B1206's question, and the B1206 observer verdicts are
VOID (Will reviewed every B1206 clip and each shows particle effects; that
instrument said "no" on 12 of them).

## The measured result (the census)

**All 25 rows of `refactor/B1206/tables/items.tsv` were observed, no
exceptions, across 23 characters in one continuous video.**

- Hand-back: **observed 19/19 (+pairs)**, plus the positive control and the
  five no-effect rows -- i.e. every `items.tsv` row.
- **19/19** = the nineteen non-control FX-bearing items (a proc/worn/click
  effect word in `items.tsv`), all observed. Two of them are the epic pairs
  worn together on one character: Blade of Tactics + Blade of Strategy
  (FxR02) and Swiftwind + Earthcaller (FxR04).
- **+pairs** = those two pairs (four items, two characters).
- **Positive control** = Fiery Avenger (FxR01, at the head of the row), the
  item Will confirmed glows.
- **Five no-effect rows** (observed, but they carry no proc/worn/click effect
  word in `items.tsv`): Swiftwind (light only, on FxR04), Jade Mace (FxR20),
  Red Dragon Scale (FxR21), Jaded Electrum Bracelet id 14649 (FxR22) and id
  16698 (FxR23).
- 25 item rows = 20 FX-bearing + 5 no-effect. 23 characters = 21 wearing one
  item each + 2 wearing a pair. Nothing is missing; nothing is listed as
  unobserved.

The per-character layout (character, class, item(s), world position, row
index) is `refactor/B1213/tables/row_layout.tsv`.

## The evidence

| artefact | where | what it is |
|---|---|---|
| continuous observer video | `review/B1213/session/video.mp4` (1139.2 s) | ONE take; the observer walks the row, 20 s per character |
| per-character timestamp table | `refactor/B1213/tables/timestamps.tsv` | video-relative window per character (t_start, t_end, 20 s hold sub-window) |
| settle still per character | `review/B1213/clips/*still_*.png` (23 files) | one full-frame still from each character's hold window |
| crop per character | `review/B1213/measure/crop_FxR*.png` (23 files) | the peak-lit frame cropped to the lit region |
| pcap | `review/B1213/session/capture.pcap` (87 MB) | every client session (`tcpdump -i any`): the observer + all 23 wearers |
| item words | `refactor/B1213/tables/item_words.tsv` | per character: equipment model words, effect115 words, tint, inventory IDFile |
| full item serialisation | `refactor/B1213/tables/items_wire.tsv` | each equipped item's full `Titanium::SerializeItem` static record |
| per-character FX signal | `refactor/B1213/tables/fx_signal.tsv` | the instrument's raw lit-pixel numbers (see below) |

The raw spawn/wear dumps behind `item_words.tsv` are
`/home/user/kb-evidence/kb-b1213-worn-fx-row/measure/{spawns_wire,wearchange_wire}.tsv`;
they are reproducible from the pcap with the committed `refactor/B1213/scripts/decode_row.py`.

### Item words (from the pcap)

`refactor/B1213/tables/item_words.tsv` has one row per character. The
`equipment_model_words` column is the nine-word `equipment[]` block of the
character's own ZoneEntry Spawn_Struct (`include/common/packets/spawn.h`,
words @197): per the standing decision it is a MATERIAL / IT model number,
never a database item id. Every character's model word matches its `items.tsv`
`idfile` (e.g. FxR02 = IT141 IT142, the Blade of Tactics + Strategy pair;
FxR04 = IT149 IT149, Swiftwind + Earthcaller share one idfile; FxR18 =
IT10648, Ghoulbane). The two pairs are visible on the wire as two model words
on one character. The six `effect115` words (the 0x73/115 spawn effect block)
are **0 0 0 0 0 0 for every one of the 23 characters** -- the worn effect is
not carried on the spawn channel.

### The instrument (a sanity gate, not a verdict)

`refactor/B1213/scripts/fx_signal.py` measures, per character, how many pixels
in a central band of the frame rise above a per-pixel 10th-percentile baseline
over that character's own 20 s hold window. Its contract, per the card: it must
detect the positive control in the same video BEFORE any per-character number
is reported.

- **Positive control (gate): DETECTED.** FxR01 (Fiery Avenger) lit_max = 14214
  (threshold 40). The gate passed, so the per-character numbers below are
  reported.
- Per-character numbers are in `refactor/B1213/tables/fx_signal.tsv`
  (lit_max, lit_mean, peak lit-pixel colour). They are a raw trace for review,
  not a glow/no-glow ruling. The per-pixel low-percentile baseline is the fix
  for the B1206 instrument defect: B1206 used a self-median baseline, which
  absorbs a continuous/animating glow into the baseline and reads the lit
  count as 0 -- the reason it said "no" on 12 clips Will saw glowing.

## How the session was run (reproducible)

- **Row creation (server-side).** `refactor/B1213/scripts/gen_row.py` generated
  the preseed SQL (`refactor/B1213/preseed/row_preseed.sql`): 23 accounts
  (id >= 10000), 23 characters (id >= 5000, zone qeynos2, x = -74, y = 420 down
  to 244 in steps of 8, z = 3.128, heading 160), and the 25 `inventory` rows.
  The row characters are live headless willeq clients
  (`--scenario zone_link_hold`) so each one's item serialisation is a real
  in-zone connection; `#summon` would not work on offline characters.
- **Camera recipe (committed).** The observer moves to target + (1.5, 1.5),
  heading 320, F9 x3 (fixed full-body camera), then holds. Calibrated so the
  target fills a large part of the frame. The fixed heading does not track the
  row, so the target's on-screen position drifts down the row -- that is why
  the instrument uses a wide central band + per-character baseline rather than
  one pinned box.
- **Walk plan (committed).** `refactor/B1213/plans/walk_plan.tsv` (generated by
  `refactor/B1213/scripts/gen_plan.py`): per character, goto + settle + 20 s
  hold + one still. The observer (Oraclewatch) was granted temporary GM
  (status 200) for `#goto`; the originals were 0/0 and were restored.
- **Timestamp table.** `refactor/B1213/scripts/build_timestamps.py` derives
  `refactor/B1213/tables/timestamps.tsv` from the driver's `timeline.tsv`
  (`review/B1213/session/timeline.tsv`) and the video start epoch
  (`review/B1213/session/video.mp4.t0`). Times are seconds from the start of
  the one video, so a clip of [t_start, t_end] shows that character.
- **Decode.** `refactor/B1213/scripts/pcap_decode.py` (reused from B1206, the
  same Daybreak state machine willeq's receiver uses) -> app packets;
  `refactor/B1213/scripts/decode_row.py` -> the item-word tables.

## Lane and DB state (left as found)

After the session: the 23 row characters/accounts/inventory rows were deleted
(`refactor/B1213/preseed/row_teardown.sql`), the Oraclewatch GM grant was
reverted to 0/0, the existing characters were restored from the pre-session
snapshot (byte-identical diff), the stack was brought down, and the lane-5
lease was released. The pre/post character snapshots are
`/home/user/kb-evidence/kb-b1213-worn-fx-row/session/char_snapshot_{before,after}.tsv`.

## No exceptions, no gaps

Every one of the 25 `items.tsv` rows was observed at close range in the one
continuous video with a committed timestamp window, a settle still, and its
item words from the pcap. There is no missing row to list.
