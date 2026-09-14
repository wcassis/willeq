# T491 — 100% memory accounting: every byte attributed, at every lifecycle point, and gated

Class A/P. **Will's ruling, 2026-09-14** (wills-standing-decisions, memory):
"the 32 MB figure and the need to track memory usage meticulously is the
reason I went down the ConstrainedCache road. I want 100% accounting of
memory use and I want it controlled. We have extensive perf/profiling and
should be able to track it at all game lifecycle points." This SUPERSEDES
`docs/decisions.md`'s T69/T293 line that untracked RSS is "reported, not
gated": **untracked anonymous memory is now a defect to drive to zero.**
Offline (no lock), slot **t2**. Coordinates with T490 (slot t1: the Iceclad
regions-scaling allocation; it will NOT touch the gate or presets — you own
them).

## What exists (read first)

- `MemoryReportInput` / `MemoryReportBreakdown` (`include/client/graphics/renderer_draw_types.h:145-260`),
  `DiagnosticsComponent::getMemoryReport()`, `/pmem`
  (`EverQuest::runPmemDiagnostics`), the T69 categories; the ConstrainedCache
  family (`constrained_texture_cache*`, `constrained_mesh_cache`, budgets in
  `config/constrained_presets.json`); `scripts/ci/visual_gate.py` (memory
  section, `untrackedBytes` at :171/:206/:643/:754, "reported not asserted"
  at :44); `docs/decisions.md` T293 envelope (256 MB RSS ceiling; "76–90% of
  RSS is untracked on the golden-image path"); `review/T293/`, `review/T289/`
  (the Pi numbers); `main.cpp:39` `mallopt(M_ARENA_MAX, 1)`.
- AUDIT-0914's Iceclad kill: anon-rss 318 MB, file-rss 20 kB
  (`review/AUDIT-0914/README.md`), i.e. the page cache is out of scope; the
  heap is the whole problem.

## Tasks

1. **A memory ledger with no leaks in the accounting.** Attribute every
   anonymous byte to a category at the allocator level, not by per-subsystem
   estimates: a scoped-category allocator seam (`MemoryScope` RAII pushing a
   category on the current thread; `malloc`/`operator new` hooks or a
   counting allocator that charges the live category; the Irrlicht-side
   allocations through its `irrAllocator` seam where the driver is ours).
   Categories = the lifecycle/owner set: zone geometry (WLD parse
   intermediates vs built meshes, separately), BSP/PVS/spatial, textures
   (decoded vs mapped), entities (per race model vs per instance), doors,
   objects/placeables, equipment, UI/atlas/icons, audio, network, game state,
   Irrlicht scene graph, "unattributed". Cost when off: zero (compile-time
   flag defaulting ON for the diagnostics build and the gate; measure the
   overhead and report it; if the hooks cost more than 2% frame time on the
   Pi, keep them behind the flag and document how the gate runs with them).
2. **Report at every lifecycle point**, same schema everywhere: startup,
   login/world, zone parse, spatial/BSP, mesh build, textures, entities,
   ready, steady state (60 s), zone change (before unload / after unload /
   after reload), `/camp`. Each line: per-category live bytes + peak, ledger
   total, `RssAnon` from `/proc/self/status`, and **unattributed =
   RssAnon − ledger** (allocator overhead/fragmentation reported separately
   via `mallinfo2`). `/pmem` prints it; the gate and `gles2_zone_benchmark`
   emit it as JSON.
3. **Gate it.** `visual_gate.py`: assert unattributed ≤ a stated bound
   (start at 10% of RssAnon, ratchet down as attribution improves; write the
   number and its trend into the README) and assert each category against
   its preset budget, on the golden zones. The T293 budgets stay; new
   categories get measured budgets by the T293 headroom rule. Record the new
   rule in `docs/decisions.md` as a dated entry quoting Will, and strike the
   "reported, not gated" sentence.
4. **The cap-hunt recipe as a script**: `scripts/ci/memory_cap_hunt.sh
   <cmd> [caps...]` running the command under `systemd-run --scope -p
   MemoryMax=<cap> -p MemorySwapMax=0`, halving the cap (256/128/64/32 MB)
   and reporting, per cap, the last lifecycle line printed before the kill.
   Runs on x86 and the Pi. T490 uses it for Iceclad; you validate it on nro.
5. Docs: `docs/module_map.md` rows, `src/client/graphics/CLAUDE.md` memory
   paragraph, `docs/decisions.md` entry.

## Proof

Standard verification bar; goldens in place; ARM; file sizes; the gate green
with the new assertions on the three golden zones (report the unattributed
percentage per zone, before and after). x86 numbers do not transfer to the
Pi (llvmpipe): say so; the Pi run is T490 Leg B's, which will use your
ledger if merged first, else board it.

File territory: a new `src/client/core/memory_ledger*.cpp` + header, the
diagnostics component's memory report, `/pmem`, `scripts/ci/visual_gate.py`,
`scripts/ci/memory_cap_hunt.sh`, `config/constrained_presets.json`,
`gles2_zone_benchmark`'s memory JSON, tests, docs. **Not** the BSP/spatial
loader or `zone_bsp*` (T490), not the texture cache eviction policy, not
held items/camera/T486 files.

STOP: the allocator hook cannot be made zero-cost when off (report the
measured cost and the alternative); Irrlicht allocations cannot be
attributed without patching the third_party tree beyond docker/irrlicht-drm/
(report which and how many bytes).
