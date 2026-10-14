// B1044 (#364): the Darwin memory reading for eqt::memory::Snapshot (Will's
// 2026-10-14 ruling, option 1: platform-scoped memory figures). macOS has no
// /proc/self/status RssAnon; the figure is Mach's task_vm_info.phys_footprint
// (task_info(TASK_VM_INFO)): the task's dirty + compressed + swapped memory,
// including IOSurface/Metal memory charged to the task. resident_size is the
// VmRSS counterpart. Included by memory_ledger.cpp on Darwin only.
#pragma once

#include <mach/mach.h>

namespace eqt::memory {

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

}  // namespace eqt::memory
