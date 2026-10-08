// Mechanism probe only: no production runtime replacement.
#include "Parser.h"
#include "Phases.h"
#include <cassert>
#include <unordered_map>
struct SourceTranslationTestAccess {
    static void run() {
        using Binding = Parser::VariableBinding;
        std::unordered_map<std::string, Binding> source;
        auto root = std::make_shared<nift::RuntimeValue>(nift::RuntimeValue::make_array());
        root->array.push_back(nift::RuntimeValue(7.0));
        Binding value(std::make_shared<nift::RuntimeValue>(3.0), 1, true, false);
        Binding location(root, 1, true, false);
        location.ref_root_slot = location.slot;
        location.ref_path.push_back(Parser::PathComponent{Parser::PathComponent::Kind::Index, 0, {}});
        location.sync();
        source.emplace("value", value); source.emplace("location", location);
        source.emplace("shadow", value); source.emplace("readonly", value);
        source.at("readonly").mutable_binding = false;
        source.at("readonly").deep_readonly = true;
        source.emplace("invocation", value); source.at("invocation").is_script_invocation = true;
        for (int i = 0; i < 2000; ++i) {
            std::unordered_map<std::string, Binding> old_map, new_map;
            { cp51::Phase old(cp51::Capture); for (const auto& entry : source) old_map[entry.first] = entry.second; }
            { cp51::Phase fresh(cp51::Frame); for (const auto& entry : source) new_map.insert_or_assign(entry.first, entry.second); }
            // Duplicate overwrites retain identical shared slots and flags.
            old_map["shadow"] = location; new_map.insert_or_assign("shadow", location);
            for (const auto& entry : old_map) {
                const auto& a = entry.second; const auto& c = new_map.at(entry.first);
                assert(a.value == c.value && a.slot == c.slot && a.ref_root_slot == c.ref_root_slot);
                assert(a.ref_path == c.ref_path && a.ref_valid == c.ref_valid && a.type == c.type);
                assert(a.mutable_binding == c.mutable_binding && a.deep_readonly == c.deep_readonly);
                assert(a.is_script_invocation == c.is_script_invocation);
            }
            root->array.push_back(nift::RuntimeValue(9.0));
            old_map.at("location").sync(); new_map.at("location").sync();
            assert(old_map.at("location").value == new_map.at("location").value);
            assert(old_map.at("location").value->num == 7.0);
        }
        std::puts("PASS insertion/overwrite/shared-slot/flags/root-path growth mechanism");
    }
};
int main() { SourceTranslationTestAccess::run(); }
