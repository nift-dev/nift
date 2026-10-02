#pragma once

#include "Diagnostic.h"

#include <optional>
#include <utility>

namespace nift::detail {

template <typename T>
class EvalOutcome {
public:
    enum class Kind { Value, Unsupported, Fatal };

    static EvalOutcome value(T value) {
        EvalOutcome outcome;
        outcome.kind_ = Kind::Value;
        outcome.value_ = std::move(value);
        return outcome;
    }

    static EvalOutcome unsupported() { return EvalOutcome{}; }

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

private:
    Kind kind_ = Kind::Unsupported;
    std::optional<T> value_;
    std::optional<Diagnostic> diagnostic_;
};

class ExecOutcome {
public:
    enum class Kind { Normal, Return, Break, Continue, Fatal };

    static ExecOutcome normal() { return ExecOutcome{}; }
    static ExecOutcome returned() { return ExecOutcome{Kind::Return, std::nullopt}; }
    static ExecOutcome broken() { return ExecOutcome{Kind::Break, std::nullopt}; }
    static ExecOutcome continued() { return ExecOutcome{Kind::Continue, std::nullopt}; }
    static ExecOutcome fatal(Diagnostic diagnostic) {
        return ExecOutcome{Kind::Fatal, std::move(diagnostic)};
    }

    Kind kind() const { return kind_; }
    Diagnostic& diagnostic() { return *diagnostic_; }
    const Diagnostic& diagnostic() const { return *diagnostic_; }

private:
    ExecOutcome() = default;
    ExecOutcome(Kind kind, std::optional<Diagnostic> diagnostic)
        : kind_(kind), diagnostic_(std::move(diagnostic)) {}

    Kind kind_ = Kind::Normal;
    std::optional<Diagnostic> diagnostic_;
};

template <typename T>
class WorkerCompletion {
public:
    enum class Kind { Pending, Value, Fatal };

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

    Kind kind() const { return kind_; }
    bool pending() const { return kind_ == Kind::Pending; }
    T& value() { return *value_; }
    const T& value() const { return *value_; }
    Diagnostic& diagnostic() { return *diagnostic_; }
    const Diagnostic& diagnostic() const { return *diagnostic_; }

private:
    Kind kind_ = Kind::Pending;
    std::optional<T> value_;
    std::optional<Diagnostic> diagnostic_;
};

}  // namespace nift::detail
