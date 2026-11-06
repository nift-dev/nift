#include "rt/infer_prune.h"

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

namespace {
constexpr KindSet kAggregate = kKindArray | kKindObject;
} // namespace

// F1: a value is materialized at every read, so the same type object is read
// many times between two changes of the site facts: the result is kept per
// type object until any site fact changes (site_version_), with the classes
// the result depends on, whose readers list must still name each reading unit.
TyRef Inference::materialize(const TyRef& t) {
    if (!t || t->any || !(t->kinds & kAggregate)) return t;
    if (memo_version_ != site_version_) materialized_.clear(), memo_version_ = site_version_;
    const auto hit = materialized_.find(t.get());
    const std::size_t unit = current_unit();
    if (hit != materialized_.end() && hit->second.in == t) {
        if (hit->second.unit != unit) {  // the lists only grow while the version holds
            for (std::uint32_t s : hit->second.read) readers_[s].insert(unit);
            hit->second.unit = unit;
        }
        return hit->second.out;
    }
    std::vector<std::uint32_t> read;
    TyRef out = materialize_now(t, read);
    ++result_.work.materializations;
    for (std::uint32_t s : read) readers_[s].insert(unit);  // H2: this unit depends on them
    if (memo_version_ == site_version_) materialized_[t.get()] = Materialized{t, out, std::move(read), unit};
    return out;
}

// F1: a stored value as every read of it gives it (prune_part): an opened
// class's structure below is anything, so the contents stay small.
TyRef Inference::pruned(const TyRef& value) {
    std::vector<std::uint32_t> read;  // a store reads nothing
    const TyRef next = prune_part(value, read, 0);
    return next ? next : value;
}

void Inference::prune_parts(std::shared_ptr<Ty>& t, bool& changed, std::vector<std::uint32_t>& read) {
    if (TyRef e = prune_part(t->elem, read, 1)) t->elem = std::move(e), changed = true;
    for (auto& entry : t->fields)
        if (TyRef f = prune_part(entry.second, read, 1)) entry.second = std::move(f), changed = true;
    for (auto& item : t->items)
        if (TyRef i = prune_part(item, read, 1)) item = std::move(i), changed = true;
}

// F1: a part whose class reads as anything (opened: past 16 sites, or exposed
// to earlier session code) is anything at every depth
// below it: its own read would give exactly that (materialize), so only the
// structure the stores no longer track is dropped, and the types stay small.
// The part's classes join `read` (the reading unit is walked again when one
// opens later). Null when nothing changes (no copy is made then).
TyRef Inference::prune_part(const TyRef& part, std::vector<std::uint32_t>& read, int depth) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 31
    return nullptr;  // A31: past 16 sites the stale structure stays (the collapse keeps its precision)
#endif
    if (opened_.empty() || !part || part->any || !(part->kinds & kAggregate)) return nullptr;
    bool unknown = false;  // a part of unknown origin is left as it is (its read is anything too)
    const std::vector<std::uint32_t> parts = classes(part->sites, unknown);
    bool open = false;
    for (std::uint32_t s : parts) {
        if (opened_.count(s)) open = true;
        read.push_back(s);
    }
    if (open) {
        if (part->elem == top_ty() && part->fields.empty() && part->items.empty() && !part->tuple) return nullptr;
        auto next = std::make_shared<Ty>(*part);
        next->sites = with_classes(part->sites, parts);
        next->elem = top_ty();
        next->fields.clear();
        next->tuple = false;
        next->items.clear();
        return next;
    }
    if (depth >= 6) return nullptr;
    std::shared_ptr<Ty> copy;  // made on the first changed child
    const auto touch = [&]() -> Ty& {
        if (!copy) copy = std::make_shared<Ty>(*part);
        return *copy;
    };
    if (TyRef e = prune_part(part->elem, read, depth + 1)) touch().elem = std::move(e);
    for (const auto& entry : part->fields)
        if (TyRef f = prune_part(entry.second, read, depth + 1)) touch().fields[entry.first] = std::move(f);
    for (std::size_t k = 0; k < part->items.size(); ++k)
        if (TyRef i = prune_part(part->items[k], read, depth + 1)) touch().items[k] = std::move(i);
    return copy;
}

} // namespace nift::rt::infer_detail
