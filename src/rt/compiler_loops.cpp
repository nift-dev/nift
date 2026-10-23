#include "rt/compiler_loops.h"

#include "rt/compiler_loops_step.h"

// CP12C slice 2: the default `for` (owner decisions A-D, 2026-10-22).
// This file is the orchestration, the source/header setup and the loop
// epilogue; the per-iteration/body passes are in compiler_loops_step.cpp.

namespace nift::rt::detail {

bool UnitCompiler::for_loop(const Node& node) {
    ForState state;
    state.node = &node;
    state.header = &ast_.at(node.a);
    state.body = &ast_.at(node.b);
    state.pair = (state.header->flags & kFlagPairBinding) != 0;
    state.names = ast_.list(*state.header);
    state.saved_textual = textual_statement_;
    ++loop_depth_;
    textual_statement_ = false;
    update_flags();
    if (!for_setup(state, node)) return false;
    if (state.error_only) {
        --loop_depth_;
        textual_statement_ = state.saved_textual;
        update_flags();
        return builder_ok();
    }
    if (!for_prepare_bindings(state, node)) return false;
    Loop loop;
    loop.start = state.loop_start;
    loops_.push_back(loop);
    if (!for_step(state)) return false;
    if (!for_emit_body(state)) return false;
    if (!for_finish(state)) return false;
    return builder_ok();
}

bool UnitCompiler::for_setup(ForState& state, const Node& node) {
    const Node& header = *state.header;
    const Node& iterable = ast_.at(header.a);
    const KindSet source_kinds = kinds(header.a);
    const bool unknown = (source_kinds & kKindUnknown) != 0;
    const bool has_array = (source_kinds & kKindArray) != 0;
    const bool has_object = (source_kinds & kKindObject) != 0;
    if (unknown) return fail(node, "type:for-source");
    if (has_array && has_object) return fail(node, "type:for-source-union");

    state.mode = state.pair ? 1 : 0;
    state.force_mode = -1;  // the loop always raises at its header; no body runs
    if (!state.pair && has_array) state.mode = 0;
    else if (state.pair && has_object) state.mode = 1;
    else if (!has_array && !has_object) state.force_mode = 2;
    else if (state.pair && has_array) state.force_mode = 3;
    else state.force_mode = 4;  // single over an object
    state.error_only = state.force_mode >= 0;

    if (!state.error_only) for_source_binding(iterable, state.place);
    if (iterable.kind == NodeKind::Ident) state.source_name = text(iterable);
    state.live = !state.error_only && state.place != nullptr;
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 1
    state.snapshot_mutant = state.live;  // M1: a snapshot instead of live re-reads
#endif

    if (!state.error_only) {
        if (state.live && !state.snapshot_mutant) {
            if (!for_read_source(*state.place, state.entry, state.entry_temp)) return false;
        } else {
            if (!temp(state.held) || !expr_to(header.a, state.held)) return false;
            state.entry = state.held;
            state.entry_temp = false;
        }
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::IterCheck, state.entry, static_cast<std::uint16_t>(state.mode));
    } else {
        if (iterable.kind == NodeKind::Ident) {
            const Binding* binding = find_binding(text(iterable));
            if (binding != nullptr) {
                if (!for_read_source(*binding, state.entry, state.entry_temp)) return false;
            } else {
                if (!temp(state.held) || !expr_to(header.a, state.held)) return false;
                state.entry = state.held;
            }
        } else {
            if (!temp(state.held) || !expr_to(header.a, state.held)) return false;
            state.entry = state.held;
        }
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::IterCheck, state.entry, static_cast<std::uint16_t>(state.force_mode));
        if (state.entry_temp) for_clear_register(state.entry);
        return true;
    }

    if (!state.pair) {
        if (!temp(state.count)) return false;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::Length, state.count, state.entry, 0);
    } else {
        if (!temp(state.keys)) return false;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::ObjectKeys, state.keys, state.entry);
        if (!temp(state.count)) return false;
        fb_->emit(OpCode::Length, state.count, state.keys, 0);
    }
    if (state.entry_temp) for_clear_register(state.entry);
    return true;
}

bool UnitCompiler::for_finish(ForState& state) {
    const Node& node = *state.node;
    // Advance; `continue` lands here (or, in M3, on the condition).
    const std::size_t increment = fb_->here();
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 4
    for (std::size_t at : loops_.back().continues) fb_->patch(at, state.loop_start);  // M3: no advance
#else
    for (std::size_t at : loops_.back().continues) fb_->patch(at, increment);
#endif
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::AddI, state.index, state.index, jump_operand(1));
    const std::size_t back = fb_->emit(OpCode::Jmp);
    fb_->patch(back, state.loop_start);

    // Clean up iterator state on every normal/break exit (M4: breaks skip it).
    const std::size_t cleanup = fb_->here();
    for (std::size_t at : state.exits) fb_->patch(at, cleanup);
#if !(defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 5)
    for (std::size_t at : loops_.back().breaks) fb_->patch(at, cleanup);
#endif
    if (state.pair) for_clear_register(state.keys);
    if (!state.live || state.snapshot_mutant) for_clear_register(state.held);
    if (state.has_loop) for_clear_register(state.loop_binding.reg);
    for_clear_register(state.first_binding.reg);
    if (state.pair) for_clear_register(state.second_binding.reg);
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 5
    const std::size_t after_cleanup = fb_->here();
    for (std::size_t at : loops_.back().breaks) fb_->patch(at, after_cleanup);
#endif

    loops_.pop_back();
    scopes_.pop();
    --loop_depth_;
    textual_statement_ = state.saved_textual;
    update_flags();
    if (!statement_preparable(node)) return fail(node, "prepared-unpreparable");
    return builder_ok();
}

} // namespace nift::rt::detail
