# T195 — The Pi's EQE1 failure is ADDRESS SPACE, not memory

T193 found the Pi dies with `std::bad_alloc` while opening composite EQE1
archives, and T194's fix attempt assumed that meant RAM. T193's addendum asked
the question that decides the design — *what does `EQE1Loader::open()` actually
retain?* — and said to measure before designing further. This measures it.

## The measurement

Client RSS, VmSize and system MemAvailable sampled every 0.3s on the Pi across
`loadZoneSequential: [48%] Opening EQE1 textures` (21:49:21.293 → 21:49:22.719,
`std::bad_alloc` at .721):

| time | RSS | **VmSize** | MemAvailable |
|---|---|---|---|
| 21:49:21 | 157 MB | **399 MB** | 260 MB |
| 21:49:21 | 165 MB | **1,034 MB** | 260 MB |
| 21:49:21 | 178 MB | **1,837 MB** | 261 MB |
| 21:49:22 | 191 MB | **2,839 MB** | 261 MB |
| 21:49:22 | **197 MB** | **3,128 MB** | 260 MB |
| 21:49:22 | 163 MB | 762 MB | 261 MB |

**RSS rises ~40MB. VmSize rises ~2.7GB, peaking at 3.13GB.** MemAvailable never
moves below ~260MB — the machine is not short of RAM at any point.

## The answer

`EQE1Loader::open()` **mmaps** the archive. Mapping costs address space, not
resident pages. The Pi runs a **32-bit ARM** binary, whose user address space
ceiling is ~3GB — which is exactly where the peak sits when the eager loop maps
the whole composite directory. **The allocation that throws is not a request for
memory the Pi lacks; it is a mapping the process has no address space left to
place.**

This also explains, cleanly, why x86 is unaffected: a 64-bit process has address
space to burn, so mapping 664 archives costs it nothing observable. **The
platform difference is 32-bit vs 64-bit, not 483MB vs 32GB.**

## What this means for the fix

The direction from T193 (don't map everything up front) survives, but the target
metric changes from RSS to VmSize, and that makes a cheaper option available:

- **Indexing by table of contents is now clearly viable.** Open → read TOC →
  **unmap** → record `texture name → archive path`. Address space is reclaimed
  on unmap, so peak VmSize stays at roughly one archive regardless of directory
  size. T194's version failed because it indexed by *filename*, which cannot
  resolve per-part textures (`fis_hs_def_0_0.eqe1` contains `fishe0101.bmp`);
  indexing by TOC fixes exactly that.
- **Then open on demand and keep only referenced archives mapped.** T194
  measured 10 archives actually referenced in befallen on x86, against 664
  mapped eagerly.
- **A 64-bit ARM build would also make the symptom disappear** without touching
  the loader — worth knowing, but it treats the symptom: mapping 1506 archives
  to use 10 is wasteful on any target.

## Method note

Two false starts, both sampling the wrong process: `$!` after `timeout 200 ./willeq…`
is `timeout`'s pid, and `pgrep -f "willeq_t186 -c"` also matches `timeout`'s own
command line. Both reported a flat ~1.3MB RSS, which is the tell. `pgrep -x
willeq_t186` gets the client. Sampler and raw trace in `review/T195/`.
