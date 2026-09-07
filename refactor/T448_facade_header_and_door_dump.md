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
