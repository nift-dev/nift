#include "rt/compiler_session_native.h"
#include "rt/builtin_bind.h"
// rt-lint: cold-file guarded native family selection; no runtime name lookup
namespace nift::rt::detail {
int UnitCompiler::session_native_call(const Node& node, NodeId receiver, std::string_view name, std::uint16_t dst) {
    std::vector<SessionMethodCandidate> candidates;
    if (!session_method_candidates(kinds(receiver), name, candidates)) return -1;
    for (const auto& candidate : candidates)
        if (!builtin_arguments_ok(node, *candidate.spec, receiver)) return 0;
    const auto mark = fb_->top();
    const auto base = mark;
    std::uint16_t reg = 0;
    if (!temp(reg) || !expr_to(receiver, reg)) return 0;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!temp(reg) || !expr_to(ast_.list(node)[k], reg)) return 0;
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 11
    std::uint16_t ignored = 0;
    if (!temp(ignored) || !expr_to(receiver, ignored)) return 0;
#endif
    std::uint16_t tag = 0, untyped = 0;
    if (!temp(tag) || !fb_->constant(Value::integer(-1), untyped)) return 0;
    fb_->set_offset(site(node));
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 10
    fb_->emit(OpCode::LoadI, tag, 4);  // force the string native for an array
#else
    fb_->emit(OpCode::BindType, tag, base, untyped);
#endif
    fb_->keep_type(tag);  // consumed by the branch, not an ordinary binding check
    std::vector<std::size_t> done;
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        std::size_t next = 0;
        if (k + 1 < candidates.size()) {
            std::uint16_t expected = 0;
            if (!fb_->constant(Value::integer(candidates[k].tag), expected)) return 0;
            next = fb_->emit(OpCode::JmpNotEq, tag, expected);
        }
        const auto& spec = *candidates[k].spec;
        const auto call = fb_->emit(OpCode::CallNative, base,
            static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(spec.id)),
            static_cast<std::uint16_t>(node.count + 1));
        fb_->note(call, std::string(spec.name));
        if (k + 1 < candidates.size()) {
            done.push_back(fb_->emit(OpCode::Jmp));
            if (!fb_->patch(next, fb_->here())) return 0;
        }
    }
    for (const auto jump : done) if (!fb_->patch(jump, fb_->here())) return 0;
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok() ? 1 : 0;
}
} // namespace nift::rt::detail
