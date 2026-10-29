#include "rt/isolate_snapshot.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include <limits>
#include <unordered_map>
#include <unordered_set>
namespace nift::rt {
namespace {
const std::uint16_t* atom_operand(const Instr& in) {
    switch (in.op) {
        case OpCode::ObjectSet: case OpCode::NewStruct:
        case OpCode::StructAdd: case OpCode::FieldSet: return &in.b;
        case OpCode::GetMember: case OpCode::FieldGet: return &in.c;
        default: return nullptr;
    }
}
}
bool snapshot_isolate_program(const Program& source, const TransferProtoMap& map,
                              IsolateTaskInput& input, std::shared_ptr<const Program>& metadata,
                              std::string& error) {
    std::unordered_set<std::uint16_t> used_atoms;
    auto copy = std::make_shared<Program>();
    copy->has_collection_boundaries = source.has_collection_boundaries;
    copy->has_coroutines = source.has_coroutines;
    std::unordered_map<const Proto*, std::size_t> indices;
    for (std::size_t i = 0; i < source.protos.size(); ++i) {
        indices.emplace(source.protos[i].get(), i);
        copy->protos.push_back(std::make_unique<Proto>());
    }
    Value constants = make_array();
    for (std::size_t i = 0; i < source.protos.size(); ++i) {
        const auto& src = *source.protos[i]; auto& dst = *copy->protos[i];
        dst.name = src.name; dst.is_async = src.is_async;
        dst.nparams = src.nparams; dst.nregs = src.nregs;
        dst.collection_expression_view = src.collection_expression_view;
        dst.code = src.code; dst.offsets = src.offsets;
        for (const auto& in : src.code) if (auto operand = atom_operand(in)) used_atoms.insert(*operand);
        dst.notes = src.notes; dst.note_table = src.note_table;
        if (src.collection_entry) dst.collection_entry = copy->protos.at(indices.at(src.collection_entry)).get();
        input.constant_counts.push_back(src.constants.size());
        for (const auto& v : src.constants) array_push(constants, v);
        std::uint32_t wire = std::numeric_limits<std::uint32_t>::max();
        (void)map.index_of(&src, wire); input.proto_wires.push_back(wire);
    }
    if (input.constants_graph.encode(constants, map, error) != TransferStatus::Ok) return false;
    auto& table = AtomTable::current();
    for (auto id : used_atoms) {
        if (id >= table.size()) { error = "invalid source atom operand"; return false; }
        input.atom_names.emplace_back(id, table.text(Atom{id}));
    }
    input.has_program_snapshot = true;
    metadata = std::move(copy);
    return true;
}

bool remap_isolate_atoms(Program& clone, const IsolateTaskInput& input, std::string& error) {
    if (input.has_program_snapshot) {
        std::unordered_map<std::uint16_t, std::uint16_t> atoms;
        for (const auto& name : input.atom_names) {
            const auto id = atom(name.second).id;
            if (id > std::numeric_limits<std::uint16_t>::max())
                { error = "isolate atom operand limit exceeded"; return false; }
            atoms.emplace(name.first, static_cast<std::uint16_t>(id));
        }
        for (auto& proto : clone.protos) for (auto& in : proto->code) {
            if (auto source = atom_operand(in)) {
                const auto found = atoms.find(*source);
                if (found == atoms.end()) { error = "invalid source atom operand"; return false; }
                auto* operand = (in.op == OpCode::GetMember || in.op == OpCode::FieldGet) ? &in.c : &in.b;
                *operand = found->second;
            }
        }
    }
    return true;
}
}
