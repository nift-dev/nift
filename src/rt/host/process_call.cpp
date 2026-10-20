#include "rt/host/process_call.h"

#include <filesystem>
#include <memory>

namespace nift::rt::host {

namespace {
std::string absolute_cwd(const std::string& spelled) {
    std::error_code ec;
    const auto base = std::filesystem::current_path(ec);
    if (spelled.empty()) return base.generic_string();
    return std::filesystem::absolute(std::filesystem::path(spelled), ec).lexically_normal().generic_string();
}
} // namespace

std::shared_ptr<ProcessGroup> start_traced(const HostCallEnv& env, SpawnRequest request, HostStatus& status,
                                           HostError& error, bool wait_at_once) {
    const std::string builtin = request.builtin;
    status = ProcessGroup::validate(request, error);
    if (status != HostStatus::Ok) {
        HostCall call(env, HostOp::Spawn, builtin);
        call.set_arg(request.stages.empty() ? std::string() : request.stages.front().program);
        // Refused before the attempt: no v1 line (no stage was about to launch).
        status = call.fail(status, error);
        return nullptr;
    }
    std::vector<std::unique_ptr<HostCall>> calls;
    for (const auto& stage : request.stages) {
        auto call = std::make_unique<HostCall>(env, HostOp::Spawn, builtin);
        call->set_spawn(stage.argv(), stage.cwd, absolute_cwd(stage.cwd));
        calls.push_back(std::move(call));
    }
    for (auto& call : calls) {  // all v1 lines before the first launch (legacy order)
        if (call->begin() != HostStatus::Ok) {
            status = call->status();
            error = call->error();
            for (auto& other : calls) if (!other->finished()) other->fail(status, error);
            return nullptr;
        }
    }
    auto group = wait_at_once ? ProcessGroup::start_for_wait(std::move(request), status, error)
                              : ProcessGroup::start(std::move(request), status, error);
    if (!group) {
        for (auto& call : calls) call->fail(status, error);
        return nullptr;
    }
    const auto stages = group->stages();
    for (std::size_t i = 0; i < calls.size(); ++i) {
        bool launched = true;
        if (i < stages.size()) launched = stages[i].launched;
        calls[i]->succeed(launched ? "launched" : "launch_failed");
    }
    return group;
}

} // namespace nift::rt::host
