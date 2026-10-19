// Memory investigation C2 (permanent regression): interned atoms are charged
// to the thread's live heap total, once, on the intern path only. Before the
// fix the atom table grew without bound outside the VM heap budget
// (memcheck2: 3M distinct object keys under a 16 MiB budget ran to 511 MB
// RSS and exit 0).
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt_test.h"

#include <string>
#include <thread>

using namespace nift::rt;

int main() {
    AtomTable& table = AtomTable::current();
    const std::size_t live0 = live_heap_bytes(), charged0 = heap_charged_bytes(), size0 = table.size();

    // New atoms: charged exactly entry_bytes each (short ones stay inline).
    const std::string longer = "a-fairly-long-generated-key-0001";
    atom("k");
    atom(longer);
    const std::size_t expected = AtomTable::entry_bytes("k") + AtomTable::entry_bytes(longer);
    CHECK(AtomTable::entry_bytes("k") == AtomTable::kAtomEntryOverhead);
    CHECK(AtomTable::entry_bytes(longer) == AtomTable::kAtomEntryOverhead + longer.size() + 1);
    CHECK(live_heap_bytes() - live0 == expected);
    CHECK(heap_charged_bytes() - charged0 == expected);
    CHECK(table.size() == size0 + 2);

    // Re-interning and lookups charge nothing.
    atom("k");
    atom(longer);
    Atom found;
    CHECK(table.find(longer, found));
    CHECK(!table.find("never-interned-key", found));
    CHECK(live_heap_bytes() - live0 == expected);

    // Growth is visible to a budget: 100,000 distinct 32-byte keys.
    const std::size_t before = live_heap_bytes();
    for (int i = 0; i < 100000; ++i) atom("a-fairly-long-generated-key-" + std::to_string(1000000 + i));
    CHECK(live_heap_bytes() - before >= 100000 * (AtomTable::kAtomEntryOverhead + 36));

    // Another thread has its own table and total: nothing is charged here.
    const std::size_t here = live_heap_bytes();
    std::size_t there = 0;
    std::thread([&] {
        atom("");  // creates this thread's table (its "" entry is charged here)
        const std::size_t t0 = live_heap_bytes();
        atom("thread-local-key-longer-than-fifteen");
        there = live_heap_bytes() - t0;
    }).join();
    CHECK(there == AtomTable::entry_bytes("thread-local-key-longer-than-fifteen"));
    CHECK(live_heap_bytes() == here);
    return rt_test::finish("atom-budget");
}
