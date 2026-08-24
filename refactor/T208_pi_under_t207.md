# T208 — the Pi under T207: correctness and memory won, load time did not move

Evidence: `review/T208/` (two runs). Binary cross-built from `6ae38b2a`.

## Numbers

Cold befallen zone-in, Phase 5/15 -> Phase 15/15, Orange Pi:

| | archives open | unused | mapped | zone-in |
|---|---|---|---|---|
| S3D baseline (T186) | n/a | n/a | n/a | ~37.7s |
| EQE1 + probe (T206) | 37 | 24 | 149MB | 39.90s / 39.09s |
| **EQE1 + index (T208)** | **13** | **0** | **46MB** | **39.97s / 39.75s** |

Both runs `PI_EXIT=0`. The archive profile is byte-identical to x86's: 13 open, 0
unused, 46232KB mapped, 294 lookups served.

## The load time did not move, and that is the answer

Cutting 24 archive opens and 103MB of mapping changed the Pi's zone-in by
nothing measurable (~39.9s vs ~39.5s, inside run-to-run spread). The per-step
split from run 1 says why:

    seq_entities     24842.8ms   <-- 62% of the load
    seq_assets        4540.7ms
    seq_regions       3123.9ms
    seq_objects        546.7ms
    seq_s3d_parse      583.0ms
    seq_bsp_compute    508.3ms
    seq_sky            470.2ms
    seq_simulation     415.0ms
    seq_collision      173.0ms
    seq_env            110.6ms
    seq_doors           33.3ms
    seq_bsp_install     14.5ms

**`seq_entities` is 62% of the Pi's zone-in — matching T187's 64% under S3D.**
Entity work is unchanged and still dominant. This is the comparison T206 said to
run before drawing any conclusion, and it settles the question: the flat-pack /
ETC1 / archive-selection work does not touch the Pi's bottleneck, because the
bottleneck was never file i/o. It is entity building.

That also retires the T206 caveat. T206's flat Pi number was measured with the
33-archive probe still in place, so it was not a fair verdict on EQE1. With the
probe gone the number is the same, which means the probe was not what was
costing the Pi either.

## What EQE1 *did* buy on the Pi

- **Correctness**: zero archives opened that serve no lookup, down from 24. This
  was the point of T207 and it holds on the target platform.
- **Memory**: 46MB mapped instead of 149MB. On a 32-bit ARM with a ~3GB user
  address-space ceiling (T195), 103MB of reclaimed mapping is the real prize --
  consistent with T195's finding that EQE1's cost on the Pi was address space.

So ETC1 + flat-pack on the Pi is a memory story, not a load-time one. That was
the hypothesis carried since T206; it is now measured.

## Next, if the Pi's zone-in time is to be attacked
It is `seq_entities`, and nothing in the EQE1 arc will move it. That is a
separate investigation into entity build cost -- mesh construction, skeleton
setup, per-entity texture binding -- not asset packaging.
