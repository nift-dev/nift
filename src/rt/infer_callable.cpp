#include "rt/infer_callable.h"
#include "rt/compiler.h"
#include "rt/compiler_process.h"
#include "rt/infer_builtins.h"
#include "rt/module_data.h"

#include <algorithm>
// rt-lint: cold-file callable inference worklist and lexical environments
namespace nift::rt::infer_detail {
TyRef Inference::process_call(const Node& call) {
    if (session_ || !imported_module_data(plan_, call)) return {};
    const auto& callee = ast_.at(call.a);
    if (current_ == static_cast<int>(plan_.functions.size()) && callee.kind == NodeKind::Member &&
        !find("bytes") && !plan_.index.count("bytes") && !plan_.struct_index.count("bytes") &&
        module_ascii_literal(ast_, call))
        return kinds_ty(kKindString);
    NodeId output = kNoNode;
    const Node* command = nullptr;
    if (callee.kind == NodeKind::Ident && ast_.text(callee) == "run" &&
        !plan_.index.count("run") && !find("run")) {
        command = &call;
    } else if (callee.kind == NodeKind::Member && !plan_.index.count("cmd")) {
        command = detail::process_pipeline_command(ast_, call, output);
        if (command) {
            if (const TyRef* binding = find_binding("cmd")) {
                const auto slot = static_cast<std::size_t>(binding - vars_.data());
                const auto installed = external_.find("cmd");
                const auto declaration = binding_decls_.find(slot);
                const bool invocation = installed != external_.end() && installed->second == slot;
                const bool shadow = invocation_ && declaration != binding_decls_.end() &&
                    std::any_of(invocation_->begin(), invocation_->end(), [&](const auto& entry) {
                        return entry.shadow == declaration->second;
                    });
                if (!invocation && !shadow) return {};
            }
        }
    }
    if (!command || !detail::process_program_literal(ast_, *command)) return {};
    for (std::uint32_t k = 0; k < command->count; ++k) {
        const auto id = ast_.list(*command)[k];
        const auto& arg = ast_.at(id);
        expr(arg.kind == NodeKind::Spread ? arg.a : id);
    }
    if (output != kNoNode) expr(output);
    return fresh(static_cast<NodeId>(&call - &ast_.at(0)), process_result());
}

std::size_t Inference::lambda_unit(NodeId node) {
    const auto found = lambda_unit_.find(node);
    if (found != lambda_unit_.end()) {
        auto& info = lambdas_[found->second - plan_.functions.size() - 1];
        if (!scopes_.empty() && info.scopes != scopes_) {
            info.scopes = scopes_;
            info.owner_function = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                ? current_ : owner_function_;
            enqueue(found->second);
        }
        return found->second;
    }
    const std::size_t id = lambdas_.size();
    const std::size_t unit = plan_.functions.size() + 1 + id;
    LambdaInfo info;
    info.node = node;
    info.owner_function = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                              ? current_
                              : owner_function_;
    info.params.assign(ast_.at(node).count, empty_ty());
    if (session_) for (std::uint32_t k = 0; k < ast_.at(node).count; ++k) {
        const auto seed = session_->parameter_seeds.find(ast_.list(ast_.at(node))[k]);
        if (seed != session_->parameter_seeds.end()) info.params[k] = seed->second;
    }
    if (!info.params.empty() && (ast_.at(ast_.list(ast_.at(node))[info.params.size()-1]).flags & kFlagVariadic)) {
        auto rest = std::make_shared<Ty>();
        rest->kinds = kKindArray;
        rest->elem = top_ty();
        info.params.back() = std::move(rest);
    }
    info.returns = empty_ty();
    info.collection_returns = empty_ty();
    info.scopes = scopes_;
    lambdas_.push_back(std::move(info));
    lambda_unit_.emplace(node, unit);
    enqueue(unit);
    return unit;
}

TyRef Inference::call_callables(const TyRef& callee, const std::vector<TyRef>& values, bool& grew) {
    // B4b H1: some targets are not in `callables`. B06 (owner 2026-11-09 16:46, item 4): a callee
    // with no kind and no target yet (a value not computed so far in the fixed-point walk: a
    // factory's result before its body is walked) has no target to miss; the walk returns here when
    // it grows. The direct-call path treats it the same way (Inference::call: no untracked_call for
    // a callee of no kind). Without this, a collection method's callback from `mk()` widened every
    // function value to parameters of anything on the first walk (`a.sort(mk())`: type:<).
#if !(defined(NIFT_B06_MUTANT) && NIFT_B06_MUTANT == 2)  // B06-2: a pending callee is inexact (B4b 8)
    const bool pending = callee && !callee->any && callee->kinds == 0 && callee->callables.empty() &&
                         !callee->untracked_callable;
#else
    const bool pending = false;
#endif
    if (!pending && !exact_callee(callee)) untracked_call();
    TyRef out = empty_ty();
    for (std::uint32_t unit : callee->callables) {
        if (static_cast<std::size_t>(unit) < plan_.functions.size()) {
            auto& params = params_[unit];
            if (session_) session_call_edge(unit, params.size(), values.size());
            for (std::size_t k = 0; k < values.size() && k < params.size(); ++k)
                grew = merge(params[k], values[k]) || grew;
            callers_[unit].insert(current_unit());
            out = unite(out, async_result(ast_.at(plan_.functions[unit]).flags, returns_[unit]));
        } else if (static_cast<std::size_t>(unit) > plan_.functions.size()) {
            const std::size_t id = static_cast<std::size_t>(unit) - plan_.functions.size() - 1;
            LambdaInfo& info = lambdas_[id];
            if (session_) session_call_edge(unit, info.params.size(), values.size());
            const Node& lambda = ast_.at(info.node);
            const bool variadic = lambda.count && (ast_.at(ast_.list(lambda)[lambda.count-1]).flags & kFlagVariadic);
            const std::size_t fixed = info.params.size() - (variadic ? 1 : 0);
            for (std::size_t k = 0; k < values.size() && k < fixed; ++k)
                grew = merge(info.params[k], values[k]) || grew;
            info.callers.insert(current_unit());
            out = unite(out, async_result(ast_.at(info.node).flags, info.returns));
        }
        if (grew) enqueue(unit);
    }
    return out;
}

int Inference::function_index(const Node& callee) const {
    if (callee.kind != NodeKind::Ident) return -1;
    const auto fn = plan_.index.find(ast_.text(callee));
    return fn == plan_.index.end() ? -1 : static_cast<int>(fn->second) - 1;
}

} // namespace nift::rt::infer_detail
