#include "rt/compiler_loops_step.h"
#include "rt/string.h"

// Per-iteration and body emission for the default `for` (CP12C slice 2). The
// loop variable is a value binding; a vanished element/key is checked before
// the first body statement that reads it, so earlier side effects stay.

namespace nift::rt::detail {

bool UnitCompiler::for_prepare_bindings(ForState& state, const Node& node) {
    const Node& header = *state.header;
    const Node& body = *state.body;
    scopes_.push();
    KindSet elem_kinds = kKindUnknown;
    {
        const auto stored = kinds_.elements.find(header.a);
        if (stored != kinds_.elements.end() && stored->second != 0) elem_kinds = stored->second;
    }
    if (!state.pair) {
        const bool captured = for_body_captures(body, text(ast_.at(state.names[0])));
        if (!for_declare_binding(text(ast_.at(state.names[0])), header,
                                 captured || selector_roots_.count(state.names[0]), elem_kinds, state.first_binding))
            return false;
    } else {
        const bool key_captured = for_body_captures(body, text(ast_.at(state.names[0])));
        const bool value_captured = for_body_captures(body, text(ast_.at(state.names[1])));
        if (!for_declare_binding(text(ast_.at(state.names[0])), header,
                                 key_captured || selector_roots_.count(state.names[0]), kKindString,
                                 state.first_binding))
            return false;
        if (!for_declare_binding(text(ast_.at(state.names[1])), header,
                                 value_captured || selector_roots_.count(state.names[1]), elem_kinds,
                                 state.second_binding))
            return false;
    }
    auto set_decl = [&](std::string_view name, Binding& b, NodeId decl) {
        b.decl_node = decl;
        b.selector_root_only = selector_roots_.count(decl) && !captured_binding(decl);
        b.location_source = selector_roots_.count(decl);
        scopes_.update(name, b);
    };
    if (!state.pair) set_decl(text(ast_.at(state.names[0])), state.first_binding, state.names[0]);
    else {
        set_decl(text(ast_.at(state.names[0])), state.first_binding, state.names[0]);
        set_decl(text(ast_.at(state.names[1])), state.second_binding, state.names[1]);
    }
    std::uint32_t loop_first = 0;
    state.has_loop = for_body_reads(body, "loop", loop_first);
    if (state.has_loop) {
        const bool captured = for_body_captures(body, "loop");
        if (!for_declare_binding("loop", header, captured, kKindObject, state.loop_binding)) return false;
        state.loop_binding.decl_node = node_id(header);
        scopes_.update("loop", state.loop_binding);
    }

    if (!temp(state.index)) return false;
    fb_->emit(OpCode::LoadI, state.index, 0);
    if (!fb_->constant(Value::integer(-1), state.untyped)) return builder_ok();
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 3
    if (state.first_binding.cell) {
        if (!temp(state.first_flag)) return false;
        fb_->emit(OpCode::LoadI, state.first_flag, 0);
    }
    if (state.pair && state.second_binding.cell) {
        if (!temp(state.second_flag)) return false;
        fb_->emit(OpCode::LoadI, state.second_flag, 0);
    }
#endif

    state.loop_start = fb_->here();
    fb_->set_offset(node.begin);
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 2
    if (state.live && !state.pair) {  // M1b: dynamic length, appended elements are visited
        std::uint16_t fresh = 0;
        bool fresh_temp = false;
        if (!for_read_source(*state.place, fresh, fresh_temp)) return false;
        fb_->emit(OpCode::Length, state.count, fresh, 0);
        if (fresh_temp) for_clear_register(fresh);
    }
#endif
    state.exits.push_back(fb_->emit(OpCode::JmpNotLt, state.index, state.count));
    state.body_start = fb_->here();
    if (plan_.coroutines) fb_->emit(OpCode::Yield);  // CP22: a safepoint every iteration

    state.reads_first = for_body_reads(body, text(ast_.at(state.names[0])), state.first_read);
    state.reads_second =
        state.pair ? for_body_reads(body, text(ast_.at(state.names[1])), state.second_read) : false;
    const bool first_writes = for_body_writes(body, text(ast_.at(state.names[0])));
    const bool first_captured = for_body_captures(body, text(ast_.at(state.names[0])));
    const bool second_writes = state.pair ? for_body_writes(body, text(ast_.at(state.names[1]))) : false;
    const bool second_captured = state.pair ? for_body_captures(body, text(ast_.at(state.names[1]))) : false;
    state.first_used = state.reads_first || first_writes || first_captured;
    state.second_used = state.pair && (state.reads_second || second_writes || second_captured);
    const bool source_changes = for_source_may_change(body, state.place, state.source_name);
    const bool lazy_first = !state.pair && state.first_used && state.live && !state.source_name.empty() &&
                            !state.snapshot_mutant && (source_changes || state.first_binding.cell);
    if (lazy_first) {
        Binding lazy = state.first_binding;
        lazy.lazy_for = true;
        lazy.lazy_source_name = state.source_name;
        lazy.lazy_name = text(ast_.at(state.names[0]));
        lazy.lazy_index = state.index;
        if (!temp(lazy.lazy_active)) return false;
        const Binding& src = *state.place;
        lazy.lazy_source_binding = std::make_shared<const Binding>(src);
        fb_->emit(OpCode::LoadI, lazy.lazy_active, 1);
        state.first_binding = lazy;
        scopes_.update(text(ast_.at(state.names[0])), lazy);
    }
    // A vanished element/key is only possible when the body can change the
    // source; otherwise the value-only opcodes avoid the found flag, the
    // missing check and (for a plain binding) the element copy.
    const bool shrink = for_source_may_shrink(body, state.place, state.source_name);
    if (shrink && ((!state.pair && first_captured) || (state.pair && second_captured)))
        return fail(node, "for-capture-vanishing");
    if (shrink && ((!state.pair && first_writes) || (state.pair && second_writes)))
        return fail(node, "for-missing-write");
    if (state.pair && (shrink || state.second_binding.cell) && state.live && !state.source_name.empty() &&
        !state.snapshot_mutant) {
        Binding lazy = state.second_binding;
        lazy.lazy_for = true;
        lazy.lazy_pair = true;
        lazy.lazy_source_name = state.source_name;
        lazy.lazy_name = text(ast_.at(state.names[1]));
        lazy.lazy_keys = state.keys;
        lazy.lazy_index = state.index;
        if (!temp(lazy.lazy_active)) return false;
        const Binding& src = *state.place;
        lazy.lazy_source_binding = std::make_shared<const Binding>(src);
        fb_->emit(OpCode::LoadI, lazy.lazy_active, 1);
        state.second_binding = lazy;
        scopes_.update(text(ast_.at(state.names[1])), lazy);
    }
    Binding& selected = state.pair ? state.second_binding : state.first_binding;
    if (selected.cell && selected.lazy_for && state.live) {
        selected.selector = true;
        selected.location_source = true;
        if (!temp(selected.selector_reg)) return false;
        scopes_.update(text(ast_.at(state.names[state.pair ? 1 : 0])), selected);
    }
    state.track_missing = shrink && ((!state.pair && state.first_used && !state.first_binding.lazy_for) ||
                                    (state.pair && state.second_used && !state.second_binding.lazy_for));
    return true;
}

bool UnitCompiler::for_step(ForState& state) {
    const Node& node = *state.node;
    // A loop variable nobody reads, writes or captures is not materialised:
    // the body runs for the fixed count, exactly as legacy (its binding has no
    // observable use). This keeps the modifying benchmark at while-loop cost.
    const bool eager_lazy_cell = !state.pair && state.first_used && state.first_binding.lazy_for &&
                                 state.first_binding.cell;
    const bool need_source = state.pair ? state.second_used :
                             (eager_lazy_cell || (state.first_used && !state.first_binding.lazy_for));
    std::uint16_t source = 0;
    bool source_temp = false;
    if (need_source) {
        if (state.live && !state.snapshot_mutant) {
            if (!for_read_source(*state.place, source, source_temp)) return false;
        } else {
            source = state.held;
        }
    }

    if (!state.pair) {
        if (eager_lazy_cell) {
            std::uint16_t value = 0;
            if (!temp(value)) return false;
            fb_->emit(OpCode::ArrayElemValue, value, source, state.index);
            if (source_temp) for_clear_register(source);
            fb_->emit(OpCode::BindType, state.first_binding.type_reg, value, state.untyped);
            if (!for_bind_value(state.first_binding, value, state.first_binding.type_reg, true,
                                state.first_flag))
                return false;
            for_clear_register(value);
        } else if (state.first_used && !state.first_binding.lazy_for) {
            if (!state.track_missing && !state.first_binding.cell) {
                fb_->emit(OpCode::ArrayElemValue, state.first_binding.reg, source, state.index);
                if (source_temp) for_clear_register(source);
                fb_->emit(OpCode::BindType, state.first_binding.type_reg, state.first_binding.reg,
                          state.untyped);
            } else {
                std::uint16_t value = 0, found = 0;
                if (!temp(value) || (state.track_missing && !temp(found))) return false;
                if (state.track_missing) fb_->emit(OpCode::ArrayElem, value, source, state.index);
                else fb_->emit(OpCode::ArrayElemValue, value, source, state.index);
                if (source_temp) for_clear_register(source);
                state.first_missing = found;
                fb_->emit(OpCode::BindType, state.first_binding.type_reg, value, state.untyped);
                if (!for_bind_value(state.first_binding, value, state.first_binding.type_reg, true,
                                    state.first_flag))
                    return false;
                for_clear_register(value);
            }
        }
    } else if (state.first_used || state.second_used) {
        std::uint16_t key = 0, value = 0, found = 0;
        bool value_allocated = false;
        if (!temp(key)) return false;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::GetIndex, key, state.keys, state.index);
        if (state.first_used) {
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 6
            // M5 binds the value first (swapped below); defer the key binding.
#else
            fb_->emit(OpCode::BindType, state.first_binding.type_reg, key, state.untyped);
            if (!for_bind_value(state.first_binding, key, state.first_binding.type_reg, true, state.first_flag))
                return false;
#endif
        }
        if (state.second_used && (!state.second_binding.lazy_for || state.second_binding.cell)) {
            if (!state.track_missing && !state.second_binding.cell) {
                fb_->emit(OpCode::ObjectLookupValue, state.second_binding.reg, source, key);
                if (source_temp) for_clear_register(source);
            } else {
                if (!temp(value) || (state.track_missing && !temp(found))) return false;
                value_allocated = true;
                if (state.track_missing) fb_->emit(OpCode::ObjectLookup, value, source, key);
                else fb_->emit(OpCode::ObjectLookupValue, value, source, key);
                if (source_temp) for_clear_register(source);
                state.second_missing = found;
            }
        }
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 6
        const std::uint16_t bound_value =
            (!state.track_missing && !state.second_binding.cell) ? state.second_binding.reg : value;
        if (state.first_used) {
            const std::uint16_t bound = state.second_used ? bound_value : key;
            fb_->emit(OpCode::BindType, state.first_binding.type_reg, bound, state.untyped);
            if (!for_bind_value(state.first_binding, bound, state.first_binding.type_reg, true, state.first_flag))
                return false;
        }
        if (state.second_used) {
            fb_->emit(OpCode::BindType, state.second_binding.type_reg, key, state.untyped);
            if (!for_bind_value(state.second_binding, key, state.second_binding.type_reg, true, state.second_flag))
                return false;
        }
#else
        if (state.second_used) {
            const std::uint16_t bound =
                (!state.track_missing && !state.second_binding.cell) ? state.second_binding.reg : value;
            fb_->emit(OpCode::BindType, state.second_binding.type_reg, bound, state.untyped);
            if (state.second_binding.cell) {
                if (!for_bind_value(state.second_binding, bound, state.second_binding.type_reg, false,
                                    state.second_flag))
                    return false;
            } else if (state.track_missing && state.second_binding.reg != bound) {
                fb_->emit(OpCode::Move, state.second_binding.reg, bound);
            }
        }
#endif
        for_clear_register(key);
        if (state.second_used && value_allocated) for_clear_register(value);
    }

