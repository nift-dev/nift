// Explicit presentation calls on struct receivers (B3, B4): the choice between a
// struct's own stringify/prettify/highlight and the built-in renderer.
#include "rt/compiler_presentation.h"

#include "rt/natives/builtin.h"
#include "rt/native.h"

namespace nift::rt::detail {

// B3 (owner ruling, b3-structs.md): an explicit `.stringify()`, `.prettify()`
// or `.highlight()` on a receiver that is not one proven struct. When the
// receiver may be an instance (or is of unknown kind) and some struct of the
// unit or session declares a method of that name, the explicit call would
// have to choose the user method or the built-in at run time (B4's dynamic
// dispatch): refused before any effect, so an explicit call never silently
// bypasses a user method. Otherwise -1: the built-in binds (a container's
// nested instances are rendered by the descriptor renderer, which runs no
// user code). Returns 0 when refused, 1 when compiled (mutant D8 only).
int UnitCompiler::presentation_dispatch(const Node& node, KindSet receiver, std::string_view name,
                                        std::uint16_t dst) {
    int declaring = -1;
    for (std::size_t s = 0; s < plan_.structs.size() && declaring < 0; ++s)
        if (presentation_override(plan_.structs[s], name, node.count) != nullptr) declaring = static_cast<int>(s);
    // Review H1: in a session, every schema any earlier statement declared
    // counts too (its instances may be reachable here without the statement
    // naming the struct). D10 ignores them.
#if !(defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 10)
    const bool in_session = session_ && node.count == 0 && session_->presentation_methods.count(std::string(name));
#else
    const bool in_session = false;
#endif
    if (declaring < 0 && !in_session) return -1;
#if !(defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 9)  // D9: the built-in binds despite the user method
    // B4: a script or module receiver without Unknown kinds dispatches at run
    // time (presentation_select); a session (earlier schemas' instances) or an
    // unknown receiver keeps the refusal.
    if ((receiver & kKindStruct) && !(receiver & kKindUnknown) && !in_session && declaring >= 0 && !session_)
        return presentation_select(node, name, dst) ? 1 : 0;
    if (receiver & (kKindStruct | kKindUnknown))
        return fail(ast_.at(node.a), "presentation-struct-method:" + std::string(name)) ? 1 : 0;
#endif
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 8
    // D8: rendering a container dispatches to the user presentation method.
    if (declaring >= 0 && (receiver & (kKindAggregate | kKindCollection))) {
        const std::uint16_t mark = fb_->top();
        std::uint16_t held = 0;
        if (!temp(held) || !expr_to(ast_.at(node.a).a, held)) return 0;
        const StructInfo& info = plan_.structs[static_cast<std::size_t>(declaring)];
        const bool ok = struct_call(node, declaring, *info.method(name), kNoNode, held, dst);
        fb_->release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
        return ok ? 1 : 0;
    }
#else
    (void)dst;
#endif
    return -1;
}

// B4 (b4-dispatch.md 5): `v.stringify()` / `prettify()` / `highlight()` on a
// receiver that may be an instance of a struct declaring that method. The
// receiver is evaluated once; for each declaring struct, StructIs tests the
// instance's descriptor (unique per declaration and compile, kept across
// isolates) and runs the user method; anything else, an instance of another
// struct included, renders through the built-in (the descriptor renderer: no
// user code, privacy kept). An explicit call never bypasses a user method.
bool UnitCompiler::presentation_select(const Node& node, std::string_view name, std::uint16_t dst) {
    const natives::BuiltinSpec* spec = natives::find_builtin(natives::Receiver::Any, name);
    if (!spec || !plain_postfix(node) || !no_trailing_comma(node)) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t held = 0, test = 0;
    if (!temp(held) || !expr_to(ast_.at(node.a).a, held) || !temp(test)) return false;
    std::vector<std::size_t> done;
    for (std::size_t s = 0; s < plan_.structs.size(); ++s) {
        const StructInfo& info = plan_.structs[s];
        const StructMethodInfo* m = presentation_override(info, name, node.count);
        if (m == nullptr) continue;
        std::uint16_t type = 0;
        const auto descriptor = struct_descriptor(plan_, session_, struct_descriptors_, static_cast<int>(s),
                                                  std::string(info.name));
        if (!fb_->struct_type(descriptor, type)) return builder_ok();
        fb_->emit(OpCode::StructIs, test, held, type);
        const std::size_t skip = fb_->emit(OpCode::JmpFalse, test);
        if (!struct_call(node, static_cast<int>(s), *m, kNoNode, held, dst)) return false;
        done.push_back(fb_->emit(OpCode::Jmp));
        if (!fb_->patch(skip, fb_->here())) return builder_ok();
    }
    const std::uint16_t base = fb_->top();
    if (!operand_window(base, 1)) return false;
    fb_->emit(OpCode::Move, base, held);
    fb_->set_offset(site(node));
    const auto id = static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(spec->id));
    const std::size_t at = fb_->emit(OpCode::CallNative, base, id, 1);
    fb_->note(at, std::string(spec->name));
    fb_->emit(OpCode::Move, dst, base);
    for (const std::size_t jump : done)
        if (!fb_->patch(jump, fb_->here())) return builder_ok();
    fb_->release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
    return builder_ok();
}
} // namespace nift::rt::detail
