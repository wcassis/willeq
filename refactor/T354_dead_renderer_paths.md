# T354 — Two dead renderer paths, deleted by measurement (B042, B047)

Class B, slot3. Both were kept by L03/L05 under correction 17 ("a
zero-caller setter whose member is read live is kept and boarded") so the
decision could be taken with evidence. The evidence now:

- **B042** `OverlaysComponent::drawZoneLineOverlay` — runs every frame,
  gates on `inZoneLine_`, whose only writer (`setZoneLineDebug`) was
  deleted as dead. A zone-line DEBUG border is a developer aid, not an
  official-client feature (no oracle), and nothing in the tree has ever
  turned it on. DELETE the draw body and its three members
  (`inZoneLine_`, `zoneLineTargetZoneId_`, `zoneLineDebugText_`) and the
  per-frame call in the frame pass — ⚠ that call is a statement in the
  G05 order golden: it learns the deletion IN PLACE (T335b shape), both
  faults re-planted. `test_ui_hide_command`/`test_static_ui_scroll` read
  the frame TU — run them.
- **B047** the incremental collision-registration API
  (`addRegionToCollision`, `addDoorToCollision`, `addObjectToCollision`,
  `findBspRegionForPoint`, `clearCollisionDebugLines`) — zero callers;
  `renderer_zone_draw.cpp`'s comment claims it runs progressively. Measure
  once more (comment-filtered grep + the compiler by deleting the
  declarations); if still zero: DELETE the five and rewrite the comment to
  say what actually happens (`setupMinimalZoneCollision()` once). K12 ff
  publisher check first. `clearCollisionDebugLines` — T335 added a
  collision-debug pixel test with a seam; keep whatever that test uses.

Proof: build; the K/L multiset for deletions is not needed (deletions
are the change) — the compiler and the suite are the proof; the G05
golden re-planted; visual gate + ARM standalone (graphics files change).
Strike B042 and B047.

STOP: a caller appears (then it is not dead — leave it and say so).
