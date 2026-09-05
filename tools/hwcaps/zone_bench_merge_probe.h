#pragma once
// P09 A-gate: cross-region same-material zone draw merge -- risky-primitive
// micro-benchmark. Extends the P08 --synthetic-draws machinery (see
// runSyntheticDrawTest in zone_bench_frame.cpp, T387) with a sibling test mode
// that measures the SUBMISSION-TIME primitives the three P09 candidate
// architectures depend on, at matched geometry:
//
//   1. separate-vbo   : N independent VBO+IBO objects, full bind+attrib
//                       setup each draw (today's per-region-per-material
//                       draw, baseline).
//   2. shared-coalesced: one shared static VBO+IBO (regions' geometry laid
//                       out contiguously at "load time"); N glDrawElements
//                       calls against the SAME bound buffer (no rebind), AND
//                       a single glDrawElements spanning all N regions'
//                       index ranges in one call (the best-case coalesce:
//                       candidate architecture #2, shared-VBO + draw-time
//                       index-range coalescing).
//   3. streamed-ibo   : per-"frame" CPU gather of N regions' indices into a
//                       scratch host buffer, glBufferData/SubData upload
//                       into a GL_DYNAMIC_DRAW element buffer, one
//                       glDrawElements (candidate architecture #3a).
//   4. client-index   : same per-frame CPU gather, but glDrawElements
//                       against a client-memory pointer (GL_ELEMENT_ARRAY_
//                       BUFFER unbound) -- no GL buffer upload at all
//                       (candidate architecture #3b; GLES2 permits
//                       client-side index arrays per spec 2.9.2).
//
// This is a self-contained raw-GLES2 harness (own shader, own VBO/IBO
// objects) sharing only the already-current EGL context from ZoneBenchData
// -- deliberately independent of the Irrlicht driver so the four primitives
// are measured with identical, controlled bind/attrib state instead of
// relying on Irrlicht's internal redundant-state cache (whose behavior we
// don't want to accidentally be measuring).
//
// New TU per refactor/P08_draw_call_investigation.md's precedent
// (zone_bench_loader.cpp is at/near the 3000-line FAIL cap; new machinery
// goes in a sibling TU, see zone_bench_histogram.cpp).

namespace ZoneBench { struct ZoneBenchData; }

namespace ZoneBench {

// Runs the four-primitive sweep and prints a results table to stdout
// (plus an optional CSV if csvPath is non-null/non-empty). numFrames is the
// number of measured repetitions per (primitive, N) cell, mirroring
// --synthetic-draws' use of --frames.
void runMergeProbeTest(ZoneBenchData& data, int numFrames, const char* csvPath);

} // namespace ZoneBench
