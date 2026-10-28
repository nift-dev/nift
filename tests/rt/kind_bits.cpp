// CP12C Slice 1 R6: the widened kind set has distinct Function/Bytes/Error
// bits. The test-only -DNIFT_KIND_MUTANT build aliases Function and Bytes and
// must fail here (a compiling negative control, not a build failure).
#include "rt/infer.h"

#include <cstdio>

int main() {
    using namespace nift::rt;
    const bool distinct = kKindFunction != kKindBytes && kKindError != kKindFunction && kKindError != kKindBytes &&
                          kKindUnknown != kKindFunction && kKindUnknown != kKindBytes && kKindUnknown != kKindError &&
                          kKindHandle != kKindUnknown && kKindFile != kKindStream;
    const bool disjoint = (kKindFunction & kKindBytes) == 0 && (kKindError & (kKindFunction | kKindBytes)) == 0 &&
                          (kKindUnknown & kKindAll) == 0 && (kKindHandle & kKindAll) == 0 &&
                          (kKindFile & kKindStream) == kKindHandle;
    if (!distinct || !disjoint) {
        std::printf("FAIL kind bits alias: function=%u bytes=%u error=%u unknown=%u\n",
                    unsigned(kKindFunction), unsigned(kKindBytes), unsigned(kKindError), unsigned(kKindUnknown));
        return 1;
    }
    std::printf("PASS kind bits distinct\n");
    return 0;
}
