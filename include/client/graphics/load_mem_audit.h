#pragma once

// P04: flag-gated per-step memory sampling for loadZoneSequential's step
// methods (renderer_zone_load.cpp) and, so results are comparable, for the
// benchmark tool's zone loader. See refactor/P04_loader_memory_audit.md.
//
// Every sample is a single log line so it can be grep'd out of a run's log
// and diffed step-by-step:
//   [LOAD-MEM-AUDIT] step=<name> rss_kb=<N> anon_kb=<N> file_kb=<N> eqe1_mapped=<B>
//     texcache=<B> meshcache=<B> accounted=<B> untracked_kb=<N>
//
// "accounted" is the sum of the known pools passed in; "untracked_kb" is
// rss_kb - accounted/1024 — the same "gap" /pmem's final line reports,
// computed identically here so the two are directly comparable.
//
// T490 (Will's ruling, 2026-09-14): anon_kb (RssAnon) and file_kb (RssFile)
// are reported beside VmRSS because the OOM killer charges ANONYMOUS memory.
// Both Iceclad kills on the Orange Pi were anon-rss (233 MB / 318 MB) with
// file-rss under 220 kB; the page cache (mapped EQE1 packs, the binary) is
// reclaimable and not what kills the client, so a ceiling comparison uses
// anon_kb, never rss_kb.

#include <cstddef>
#include <cstdint>

namespace EQT { namespace Graphics {

// Reads VmRSS out of /proc/self/status. Returns 0 on any read failure
// (e.g. non-Linux build) rather than throwing — this is a diagnostic path.
long readProcessRssKb();

// VmRSS / RssAnon / RssFile out of /proc/self/status, in kB; 0 for any field
// the kernel does not report (RssAnon/RssFile need Linux >= 4.5).
struct ProcessRssKb {
    long rssKb = 0;
    long anonKb = 0;
    long fileKb = 0;
};
ProcessRssKb readProcessRssBreakdownKb();

// Logs one [LOAD-MEM-AUDIT] line via LOG_INFO(MOD_GRAPHICS, ...). Safe to call
// unconditionally; callers should still gate on the --load-mem-audit /
// config_.constrainedConfig.loadMemAudit flag to avoid the /proc read and log
// spam in normal runs.
void logLoadMemAuditStep(const char* stepName,
                          size_t eqe1MappedBytes,
                          size_t textureCacheBytes,
                          size_t meshCacheBytes,
                          size_t entityPrepBytes);

}} // namespace EQT::Graphics
