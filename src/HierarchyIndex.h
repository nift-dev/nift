#pragma once
#include "Types.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// Lazy, immutable project-page hierarchy index.
//
// Built once per project (std::call_once) on the first hierarchy query and
// never otherwise. Direct edges only: parent_of and children_of; transitive
// ancestors/descendants/siblings are derived on demand. Construction uses
// indexed lookup (unordered_map name -> tracked index) and processes pages
// top-down by name segment count, memoizing each path prefix's nearest
// existing ancestor, so total work is expected O(N) for representative
// hierarchies. Never stores a transitive closure.
struct HierarchyIndex {
    static constexpr std::size_t NO_PARENT = static_cast<std::size_t>(-1);

    // tracked name -> tracked index
    std::unordered_map<std::string, std::size_t> name_index;
    // per tracked index, the parent's tracked index or NO_PARENT
    std::vector<std::size_t> parent_of;
    // per tracked index, tracked-ordered direct child indices
    std::vector<std::vector<std::size_t>> children_of;

    std::size_t index_of(const std::string& name) const {
        const auto it = name_index.find(name);
        return it == name_index.end() ? NO_PARENT : it->second;
    }

    std::size_t parent_index(std::size_t page) const {
        return page < parent_of.size() ? parent_of[page] : NO_PARENT;
    }

    const std::vector<std::size_t>* children(std::size_t page) const {
        return page < children_of.size() ? &children_of[page] : nullptr;
    }

    // Nearest existing ancestor index for a page, walking direct parent edges
    // (O(depth)).
    std::size_t nearest_ancestor(std::size_t page) const {
        while (page != NO_PARENT && parent_of[page] != NO_PARENT) page = parent_of[page];
        return page;
    }

    static HierarchyIndex build(const std::vector<TrackedInfo>& tracked) {
        HierarchyIndex h;
        const std::size_t n = tracked.size();
        h.name_index.reserve(n);
        h.parent_of.assign(n, NO_PARENT);
        h.children_of.assign(n, {});

        // Top-down processing order: pages with fewer name segments first so an
        // existing ancestor is always indexed before its descendants.
        auto segments = [](const std::string& name) -> std::size_t {
            std::size_t count = 0;
            for (char c : name) if (c == '/') ++count;
            return count;
        };
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return segments(tracked[a].name) < segments(tracked[b].name);
        });

        // prefix path -> nearest existing page index (pages AND transparent
        // intermediates), so each prefix is resolved at most once.
        std::unordered_map<std::string, std::size_t> nearest;
        nearest.reserve(n * 2);

        // The root "/" must be resolvable before any top-level page so they
        // attach to it regardless of tracked order.
        std::size_t root_index = NO_PARENT;
        for (std::size_t i = 0; i < n; ++i)
            if (tracked[i].name == "/") { root_index = i; break; }
        if (root_index != NO_PARENT) {
            h.name_index["/"] = root_index;
            nearest["/"] = root_index;
        }

        for (const std::size_t idx : order) {
            const std::string& name = tracked[idx].name;
            if (name == "/" || name.empty()) continue;  // root handled above
            h.name_index[name] = idx;
            std::size_t parent = NO_PARENT;
            std::string prefix = name;
            std::vector<std::string> seen;
            while (true) {
                const auto slash = prefix.rfind('/');
                if (slash == std::string::npos) {
                    const auto it = nearest.find("/");
                    if (it != nearest.end()) parent = it->second;
                    break;
                }
                prefix = prefix.substr(0, slash);
                const auto it = nearest.find(prefix);
                if (it != nearest.end()) { parent = it->second; break; }
                seen.push_back(prefix);
                if (prefix.empty()) break;
            }
            if (parent == NO_PARENT) {
                const auto it = nearest.find("/");
                if (it != nearest.end()) parent = it->second;
            }
            // Memoize every transparent intermediate prefix to its parent so a
            // later page sharing the prefix resolves it in O(1).
            for (const auto& p : seen) nearest[p] = parent;
            nearest[name] = idx;
            h.parent_of[idx] = parent;
            if (parent != NO_PARENT) h.children_of[parent].push_back(idx);
        }
        return h;
    }
};