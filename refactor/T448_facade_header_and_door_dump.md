# T448 — the renderer facade header back under its Batch L size (structural); the door-spawn byte dump (B464)

Class B, slot1, no server. `include/client/graphics/irrlicht_renderer.h`
is 1,429 lines (Batch L left it at 1,343; it regrew through T3xx/T4xx
accessor additions). Read Batch L (`refactor/L_*.md`) for the facade
rule (the header is the bridge's API; components own the bodies), the
header itself (188 method declarations; count the inline bodies), the
`ConfigKeyReachability`/G05 pins that name renderer methods, and B464.

1. Every inline method body in the facade header that is more than a
   one-line forwarder moves to the component TU that owns the state it
   touches (say which per method); one-line forwarders stay. Dead
   declarations (zero callers tree-wide incl. tools/ and tests/) are
   deleted with the census shown. Comment blocks that restate a method
   name are dropped. Target: the header under 1,200 with no behaviour
   change; G05/G06 zero insertions/reorders (bodies moving between TUs
   do not change the frame's statement order — prove it).
2. B464: the door-spawn debug dump prints the door's own id and its own
   80-byte record's bytes; pre-fix failing test on a two-door packet.
3. T431 (slot3) is editing camera code and may touch this header — keep
   the camera section's declarations untouched to minimise the merge.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix
for B464, G05/G06 in place, visual gate standalone + ARM (no image
golden should move), file sizes, DocClaims/module_map for moved bodies.
Strike B464; new items B467+ (T447/T431 allocate from B465+ — say
which you used).

STOP: an image golden moves; a G05/G06 reorder.

## Review

Executed in slot1 on `task/T448-facade-header`, base `d82a81f5`. Full tables,
the caller census method and the gate numbers are in
`/mnt/projects/fable/review/T448/README.md`; the headlines:

**1. The header is 1,429 -> 1,191 lines.** Six inline bodies were more than a
one-line forwarder; five moved to the TU that owns the state they touch
(`setRenderDistance`, `requestChatPrefill`, `takeChatPrefill`,
`setNightAmbientProbe` -> `renderer/irrlicht_renderer.cpp`; `setLoadingProgress`
-> `renderer/renderer_init.cpp`) and the sixth, `setFogThickness`, was deleted
outright as dead. The other 96 inline bodies are one-line forwarders and stay.

**2. Ten declarations had zero callers tree-wide and were deleted**:
getBoidsManager, getTumbleweedManager, getUIAtlas, getZoneShader,
isLoadingScreenVisible, getFogThickness, setFogThickness (0 occurrences
anywhere), getConstrainedTextureCache (its one hit is ZoneGeometry's unrelated
method of the same name), and getSkyDebugInfo + removeWorldObject (whose only
hits were their own definitions, deleted with them). The census swept
`src/ include/ tests/ tools/ benchmarks/ docker/` -- 1,167 files -- and every
candidate was re-grepped by hand rather than trusted to the frequency count.
`fogThickness_` itself stays: four sites read it as `ctx_.fogThickness`. With
no setter it is now a constant 50.0f, which is a product question, not one this
unit should answer.

**3. Comments: 39 lines that restated the declaration below them were dropped,
and eight Batch-L narrative blocks were MOVED VERBATIM into the component
header that owns the bodies they describe** (player_camera_component.h,
frame_component.h, overlays_component.h, rdp_component.h, and two into
irrlicht_renderer.cpp beside the bodies that moved with them). Nothing was
discarded. The eleven per-component `unique_ptr` blocks at the end of the class
collapsed to one shared note: each block's three facts -- the family, "ctor
body, renderer_init.cpp", and the friendship census -- are already written in
more detail in that component's own header, checked one by one before the
collapse.

**4. B464: half the board entry was wrong, and this unit measured it.** The
loop-index-as-door-id half was real. The "indexes `data` from the PACKET base"
half was not: `data` is `p.Data() + 2 + i * sizeof(Door_Struct)`, already that
door's own record, so no line ever printed the first door's bytes. The dump now
formats through `eqt::entities::FormatDoorSpawnDump(loopIndex, record)`, which
prints the record's own id and keeps the loop index as an explicit
`(packet slot N)`. `DoorSpawnDumpTest.EachLineNamesItsOwnDoorIdNotTheLoopIndex`
FAILED before the fix and passes after;
`.EachLinePrintsItsOwnRecordsBytesNotTheFirstDoors` passes in both runs on
purpose -- it is the pin that stops a future "fix both indices" from breaking
the half that was already right.

**5. Gates.** Build exit 0. `ctest -j6` 4665/4666 -- the single red was
`check_build_tree_orphans` flagging `build/bin/test_detail_view_distance`, a
stale binary this slot inherited from the previous unit (T445) whose source
does not exist at this base; deleting it made the test pass standalone. G05
(`test_frame_render_order`) and G06 (`test_movement_order`) are 4/4 each: zero
insertions, zero reorders, as a structural move must be. Visual gate PASS
standalone with no image golden moved (host load 9.49; qeynos2 CPU-p95 4.089 ms
against a 10.108 ceiling, gfaydark 9.872/10.672, befallen 8.395/10.948). ARM
cross-build exit 0 (host load 11.43). `check_file_sizes.sh fable`: 14 ok, 0
warn, 0 fail.

**6. Board.** B464 struck, with the correction recorded in the strike. No new
items -- the B467+ range reserved for this unit went unused.

**Deviations.** (a) The camera section's declarations were left untouched as
the plan required, so T431's merge is unaffected. (b) B464's fix is pinned by a
pure formatting helper rather than by capturing log output: `LOG_DEBUG` writes
through a double-buffered thread-local drained by a background thread, so a
`CaptureStdout` assertion would be a race, and the macro compiles to nothing
without `EQT_DEBUG`. Extracting the format string is what makes the line
testable at all, and the production call site is the only caller.

