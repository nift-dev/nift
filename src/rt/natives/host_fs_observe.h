#pragma once
// N2 dependency-observation helpers shared by the cp/mv and file-handle
// operations: a copied source is a read (or absence), a moved source is gone.
#include "rt/natives/host_fs_internal.h"

namespace nift::rt::natives::host_fs_detail {

// N2: legacy observe_copy_source: the source is a dependency if it exists,
// otherwise its absence/unreadability is recorded.
inline void observe_copy_source(const Context& context, const fs::path& source) {
    if (context.host.observer == nullptr) return;
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 15
    return;  // N2 control: cp source observation removed
#else
    std::error_code error;
    if (fs::exists(source, error) && !error) context.host.observer->stream_open(source);
    else context.host.observer->failed_read(source);
#endif
}

// N2: a successful move records the source's absence.
inline void observe_move_absence(const Context& context, const fs::path& source) {
    if (context.host.observer == nullptr) return;
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 16
    return;  // N2 control: mv absence observation removed
#else
    context.host.observer->failed_read(source);
#endif
}

} // namespace nift::rt::natives::host_fs_detail
