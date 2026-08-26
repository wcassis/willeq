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

## Review (2026-08-26, slot2, task/T266-eqe1-late-spawn off fable b8725e94)

Evidence: `review/T266/` (README.md maps every file).

### Reproduction (pre-fix binary, T263 recipe)
qeynos2 safe point, `#npctypespawn 114106` (race 195, size 30): mesh loads
(`vertices=4631` from bwd_chr.s3d), then `EQE1: 'bwdhe0005' exists in 9
archive(s), none a candidate for zone 'qeynos2'` -> `[FATAL] Texture
'bwdhe0005.dds' not found in any EQE1 flat pack` -> EXIT=1
(`live_before.log` 64474-64475). Exactly T263 §3.

### Fix (graphics resolver only; T264/T265 territory untouched)
Two layers, both rule R2, both tagged `late-open`:

1. **Race-driven extension after load** (`renderer_zone_stream.cpp`
   `queueEntityPrepRequests`): T199's spawn-list -> archive extension now
   runs for every race that reaches the entity prep queue, not only the
   load-time list. Factored into `IrrlichtRenderer::addSpawnedRaceEQE1Candidate
   (raceId)` (renderer_effects.cpp), shared with Step0; deduped per race so
   the per-frame call is a set lookup. This is what fired live: `EQE1: race
   195 spawned after zone load -> its archive queued as a late candidate`.
2. **Index fallback** (`EQE1TextureIndex::resolve()`, new): a name held by
   NO candidate opens its first owner in index order instead of returning
   nothing. A name in NO archive still resolves to nothing -> the
   all-or-nothing FATAL stands (decision #2). `openArchiveContaining()` now
   calls resolve(), appends a fallback-opened archive to the candidate set
   (siblings take the fast path), logs one INFO line per late-open, and
   tells the cache (`ConstrainedTextureCache::noteEQE1LateOpen`).

Bookkeeping: `pendingGlobalEQE1Archives_` is now written from two threads
(loading thread Step0, render thread late spawns) and read on whichever
thread the cache lookup runs -> guarded by new `eqe1CandidatesMutex_` (never
held across `getOrOpenEQE1`, which takes `eqe1Mutex_`). `eqe1LoadTimeCandidateCount_`
marks the end of the load-time set so candidates appended later are tagged
late-open too. The T214 request log gains a 5th column `how`
(`late-open` / `-`). Reset with the candidate set in `unloadZone()`.

### Unit test (red/green): tests/test_eqe1_texture_index.cpp (new target)
| test | red (late branch = old rule) | green |
|---|---|---|
| CandidateHoldingTheNameWinsInCandidateOrder | OK | OK |
| NameInNoCandidateOpensItsOwnerAsLateOpen | FAIL | OK |
| NameInNoArchiveResolvesNothing | OK | OK |
| ResolveIsFalseBeforeLoad | OK | OK |
| Shipped.T263NameLateOpensBwdChrForQeynos2 (real index, qeynos2's static set) | FAIL | OK |

### Live after (same tour, fixed binary)
| | before | after |
|---|---|---|
| exit | 1 (FATAL) | 0 (/camp) |
| dumpscene | none | `[NPC] id=273 'Lord_Yelinak000' ... placeholder=false` |
| vertices | 4631 (then FATAL) | 4631 |
| screenshot | none | 800x600, **0 magenta of 480000** |
| request log | n/a | 23 names served by bwd_chr, all `late-open`; NOT-FOUND 0 |
| zone-in (x86 llvmpipe) | befallen 6272ms, qeynos2 5803ms | 6141ms, 6745ms (noise; EQE1-USAGE identical 9/0 archives) |

Pi not run (out of scope per plan; load time is not a work item).

### Deviations / unexpected
- The screenshot is a real, magenta-free frame but the size-30 dragon spawns
  at the invoker so the frame is its skin at point-blank range. A framed
  attempt (/warp 70 units + /face) put the player outside every BSP region
  and the dragon was culled -- kept as `after/*framed*` and labelled NOT
  evidence. No scriptable zoom exists. Will judges the first frame.
- `#invul off` before `/camp` got Testchar killed by the dragon (respawn at
  bind, qeynos zone 1). Level/exp/inventory unchanged; empty corpse deleted
  (WRITE 7); Testchar restored. Run2's tour keeps invul on.
- Protocol slip: the first pre-fix build was piped through `tail`; every
  later build/test wrote to a file instead.
- `check_file_sizes.sh fable` compares against the moved `fable` tip
  (23251315, T264/T265 merged after this branch was cut) so it lists 32
  files; `--files` over this unit's 11 files: 0 fail, 3 pre-existing WARNs.

### Verification
1. `cmake --build slot2/build -j24` OK. 2. ctest **2976/2977** (sole failure
AudioManagerTest.PreloadCommonSounds, ALSA). 3. build-gles2-x86.sh OK.
4. Visual gate **PASS**, goldens untouched. 5. build-arm-noble.sh OK.
6. check_file_sizes: 0 fail.

Server: lock taken/released (noclobber), up.sh befallen + boot_qeynos2.sh,
down.sh, `diff db_before.txt db_final.txt` empty, 3 dragon rows deleted.
