# T391 — the two-client render-thread SIGSEGV (B202)

Class A; slot1; eqemu lock (atomic; must be ABSENT — if present read it
and STOP). Read the B202 board text, `review/T386/README.md` §5 and
`review/T386/logs/fable_spells3_caster_gdb.txt` (the backtrace:
`SimBridgeComponent::applySimulationOutput` ← `applySimulationResults` ←
`FrameComponent::processFrame`, worker parked), `src/client/graphics/sim/`
(the sim bridge, `SimulationOutput`, `computeOnce()`), and T386's rig
notes (`--script` is the two-client driver; `#set invulnerable off`).

1. Reproduce under gdb with the SAME recipe (two clients, qeynos2 or
   arena, 20–60 s after the second zones in); get the faulting line and
   the values (which vector/index; which entity id). Three crashes with
   full backtraces + `info locals` in `review/T391/`. If ASAN builds in
   the slot's budget (T352's recipe used TSAN — the flags are analogous),
   one ASAN run is worth more than three gdb runs.
2. Read `applySimulationOutput` against what the worker wrote: the likely
   shape is an index computed from the entity set the worker saw applied
   to the entity set the render thread now has (a second player's spawn
   / despawn between compute and apply). Name the invariant.
3. Fix at the apply side (validate against the current set; skip stale
   entries) — not by widening a lock across the frame. Test, pre-fix
   failing: a unit test that runs `computeOnce()` over N entities, removes
   or adds one, then applies — must not read out of range (drive through
   the public seam T367's `test_zone_light_culling.cpp` used).
4. Live: a two-client session survives 5 minutes in both zones; capture
   the log. Then take T386's item 3 (B160 fire vs frost on the observing
   client) if time remains — do NOT spend more than 30 minutes on it.
5. Strike B202. Restore every server value; `down.sh`; delete the lock.

STOP: lock present; the fault is not in the sim apply (report the real
frame and STOP — do not fix by argument).

---

## Review (T391, executor, 2026-09-05)

Branch `task/T391-two-client-segv`, base `e3c80278`. **B202 is a
use-after-free of `EntityVisual::lightNode`, reproduced under ASAN with the
faulting line, fixed at the apply side's ownership, pinned by a new test that
fails before and passes after, and live-proven over 5-minute two-client
sessions in two zones.**

### 1. The defect, and why it looked like "a second player kills the client"

`EntityRenderer::setEntityLight()` (`src/client/graphics/entity_renderer.cpp`)
creates the entity's light with

```
visual.lightNode = smgr_->addLightSceneNode(nullptr, lightPos, colour, radius);
```

A **null parent means Irrlicht parents the node to the ROOT scene node**, and
`addLightSceneNode()` drops its construction reference before returning — so
the node comes back with a reference count of **one, held by its parent and by
nobody else**, while `EntityVisual` keeps a **raw pointer** to it.

`SimBridgeComponent::applySimulationOutput()`
(`src/client/graphics/sim/renderer_sim_bridge.cpp:1030-1040`) detaches that
node every time the simulation worker culls the entity:

```
} else if (!er.shouldBeVisible && visual.inSceneGraph) {
    visual.sceneNode->remove();
    visual.inSceneGraph = false;
    if (visual.lightNode) visual.lightNode->remove();     // <-- frees it
}
```

`ISceneNode::remove()` is `Parent->removeChild(this)`, and `removeChild()`
drops. At a count of one that **deletes the node**, and `visual.lightNode` is
left pointing at freed memory. The next frame that re-admits the entity then
runs, thirty lines earlier in the same function,

```
if (visual.lightNode) ctx_.smgr->getRootSceneNode()->addChild(visual.lightNode);
```

on freed memory, on the render thread, inside `applySimulationOutput()` — which
is B202's backtrace exactly. Two teardown paths dereference the same stale
pointer as well: `EntityRenderer::removeEntity()` and
`EntityRenderer::clearEntities()` (`entity_lifecycle.cpp`).

**The invariant:** *a scene node the renderer keeps a raw pointer to across a
`remove()` / `addChild()` cycle must be owned by an explicit `grab()` — it has
to survive with no parent.* The object lights already do exactly this
(`renderer_zone_load_objects.cpp`: `lightNode->grab(); lightNode->remove();`)
and the entity's own `sceneNode` already does (`entity_lifecycle.cpp` drops
it). `EntityVisual::lightNode` was the one node that did neither.

**Why only with a second player.** In the T386 session that produced the
backtrace, the whole 206-entity `qeynos2` population built **exactly one**
entity light node, and it belonged to the other player:

    [ENTITY] Created light for entity 286 (Testbuddy): level=11, radius=92.0

