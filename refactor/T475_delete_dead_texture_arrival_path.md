# T475 — delete the dead async texture-arrival path (B670, closing B640)

Class B (unreached), **no lock, no Pi**, slot3. Read T473's Review
(`refactor/T473_door_retexture_after_pending_texture.md`) — it proved the path
has no producer — and T470/T468's Reviews for the door ownership rules.

## The decision (orchestrator)

**Delete, do not restore.** Texture loading became synchronous in Batch T's T04
(`aaee58d1`, 2026-03-16), and T326 (`77c842b4`) deleted the cache's async
upload queue — the only code that ever submitted a GPU upload with
`callbackKey >> 56 == 3`. Load time is secondary to correctness with no maximum
(Will, standing decision #1), so there is no reason to bring an async texture
producer back. Everything downstream of that key is unreachable: the
`processCompletedUploads()` / `processOneGPUResult()` texture arm
(`renderer_gpu_upload.cpp`), `doorTextureRebuildQueue` and its two drains
(`renderer_frame.cpp` Priority 8, `renderer_zone_stream.cpp` P1.6),
`pendingTextureDoors()` / `pendingTextureObjects` / `pendingTextureRegions_` and
their consumers, `setGPUUploadThread()`'s never-read pointer,
`pendingAsyncUploads_` (read, never written), and whatever only those reach.

## Tasks

1. Census every symbol of the path (T473's hop list and census table are the
   starting set), prove zero producers/readers per the protocol's zero-caller
   rule (`src/ include/ tests/ tools/ benchmarks/ docker/`), then delete,
   cascading to a fixed point the T458 way. Keep what the atlas-page and region
   vertex-buffer uploads use (`GPUUploadThread` itself stays).
2. Replace T473's tripwire test with the right net for a deleted path: a source
   census that fails if a texture-keyed upload (high byte 3) or a pending-texture
   door/object/region queue reappears without a consumer. B640's missing
   `rebuildSingleDoor()` branch closes with the path; say so in the Review.
3. Leave a short tombstone comment where the arm was (standing decision #13:
   delete commented-out CODE, keep tombstone PROSE explaining a deliberate
   absence), naming T04/T326/T473/T475.
4. B671 (a mesh built without its texture stays untextured for the zone) is
   NOT this unit — it is a live-measurement item for a later lock holder; do not
   touch its logging.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`renderer_frame_render` and zone-stream order goldens are
pinned — in-place per G-correction 32, say so); `ZeroCallers` / `WriteOnlyFields`
rows updated; visual gate (no image golden moves); ARM; file sizes; doc nets
(`docs/module_map.md`, CLAUDE.md mentions). Board B690–B699; strike B670, B640.

File territory: `renderer_gpu_upload.cpp`, the two queue drains, the pending
texture queues in zone load / zone stream / door / object / region code, their
headers, tests, `scripts/refactor/check_zero_callers.py`, docs. **Not** the
camera component, `CameraController`, `/camera`, `camera_rig.h` (T474, slot1),
**not** entity/equipment attachment (T472, slot2).

STOP: a symbol on the path turns out to have a live producer (report it with
the citation — the decision then returns to the orchestrator); an image golden
moves.

## Review

(executor fills)
