// RECHECK2 rc2/forge/g20: a forged header. The unit does not include
// rt/mutation.h; it declares its own public SortKey and Protocol::sort_storage
// with the real mangled name (an ODR violation of SortKey and Protocol:
// ill-formed, no diagnostic required, so no compiler refuses it), and calls the
// real out-of-line commit.
#include "rt/value.h"
#include <vector>
namespace nift::rt::mutation {
class SortKey { public: SortKey() {} };
class Protocol { public: static std::vector<Value>& sort_storage(const SortKey&, Value&); };
}
void nift_probe_hijack(nift::rt::Value& v) { nift::rt::mutation::Protocol::sort_storage(nift::rt::mutation::SortKey(), v).clear(); }