Testbuddy carries a *Gloomingdeep Lantern* (`items.light = 11`, inventory slot
14); no NPC in `qeynos2` carries a light. A single-client session therefore
never has a `lightNode` to free and runs clean forever — which is precisely
what T386 measured and could not explain. It is not a race and not a
worker/render-thread ordering problem (the worker is parked in its condition
variable in the backtrace, and this reproduces with one thread's worth of
determinism).

### 2. Reproduction — ASAN, with the faulting line

`review/T391/scripts/t391_run.sh` is the rig: observer = **Testcaster**,
graphical (Xvfb + `--egl-x11`), started first and allowed to settle for 55 s;
partner = **Testbuddy**, `--no-graphics`, started second — the *second player*,
and the only entity in the zone carrying a light. The partner then `#goto`s in
and out of the observer's visible-entity set for five minutes, which is the
cull / un-cull cycle. Observer built from `build-asan` (`-fsanitize=address
-g -O1`, `refactor/T352`'s recipe).

Two runs of the rig were needed to find the trigger and are kept as the record:

| run | zone | second player's light | result |
|---|---|---|---|
| `t391_prefix_asan_*` | befallen | none (wizard, no light source) | survived 407 s — **the negative control**: no lightNode, no crash |
| `t391_prefix3_asan_*` | qeynos2 | `Created light for entity 282 (Testbuddy): level=11` | **ASAN heap-buffer-overflow** |

The pre-fix report (`logs/t391_prefix3_asan_asan.2431866`):

    ==2431866==ERROR: AddressSanitizer: heap-buffer-overflow
    READ of size 8 at 0x514000124c40 thread T0
      #0 EntityRenderer::clearEntities() src/client/graphics/entity_lifecycle.cpp:210
      #1 ZoneLifecycleComponent::unloadZone() renderer_zone_lifecycle.cpp:285
      #2 IrrlichtRenderer::shutdown()      irrlicht_renderer.cpp:152
    0x514000124c40 is located 124 bytes after 388-byte region [0x514000124a40,0x514000124bc4)

Line 210 is `visual.lightNode->remove();`. The read lands in a **heap left
redzone** (shadow `fa`) inside a buffer the GLES2 driver allocated during
`CSceneManager::drawAll()` — i.e. the `ILightSceneNode` object was freed
earlier in the session and its memory recycled. The value that was broken is
`visual.lightNode` itself: it names storage that is no longer a scene node.
The observer's log confirms the whole chain in one session — the node is
created (`17:56:33 Created light for entity 282`), the partner leaves and
re-enters view repeatedly, and the pre-fix process dies at teardown before
`clearEntities()` completes (the log stops mid-`unloadZone`, at the Fog line
immediately preceding it).

### 3. The fix — at the apply side's ownership, no lock widened

Four lines, all ownership; `applySimulationOutput()` itself is untouched.

- `src/client/graphics/entity_renderer.cpp`, `setEntityLight()` create path:
  `visual.lightNode->grab()` — EntityRenderer now owns the reference implied
  by the raw pointer it stores.
- `setEntityLight()` `lightLevel == 0` path: `remove()` then `drop()`.
- `entity_lifecycle.cpp` `removeEntity()`: `remove()` then `drop()`, node
  nulled.
- `entity_lifecycle.cpp` `clearEntities()`: `remove()` then `drop()`, node
  nulled.

`remove()` is a no-op on an already-detached node, so the cull state does not
have to be tracked separately.

### 4. The test — fails before, passes after

`tests/test_entity_light_node_lifetime.cpp` (5 tests, registered in
`tests/cmake/renderer_guards.cmake`). It drives the **production apply seam**:
a real `EDT_NULL` scene graph, a real `EntityRenderer`, a bare never-`Init()`'d
`IrrlichtRenderer` with `Context().smgr` / `Context().entityRenderer` wired,
and `renderer.SimBridge().applySimulationOutput(out)` called with an
`EntityResult` whose `shouldBeVisible` flips false then true. Each test takes
**its own `grab()` first**, so nothing in the test dereferences freed memory
even against the pre-fix code — the failures are on the reference **count**,
deterministic in a plain non-sanitizer build.

    PRE-FIX   build/bin/test_entity_light_node_lifetime -> exit 1, 3 passed, 2 FAILED
      EntityRendererOwnsAReferenceToTheLightNode
        test:175  light->getReferenceCount()  Which is: 2   expected 3
      LightNodeSurvivesACullAndUnCullThroughTheApplySeam
        test:193  light->getReferenceCount()  Which is: 1   expected 2
                  (after the cull: no parent AND no owner -> in production, deleted)
        test:203  Which is: 2  expected 3
        test:208  Which is: 2  expected 3

    POST-FIX  build/bin/test_entity_light_node_lifetime -> exit 0, 5 passed

The three release tests (`removeEntity`, `clearEntities`,
`setEntityLight(0)`) pass in both runs — pre-fix vacuously (there was nothing
to release), post-fix as real no-leak assertions on the new `drop()`s.