    const Binding& selected = state.pair ? state.second_binding : state.first_binding;
    if (selected.selector) {
        // The descriptor owns the source SLOT, not a copied iterable Value.
        // Its path is stable while direct writes use the iteration's cache.
        const std::uint16_t mark = fb_->top();
        std::uint16_t payload = 0, parts[8], root = 0, descriptor_type = 0, name = 0;
        if (!temp(payload)) return false;
        for (auto& part : parts) if (!temp(part)) return false;
        if (!binding_cell(*state.place, root)) return false;
        fb_->emit(OpCode::CellSlot, parts[0], root);
        if (state.pair) fb_->emit(OpCode::GetIndex, parts[1], state.keys, state.index);
        else fb_->emit(OpCode::Move, parts[1], state.index);
        fb_->emit(OpCode::Move, parts[2], selected.type_reg);
        if (!fb_->constant(make_string(text(ast_.at(state.names[state.pair ? 1 : 0]))), name) ||
            !temp(descriptor_type)) return false;
        fb_->emit(OpCode::LoadI, descriptor_type, jump_operand(-2));
        fb_->emit(OpCode::LoadK, parts[3], static_cast<std::uint16_t>(name & ~kConstantBit));
        fb_->emit(OpCode::CellAnchor, parts[4], selected.reg);
        fb_->emit(OpCode::LoadBool, parts[5], 0);
        fb_->emit(OpCode::LoadNull, parts[6]);
        fb_->emit(OpCode::Move, parts[7], selected.reg);
        fb_->emit(OpCode::NewArray, payload, parts[0], 8);
        fb_->emit(OpCode::NewCell, selected.selector_reg, payload, descriptor_type);
        for_clear_register(payload);
        for (auto part : parts) for_clear_register(part);
        fb_->release_to(mark);
    }
    if (state.has_loop && !for_emit_loop_metadata(state.loop_binding, state.index, state.count)) return false;
    return true;
}

