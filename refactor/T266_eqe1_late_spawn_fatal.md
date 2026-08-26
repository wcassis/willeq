# T266 — a race that spawns after zone load is FATAL under EQE1 (T263 finding; violates rule R2)

Slot: slot2. x86. OWNS eqemu (noclobber) for the reproduction and proof.
Class A, CRITICAL (client exit). Territory: the EQE1 archive resolver —
`openArchiveContaining()` and the zone candidate set (renderer_effects.cpp
~408, T207's `eqe1_texture_index`, T199's spawn-driven set, T214's
`isPersistentEQE1Archive`), tests. Do NOT touch race_models/builder/db
(T265 slot1) or anything T264 (slot3) deletes — T264 is intents/handlers
in src/client/action, bridge, eq_*.cpp; you are in graphics/ resolver
code.

## Defect (review/T263/README.md §3)
qeynos2 safe point, post-load `#npctypespawn` of a race-195 NPC: mesh
loads from `bwd_chr.s3d` (4631 verts), then
`EQE1: 'bwdhe0005' exists in 9 archive(s), none a candidate for zone
'qeynos2'` → `[FATAL] Texture 'bwdhe0005.dds' not found in any EQE1 flat
pack` → client exits. `bwd_chr.eqe1` holds the texture. The candidate set
is built from the spawn list at zone load only, so ANY later spawn of a
race absent at load (roamers arriving, pets, GM/summoned NPCs, illusions)
kills the client. Will's rule R2 (deferred.md STATUS): an archive is
legitimately open when a lookup needed it and nothing already open could
serve it — the current code refuses exactly that case.

## Scope
1. Reproduce (pre-fix binary): the T263 recipe; capture the FATAL.
2. Fix: when a lookup misses every open/candidate archive, consult T207's
   name→archive index over ALL archives, open the one that holds the name
   (R2), and record it in the request log with a distinct tag
   (`late-open`). Keep the candidate set as the fast path. The
   all-or-nothing FATAL stays for names in NO archive (that is a real
   asset gap, decision #2).
3. Unit test on the resolver: a name absent from the candidate set but
   present in another indexed archive → resolved, archive opened once;
   a name in no archive → still fatal-path. Red/green.
4. Live after: same spawn → `placeholder=false`, `vertices=4631`, screenshot
   with 0 magenta pixels, request log shows the late-open of `bwd_chr`.
   Zone-in time unchanged (Pi is not in scope; note it).
5. Delete the GM-spawned dragon rows afterwards (`zone_state_spawns`, T263
   mechanism) and restore Testchar.

## Verification
Full ctest; visual gate; Review; evidence review/T266/. Commit:
`T266: EQE1 late spawns open the archive that holds the texture (rule R2)`.
