#!/usr/bin/env python3
"""B978: the ONE Python definition of willeq-owned memory, the figure the
64 MB target is read against.

Will, 2026-09-29 (B933): "64 MB excludes driver memory" -- the ledger's
gpu_driver category is subtracted from RssAnon.
Will, 2026-10-01 (B978): "1) yes" to "does gpu_buffers count toward the 64 MB
target" -- GPU buffers holding willeq's textures/meshes/render targets are
willeq's own memory.

    owned = RssAnon - gpu_driver + gpu_buffers

gpu_buffers is added only when the row lives OUTSIDE RssAnon (Lima GEM on the
Orange Pi: sources drm_fdinfo, memcg_shmem, drm_maps_lower_bound, logical). On
a software rasteriser (llvmpipe, x86: source logical_in_gpu_driver) the same
bytes are heap inside gpu_driver already, and adding them would count them
twice. Mirrors eqt::memory::finishTargetGap / ownedPlusGpuBuffers
(include/client/core/gpu_buffer_census.h). The 256 MB failing bound stays on
whole RssAnon.

Every caller (scripts/ci/visual_gate.py, scripts/pi/*.py,
scripts/client_ledger_table.py) imports this module rather than re-deriving
the arithmetic.
"""

OWNED_LABEL = "owned=RssAnon-gpu_driver+gpu_buffers"
TARGET_BYTES = 64 * 1024 * 1024

# gpu_buffer_census.h GpuBufferSource tokens whose bytes are OUTSIDE RssAnon.
OUTSIDE_RSS_ANON_SOURCES = frozenset(("drm_fdinfo", "memcg_shmem", "drm_maps_lower_bound", "logical"))


def gpu_buffers_outside(source, outside_flag=None):
    """True when the gpu_buffers row is outside RssAnon and so adds to owned.
    An explicit flag (the [memgpu] line's outside_rss_anon=, the [memledger]
    line's gpu_buffers_outside_rss_anon=, the JSON's outsideRssAnon) wins; the
    source token decides for logs written before B978 carried the flag."""
    if outside_flag is not None and outside_flag != "":
        return str(outside_flag).lower() in ("1", "true", "yes", "y")
    return (source or "") in OUTSIDE_RSS_ANON_SOURCES


def owned_rss_anon(rss_anon, gpu_driver):
    """RssAnon - gpu_driver, clamped at 0 (the B933 figure, now a component)."""
    return max(0, int(rss_anon or 0) - int(gpu_driver or 0))


def owned(rss_anon, gpu_driver, gpu_buffers=0, outside=False):
    """owned = RssAnon - gpu_driver + gpu_buffers (only when outside RssAnon)."""
    return owned_rss_anon(rss_anon, gpu_driver) + (int(gpu_buffers or 0) if outside else 0)


def owned_from_memgpu(kv):
    """(owned, owned_rss_anon, gpu_buffers_added) from one parsed [memgpu] line.

    Reads BOTH shapes: since B978 the line carries owned_rss_anon= and owned=
    (the target figure); before it, owned= was RssAnon-gpu_driver and
    owned_plus_gpu_buffers= the sum (B955). The presence of owned_rss_anon=
    tells them apart. Returns (None, None, None) when the line has neither."""
    out = gpu_buffers_outside(kv.get("source"), kv.get("outside_rss_anon"))
    buffers = int(kv.get("gpu_buffers", 0) or 0) if out else 0
    if "owned_rss_anon" in kv:
        anon = int(kv["owned_rss_anon"])
        return int(kv.get("owned", anon + buffers)), anon, buffers
    if "owned" in kv:
        anon = int(kv["owned"])
        if "owned_plus_gpu_buffers" in kv:
            return int(kv["owned_plus_gpu_buffers"]), anon, int(kv["owned_plus_gpu_buffers"]) - anon
        return anon + buffers, anon, buffers
    return None, None, None


def owned_from_ledger_json(ledger):
    """(owned, owned_rss_anon, gpu_buffers_added, outside) from a ledger JSON
    object (eqt::memory::toJson / the benchmark's --memory-json "ledger").
    Computed from the components, not from ownedBytes, so a binary built
    before B978 prints the same figure."""
    rss_anon = ledger.get("rssAnonBytes", 0)
    gpu_driver = ledger.get("categories", {}).get("gpu_driver", {}).get("live", 0)
    gb = ledger.get("gpuBuffers") or {}
    out = gpu_buffers_outside(gb.get("source"), gb.get("outsideRssAnon"))
    buffers = int(ledger.get("gpuBuffersBytes", gb.get("gpuBuffersBytes", 0)) or 0)
    anon = owned_rss_anon(rss_anon, gpu_driver)
    added = buffers if out else 0
    return anon + added, anon, added, out
