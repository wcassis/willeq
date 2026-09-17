#include "client/graphics/load_mem_audit.h"

#include <cstdio>
#include <cstring>

// Output goes to stdout via printf rather than LOG_INFO: these lines are
// flag-gated measurement output (only emitted under --load-mem-audit), and
// LOG_INFO is suppressed at the default NONE log level — which silently
// swallowed the samples in gles2_zone_benchmark runs. printf matches the
// benchmark's own reporting convention and keeps the lines greppable in
// willeq runs regardless of --log-level.

namespace EQT { namespace Graphics {

ProcessRssKb readProcessRssBreakdownKb() {
    ProcessRssKb out;
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return out;

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &out.rssKb);
        } else if (std::strncmp(line, "RssAnon:", 8) == 0) {
            std::sscanf(line + 8, "%ld", &out.anonKb);
        } else if (std::strncmp(line, "RssFile:", 8) == 0) {
            std::sscanf(line + 8, "%ld", &out.fileKb);
        }
    }
    std::fclose(f);
    return out;
}

long readProcessRssKb() {
    return readProcessRssBreakdownKb().rssKb;
}

void logLoadMemAuditStep(const char* stepName,
                          size_t eqe1MappedBytes,
                          size_t textureCacheBytes,
                          size_t meshCacheBytes,
                          size_t entityPrepBytes) {
    ProcessRssKb rss = readProcessRssBreakdownKb();
    size_t accountedBytes = eqe1MappedBytes + textureCacheBytes + meshCacheBytes + entityPrepBytes;
    long accountedKb = static_cast<long>(accountedBytes / 1024);
    long untrackedKb = rss.rssKb - accountedKb;

    printf("[LOAD-MEM-AUDIT] step=%s rss_kb=%ld anon_kb=%ld file_kb=%ld eqe1_mapped=%zu texcache=%zu "
           "meshcache=%zu entityprep=%zu accounted_kb=%ld untracked_kb=%ld\n",
           stepName, rss.rssKb, rss.anonKb, rss.fileKb, eqe1MappedBytes, textureCacheBytes,
           meshCacheBytes, entityPrepBytes, accountedKb, untrackedKb);
    fflush(stdout);
}

}} // namespace EQT::Graphics
