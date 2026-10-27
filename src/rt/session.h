#pragma once
// Persistent VM namespace, immutable unit ownership and transactional admission.
#include "rt/compiler.h"
#include "rt/vm.h"
#include <memory>
#include <string>
#include <vector>
namespace nift::rt {
struct SessionResult {
    bool admitted = false;
    std::string refusal;
    std::uint32_t offset = 0;
    VmResult execution;
    Value value;
    bool presentation = false;
    std::uint64_t vm_executions = 0, legacy_executions = 0;
};
// VM-owned from construction, including when the first unit refuses (D-054).
// Owns one VM, stable cells, and every immutable code/source unit it publishes.
// Host handles remain unadmitted until CP17 session-lifetime reconciliation.
class VmSession {
public:
    explicit VmSession(VmOptions options = VmOptions());
    ~VmSession();
    void initialize_invocation(const std::string& command, const std::vector<std::string>& arguments);
    VmSession(const VmSession&) = delete;
    VmSession& operator=(const VmSession&) = delete;
    SessionResult execute(const std::string& source, std::string path, std::string& output,
                          bool inspect_tail = false);
    SessionResult evaluate(const std::string& source, std::string path, std::string& output);
    std::vector<std::string> callable_names() const;
    std::vector<std::string> namespace_names() const;
    bool contains(const std::string& name) const;
    Value value(const std::string& name) const;
    std::size_t binding_count() const;
    std::uint64_t executions() const { return executions_; }
    std::uint64_t refusals() const { return refusals_; }
    std::size_t retained_code_units() const;
    std::size_t code_slots() const { return linked_.size(); }
    void close();
private:
    std::vector<std::pair<std::uint64_t, const Proto*>> activation_targets(const Program&) const;
    void activate_code(const std::vector<std::pair<std::uint64_t, const Proto*>>&,
                       const std::shared_ptr<Program>&);
    SessionResult execute_safe(const std::string&, const std::string&, std::string&, bool, bool);
    SessionResult execute_unit(const std::string&, std::string, std::string&, bool expression,
                               bool inspect_tail = false, bool coherent = false,
                               double prior_parse_us = 0, double prior_compile_us = 0);
    void refresh(SessionCompileContext& pending);
    Value last_value_;
    void retire_code();
    struct CodeUnit { std::weak_ptr<Program> owner; std::vector<std::uint16_t> slots; };
    Program bootstrap_;
    std::vector<CodeUnit> units_;
    std::vector<std::uint16_t> vacant_;
    std::vector<const Proto*> linked_{nullptr};
    SessionCompileContext namespace_;
    std::vector<Value> globals_;
    std::unique_ptr<Vm> vm_;
    bool closed_ = false, active_ = false, close_pending_ = false;
    std::uint64_t executions_ = 0, refusals_ = 0;
};
} // namespace nift::rt
