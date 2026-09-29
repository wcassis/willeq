# B910 — EQE1 31-char name truncation: the pack census

Renamed from B100 on 2026-09-29 (B908): B100 is the struck InputAction entry;
this work was filed under the GitLab issue number #100 by mistake. MR !27 and
its commits still say B100.

Card: kanban t_89745f84 (rework of t_987c6cd0, MR !27,
`task/kb-b100-eqe1-name-truncation` @ `76bc3a14`). Issue: #100.

`census.txt` is the durable evidence for the `76bc3a14` commit message's census
claim ("soldunga's gnome_stronghold_floor_transition is the ONLY name in the
tree that is truncated"). It is the captured stdout of `census.py` re-run on
this host against the shared live tree; re-running it reproduces every number
in the file:

```
python3 refactor/B910/census.py
```

## What the census establishes

- **1506 packs / 183,058 entries** in `/mnt/projects/EverQuest_ETC1`.
- **Version census {v1:1504, v2:1, v3:1}** — the sole v3 pack is
  `soldunga.eqe1`, carrying exactly one name-table-resident name
  (gnome_stronghold_floor_transition, 33 chars, stored FULL).
- **0 structural violations** — every pack's index fits the file, every data
  span is in-bounds, every v1/v2 name field is null-terminated, and every v3
  table reference lands in `[indexEnd, dataStart)`.
- **0 genuinely truncated names.** The truncation argument: the old builder's
  `strncpy(name, 31)` into a zero-init `char[32]` NUL-terminates, so a
  truncated name is always stored at exactly 31 chars — never 30, never more.
  v3 stores long names in full by construction, so the only candidates
  anywhere are exactly-31-char stored names. The census finds exactly one —
  `bricktrimflorish&protrait_alpha` in `frozenshadow` — and cross-checks it
  against the PFS source archives: it is a genuine 25-char base + `_alpha`
  companion that fits exactly (the base is present in the source), not a
  truncation.

## Why this script, and not the round-1 scratch variants

The scratch `b100_*.py` files that produced the round-1 numbers were untracked
working copies; `census.py` is the self-contained, durable re-implementation
of the census they pointed at, corrected for the v3 layout they got wrong:
`b100_census3.py` (the PFS cross-check) read the 48-byte inline field at
`16 + i*48` and never decoded the `{nameOffset, nameLength}` tail;
`b100_census.py` used a 72-byte v3 stride. Both are wrong for v3, whose entry
stride is **56** (48-byte entry + 8-byte name tail). `census.py` parses v3
correctly — the 16-bit version field at offset 4, the 56-byte stride, and the
8-byte tail for name-table-resident names — matching
`include/client/graphics/eqe1_format.h` (`indexEntryStride()`,
`EQE1IndexEntryNameTail`).

The one genuinely-truncated name in the tree (soldunga's) could never have
surfaced as a "31-char candidate" from any of those variants: in the v3 pack
its inline field is zeroed and the name lives in the table, so a 48-byte-stride
reader sees an empty field. `census.py` reads the table-resident name back via
the tail, which is what makes the v3-aware census sound.

## Boundary

This commit adds ONLY the census evidence (`refactor/B910/`, then
`refactor/B100/`; renamed to `refactor/B910/` by B908). It does not touch
the code (`eqe1_format.h`, `eqe1_loader.cpp`, `eqe1_builder.cpp`,
`composite_texture_io.cpp`, `verify_eqe1.cpp`, tests), the pack
(`soldunga.eqe1` / `willeq.eqe1`), the sha pin
(`data/willeq.eqe1.sha256`), or `data/eqe1_texture_index.json` — all of which
the round-1 review (t_85fc29eb) re-verified on this host.