bool UnitCompiler::for_emit_body(ForState& state) {
    const Node& body = *state.body;
    int first_stmt = -1, second_stmt = -1;
    {
        const NodeId* list = ast_.list(body);
        for (std::uint32_t k = 0; k < body.count; ++k) {
            std::uint32_t off = 0;
            if (first_stmt < 0 && state.track_missing && state.reads_first &&
                for_body_reads(ast_.at(list[k]), text(ast_.at(state.names[0])), off))
                first_stmt = static_cast<int>(k);
            if (state.pair && state.track_missing && second_stmt < 0 && state.reads_second &&
                for_body_reads(ast_.at(list[k]), text(ast_.at(state.names[1])), off))
                second_stmt = static_cast<int>(k);
            if (first_stmt >= 0 && (!state.pair || second_stmt >= 0)) break;
        }
    }
    const NodeId* body_list = ast_.list(body);
    for (std::uint32_t k = 0; k < body.count; ++k) {
        if (static_cast<int>(k) == first_stmt) {
            const std::size_t have = fb_->emit(OpCode::JmpTrue, state.first_missing);
            fb_->set_offset(state.first_read);
            const std::size_t at = fb_->emit(OpCode::IterMissing);
            fb_->note(at, std::string(text(ast_.at(state.names[0]))));
            fb_->patch(have, fb_->here());
        }
        if (static_cast<int>(k) == second_stmt) {
            const std::size_t have = fb_->emit(OpCode::JmpTrue, state.second_missing);
            fb_->set_offset(state.second_read);
            const std::size_t at = fb_->emit(OpCode::IterMissing);
            fb_->note(at, std::string(text(ast_.at(state.names[1]))));
            fb_->patch(have, fb_->here());
        }
        const Node& statement_node = ast_.at(body_list[k]);
        if ((statement_node.kind == NodeKind::Break || statement_node.kind == NodeKind::Continue) &&
            k + 1 < body.count)
            return fail(statement_node, "jump-not-last");
        if (!statement(body_list[k])) return false;
        // Assigning to the loop variable replaces the lazy location: a later
        // read is the new value, not the vanished element.
        if (statement_node.kind == NodeKind::Assign || statement_node.kind == NodeKind::CompoundAssign ||
            statement_node.kind == NodeKind::IncDec) {
            const Node& target = ast_.at(statement_node.a);
            if (target.kind == NodeKind::Ident) {
                if (text(target) == text(ast_.at(state.names[0])) && state.first_missing != 0)
                    fb_->emit(OpCode::LoadI, state.first_missing, 1);  // assigned: present
                if (state.pair && text(target) == text(ast_.at(state.names[1])) && state.second_missing != 0)
                    fb_->emit(OpCode::LoadI, state.second_missing, 1);
            }
        }
    }
    return true;
}

} // namespace nift::rt::detail
