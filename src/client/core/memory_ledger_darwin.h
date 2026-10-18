// B1044 (#364): the Darwin memory reading for eqt::memory::Snapshot (Will's
// 2026-10-14 ruling, option 1: platform-scoped memory figures). macOS has no
// /proc/self/status RssAnon; the figure is Mach's task_vm_info.phys_footprint
// (task_info(TASK_VM_INFO)): the task's dirty + compressed + swapped memory,
// including IOSurface/Metal memory charged to the task. resident_size is the
// VmRSS counterpart.
//
// B1044-nf: this header carries ALL of the Darwin additions to
// memory_ledger.cpp in one place, guarded so it is safe to include on EVERY
// platform (the host calls the helpers unconditionally; on Linux they are
// no-ops). That keeps the merged host file under the 1,500-line ceiling, and
// Linux preprocessor output of memory_ledger.cpp is unchanged: the no-op
// readDarwinFootprint() writes nothing and darwinLogExtra()/darwinJsonExtra()
// return the empty string, so the log line and JSON are byte-identical to a
// build with no Darwin arm. Included by memory_ledger.cpp after
// "client/core/memory_ledger.h".
//
// The log/JSON helpers do not call the host's appendKV (it lives in the host's
// anonymous namespace); they build the same " key=value " fragments by hand.
#pragma once

#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

namespace eqt::memory {

#if defined(__APPLE__)

inline void readDarwinFootprint(Snapshot& s) {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS)
        return;
    s.physFootprintBytes = static_cast<uint64_t>(info.phys_footprint);
    s.vmRssBytes = static_cast<uint64_t>(info.resident_size);
    s.ownedPhysFootprintBytes =
        s.physFootprintBytes > s.gpuDriverBytes ? s.physFootprintBytes - s.gpuDriverBytes : 0;
}

// toLogLine(): the keys the host appends after "over_ceiling".
inline std::string darwinLogExtra(const Snapshot& s) {
    std::string out;
    out += " phys_footprint=" + std::to_string(s.physFootprintBytes) + " ";
    out += "owned_phys_footprint=" + std::to_string(s.ownedPhysFootprintBytes);
    return out;
}

// toJson(): the fields the host appends after "overCeilingBytes".
inline std::string darwinJsonExtra(const Snapshot& s) {
    std::string out;
    out += ",\"physFootprintBytes\":" + std::to_string(s.physFootprintBytes);
    out += ",\"ownedPhysFootprintBytes\":" + std::to_string(s.ownedPhysFootprintBytes);
    return out;
}

#else

// Linux / no-Mach: no-op so the host's unconditional calls compile and the
// emitted log line and JSON are unchanged.
inline void readDarwinFootprint(Snapshot&) {}
inline std::string darwinLogExtra(const Snapshot&) { return std::string(); }
inline std::string darwinJsonExtra(const Snapshot&) { return std::string(); }

#endif

}  // namespace eqt::memory
