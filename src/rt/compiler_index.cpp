#include "rt/compiler_index.h"
#include "rt/compiler_call.h"
#include "rt/legacy_names.h"
#include "rt/compiler_handles.h"
#include <algorithm>
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/builtin_bind.h"
#include "rt/compiler_native_args.h"
#include "rt/native.h"
#include "rt/module_data.h"
#include "rt/module_link.h"
#include "rt/op_admission.h"
#include "rt/prepared_parse.h"
#include "rt/source_scan.h"
#include "rt/vm_mutants.h"
namespace nift::rt::detail {
bool UnitCompiler::index(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    std::string location;
    if (!argument_path(node, location)) return false;
    if (index_depth_ > 0) return fail(node, "path-in-index");
    const KindSet object_kinds = kinds(node.a), key_kinds = kinds(node.b);
    const bool array = (object_kinds & ~kKindArray) == 0;
    const bool bytes = object_kinds == kKindBytes && key_kinds && !(key_kinds & ~kKindNumber) &&
        (!loop_reachable() || (loop_region() && key_kinds == kKindInt && ast_.at(node.a).kind == NodeKind::Ident &&
                              ast_.at(node.b).kind == NodeKind::Ident));
    const bool object = (object_kinds & ~kKindObject) == 0 && (key_kinds & ~kKindString) == 0;
    const Node& key_ast = ast_.at(node.b);
    // A loop print argument is re-parsed textually; it rejects object length().
    const bool print_index = print_span_ && node.begin >= print_span_->begin && node.end <= print_span_->end;
    if (print_index && ast_.at(node.a).kind == NodeKind::Ident && loop_region() &&
        key_ast.kind == NodeKind::Call && key_ast.count == 0) {
        const Node& callee = ast_.at(key_ast.a);
        if (callee.kind == NodeKind::Member &&
            text(ast_.at(callee.b)) == "length" &&  // rt-lint: allow-string-compare compile time
            ast_.at(callee.a).kind == NodeKind::Ident && kinds(callee.a) == kKindObject) {
            const bool ok = legacy_fatal(node, "unknown value or malformed expression: " +
                                               std::string(text(key_ast)), dst);
            return ok;
        }
    }
    const bool bad_bytes_key = object_kinds == kKindBytes && key_kinds == kKindString &&
        !loop_reachable() && !session_ && key_ast.kind == NodeKind::Ident;
    const bool bad_boolean_key = in_lambda_ && !session_ && !loop_reachable() &&
                                 return_read_offset_ == kNoNode && object_kinds == kKindArray &&
                                 (key_ast.kind == NodeKind::True || key_ast.kind == NodeKind::False);
    const Node& receiver = ast_.at(node.a);
    const Binding* parameter = in_lambda_ && receiver.kind == NodeKind::Ident ? find_binding(text(receiver)) : nullptr;
    const bool null_parameter = parameter && parameter->decl_node != kNoNode &&
        ast_.at(parameter->decl_node).kind == NodeKind::Param && !session_ && !loop_reachable() &&
        return_read_offset_ == kNoNode && object_kinds == kKindNull && key_ast.kind == NodeKind::Number;
    const Node* null_call = null_parameter && (!module_names_ || module_names_->empty())
        ? direct_null_index_call(ast_, root_, node_id(node)) : nullptr;
    const bool null_script_index = object_kinds == kKindNull && receiver.kind == NodeKind::Ident && !session_ &&
        key_ast.kind == NodeKind::String && !key_ast.count && key_ast.a == kNoNode && print_arg_ == &node &&
        !in_function_ && !loop_reachable() && !plan_.coroutines && (!module_names_ || module_names_->empty());
    const Binding* local_index = receiver.kind == NodeKind::Ident ? find_binding(text(receiver)) : nullptr;
    const bool local_value_index = local_index && !local_index->global && !local_index->cell &&
        local_index->decl_node != kNoNode && ast_.at(local_index->decl_node).kind == NodeKind::Ident;
    const NodeKind index_receiver_kind = ast_.at(node.a).kind;
    const bool non_ident_index = index_receiver_kind == NodeKind::Call ||
        index_receiver_kind == NodeKind::Member || index_receiver_kind == NodeKind::Index;
    const bool unproved_index = object_kinds == 0 || (object_kinds & (kKindUnknown | kKindNull)) != 0;
    const bool dynamic_index = (plan_.linked && plan_.linked->locate(node.begin).module > 0) ||
        ((local_value_index || non_ident_index) && unproved_index);
    if (!dynamic_index && !(array && !(key_kinds & ~kKindNumber)) && !bytes && !object && !bad_boolean_key &&
        !bad_bytes_key && !null_call && !null_script_index && !unknown_defers(object_kinds | key_kinds))
        return fail(node, "type:index");
    const std::uint16_t mark = fb_->top();
    std::uint16_t object_reg = 0, key = 0;
    const Node& chain = chain_enter(node);
    const std::string_view saved_note = global_note_;
    const bool note_set = global_note_.empty() && ast_.at(node.a).kind != NodeKind::Array;
    const bool offset_set = global_offset_set_;
    if (ast_.at(node.a).kind == NodeKind::Array) { global_offset_set_ = true; global_offset_ = node.begin; }
    if (note_set) global_note_ = text(node);
    const bool receiver_ok = reg_any(node.a, object_reg);
    if (note_set) global_note_ = saved_note;
    global_offset_set_ = offset_set;
    chain_next_ = nullptr;
    if (!receiver_ok || !retain_receiver(object_reg, object_reg < mark, node.b)) return false;  // H-A: b[g()]
    ++index_depth_;
    const std::uint8_t saved_flags = fb_->flags();
    fb_->set_flags(static_cast<std::uint8_t>(saved_flags | kInstrIndexKey));
    const bool key_ok = expr_any(node.b, key);
    fb_->set_flags(saved_flags);
    --index_depth_;
    if (!key_ok) return false;
    if (null_call || null_script_index) {
        const bool ok = legacy_fatal(null_call ? *null_call : node, "invalid index", dst);
        fb_->release_to(mark);
        return ok;
    }
    if (bad_boolean_key || bad_bytes_key) {
        const std::string kind = bad_bytes_key ? "bytes" : "JSON array";
        const bool ok = legacy_fatal(node, kind + " indices must be non-negative integers in '" +
                                     std::string(location.empty() ? text(chain) : location) + "'", dst);
        fb_->release_to(mark);
        return ok;
    }
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetIndex, dst, object_reg, key);
    const std::size_t note_index = fb_->note(at, location.empty() ? std::string(text(chain)) : std::move(location));
    if (print_index && loop_region() && ast_.at(node.a).kind == NodeKind::Ident)
        fb_->mark_textual_index(note_index);
    fb_->release_to(mark);
    return true;
}
} // namespace nift::rt::detail
