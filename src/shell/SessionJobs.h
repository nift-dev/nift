#pragma once
// Cold CP19 attachment: JobTable remains the only owner of shell groups.
#include "shell/Spawn.h"
#include "rt/session.h"
namespace nift::shell {
class SessionJobs final : public rt::SessionJobOwner {
public:
    JobTable jobs;
    rt::VmResult finish() override {
        const auto result = jobs.finish();
        rt::VmResult out; out.host_fault = result.host_fault;
        if (result.host_fault || !result.error.empty()) {
            out.reason = rt::StopReason::Error;
            out.code = "process.job_cleanup";
            out.message = result.error;
            out.host_fault_message = result.error;
        }
        return out;
    }
};
} // namespace nift::shell
