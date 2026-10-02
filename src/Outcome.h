#pragma once

#include "Diagnostic.h"
#include "RuntimeValue.h"

#include <optional>
#include <utility>

namespace nift::detail {

template <typename T>
class EvalOutcome {
public:
    enum class Kind { Value, Unsupported, Recoverable, Fatal };

    static EvalOutcome value(T value) {
        EvalOutcome outcome;
        outcome.kind_ = Kind::Value;
        outcome.value_ = std::move(value);
        return outcome;
    }

    static EvalOutcome unsupported() { return EvalOutcome{}; }

    static EvalOutcome recoverable(RuntimeValue error_value) {
        if (!error_value.is_error()) throw std::logic_error("recoverable outcome requires Error");
        EvalOutcome outcome;
        outcome.kind_ = Kind::Recoverable;
        outcome.error_ = std::move(error_value);
        return outcome;
    }

    static EvalOutcome fatal(Diagnostic diagnostic) {
        EvalOutcome outcome;
        outcome.kind_ = Kind::Fatal;
        outcome.diagnostic_ = std::move(diagnostic);
        return outcome;
    }

    Kind kind() const { return kind_; }
    T& value() { return *value_; }
    const T& value() const { return *value_; }
    Diagnostic& diagnostic() { return *diagnostic_; }
    const Diagnostic& diagnostic() const { return *diagnostic_; }
    RuntimeValue& error() { return *error_; }
    const RuntimeValue& error() const { return *error_; }

private:
    Kind kind_ = Kind::Unsupported;
    std::optional<T> value_;
    std::optional<Diagnostic> diagnostic_;
    std::optional<RuntimeValue> error_;
};

class ExecOutcome {
public:
    enum class Kind { Normal, Return, Break, Continue, Recoverable, Fatal };

    static ExecOutcome normal() { return ExecOutcome{}; }
    static ExecOutcome returned() { return ExecOutcome{Kind::Return, std::nullopt}; }
    static ExecOutcome broken() { return ExecOutcome{Kind::Break, std::nullopt}; }
    static ExecOutcome continued() { return ExecOutcome{Kind::Continue, std::nullopt}; }
    static ExecOutcome recoverable(RuntimeValue error_value) {
        if (!error_value.is_error()) throw std::logic_error("recoverable outcome requires Error");
        ExecOutcome outcome;
        outcome.kind_ = Kind::Recoverable;
        outcome.error_ = std::move(error_value);
        return outcome;
    }
    static ExecOutcome fatal(Diagnostic diagnostic) {
        return ExecOutcome{Kind::Fatal, std::move(diagnostic)};
    }

    Kind kind() const { return kind_; }
    Diagnostic& diagnostic() { return *diagnostic_; }
    const Diagnostic& diagnostic() const { return *diagnostic_; }
    RuntimeValue& error() { return *error_; }
    const RuntimeValue& error() const { return *error_; }

private:
    ExecOutcome() = default;
    ExecOutcome(Kind kind, std::optional<Diagnostic> diagnostic)
        : kind_(kind), diagnostic_(std::move(diagnostic)) {}

    Kind kind_ = Kind::Normal;
    std::optional<Diagnostic> diagnostic_;
    std::optional<RuntimeValue> error_;
};

template <typename T>
class WorkerCompletion {
public:
    enum class Kind { Pending, Value, Recoverable, Fatal };

    static WorkerCompletion value(T value) {
        WorkerCompletion completion;
        completion.kind_ = Kind::Value;
        completion.value_ = std::move(value);
        return completion;
    }

    static WorkerCompletion fatal(Diagnostic diagnostic) {
        WorkerCompletion completion;
        completion.kind_ = Kind::Fatal;
        completion.diagnostic_ = std::move(diagnostic);
        return completion;
    }

    static WorkerCompletion recoverable(RuntimeValue error_value, Diagnostic diagnostic = {}) {
        if (!error_value.is_error()) throw std::logic_error("recoverable worker completion requires Error");
        WorkerCompletion completion;
        completion.kind_ = Kind::Recoverable;
        completion.error_ = std::move(error_value);
        completion.recoverable_diagnostic_ = std::move(diagnostic);
        return completion;
    }

    Kind kind() const { return kind_; }
    bool pending() const { return kind_ == Kind::Pending; }
    T& value() { return *value_; }
    const T& value() const { return *value_; }
    Diagnostic& diagnostic() { return *diagnostic_; }
    const Diagnostic& diagnostic() const { return *diagnostic_; }
    RuntimeValue& error() { return *error_; }
    const RuntimeValue& error() const { return *error_; }
    bool has_recoverable_diagnostic() const { return recoverable_diagnostic_.has_value(); }
    Diagnostic& recoverable_diagnostic() { return *recoverable_diagnostic_; }
    const Diagnostic& recoverable_diagnostic() const { return *recoverable_diagnostic_; }

private:
    Kind kind_ = Kind::Pending;
    std::optional<T> value_;
    std::optional<Diagnostic> diagnostic_;
    std::optional<Diagnostic> recoverable_diagnostic_;
    std::optional<RuntimeValue> error_;
};

}  // namespace nift::detail
