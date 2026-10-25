// CP18: owned compile-once Runtime 2.0 programs; no legacy engine re-entry.
#include "nift/c_abi.h"
#include "rt/compiler.h"
#include "rt/parser.h"
#include "rt/prevalidate.h"
#include "rt/source_map.h"
#include "rt/vm.h"

#include <limits>
#include <memory>
#include <string>
#include <thread>

struct nift_program {
    std::unique_ptr<nift::rt::Program> code;
    nift::rt::SourceMap sources;
    std::thread::id owner = std::this_thread::get_id();
};
struct nift_program_result {
    nift_program_outcome outcome = NIFT_PROGRAM_COMPLETE;
    std::string output, returned_text, message, code, source;
    unsigned long long line = 0, column = 0;
    bool returned = false, catchable = false;
};
namespace {
bool valid(const char* data, size_t size) { return data || size == 0; }
std::string copy(const char* data, size_t size) { return size ? std::string(data, size) : std::string(); }
void location(nift_program_result& result, const nift::rt::SourceMap& map, std::uint32_t offset) {
    const auto origin = map.at(offset);
    result.source = origin.path;
    result.line = origin.line;
    result.column = origin.column;
}
nift_status view(const nift_program_result* result, const std::string* text, nift_string* out) {
    if (out) *out = {nullptr, 0};
    if (!result || !out) return NIFT_ERROR_INVALID_ARGUMENT;
    *out = {text->data(), text->size()};
    return NIFT_OK;
}
} // namespace

extern "C" {
nift_status nift_program_compile(const nift_source* source, nift_program** out_program,
                                 nift_program_result** out_result) {
    if (out_program) *out_program = nullptr;
    if (out_result) *out_result = nullptr;
    if (!out_program || !out_result || !source || source->kind != NIFT_SOURCE_TEXT ||
        !valid(source->data, source->length) || !valid(source->logical_name, source->logical_name_length) ||
        source->length > std::numeric_limits<std::uint32_t>::max()) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        const std::string text = copy(source->data, source->length);
        auto result = std::make_unique<nift_program_result>();
        auto program = std::make_unique<nift_program>();
        program->sources = nift::rt::SourceMap::single(
            source->logical_name_length ? copy(source->logical_name, source->logical_name_length) : "<embed>", text);
        const auto validation = nift::rt::prevalidate(text);
        if (!validation.ok) {
            result->outcome = NIFT_PROGRAM_SYNTAX_ERROR;
            result->message = validation.message;
            result->code = validation.code;
            location(*result, program->sources, validation.offset);
            *out_result = result.release();
            return NIFT_OK;
        }
        nift::rt::Ast ast(text);
        const auto parsed = nift::rt::parse_script(ast);
        if (!parsed.ok) {
            result->outcome = NIFT_PROGRAM_SYNTAX_ERROR;
            result->message = parsed.error.message;
            result->code = nift::rt::ParseError::kCode;
            location(*result, program->sources, parsed.error.offset);
        } else {
            auto compiled = nift::rt::compile_unit(ast, parsed.root);
            if (!compiled.program) {
                result->outcome = NIFT_PROGRAM_REFUSED;
                result->message = compiled.reason;
                location(*result, program->sources, compiled.offset);
            } else {
                program->code = std::move(compiled.program);
                *out_program = program.release();
            }
        }
        *out_result = result.release();
        return NIFT_OK;
    } catch (...) { return NIFT_ERROR_INTERNAL; }
}
nift_status nift_program_free(nift_program* program) {
    if (!program) return NIFT_OK;
    if (program->owner != std::this_thread::get_id()) return NIFT_ERROR_INVALID_ARGUMENT;
    try { delete program; return NIFT_OK; }
    catch (...) { return NIFT_ERROR_INTERNAL; }
}
nift_status nift_program_run(const nift_program* program, size_t max_frames, size_t max_registers,
                             size_t heap_budget, nift_program_result** out_result) {
    if (out_result) *out_result = nullptr;
    if (!out_result || !program || (max_registers && max_registers < 256) ||
        program->owner != std::this_thread::get_id()) return NIFT_ERROR_INVALID_ARGUMENT;
    try {
        auto result = std::make_unique<nift_program_result>();
        nift::rt::VmOptions options;
        options.source_map = &program->sources;
        if (max_frames) options.max_frames = max_frames;
        if (max_registers) options.max_registers = max_registers;
        if (heap_budget) options.heap_budget = heap_budget;
        nift::rt::Vm vm(*program->code, options);
        const auto stop = vm.run(result->output);
        result->outcome = stop.reason == nift::rt::StopReason::None ? NIFT_PROGRAM_COMPLETE :
            stop.reason == nift::rt::StopReason::Error ? NIFT_PROGRAM_ERROR : NIFT_PROGRAM_INTERNAL;
        result->message = stop.message;
        result->code = stop.code;
        result->catchable = stop.catchable;
        result->returned = stop.returned;
        result->returned_text = stop.return_text;
        if (stop.reason != nift::rt::StopReason::None) {
            if (stop.error_origin) {
                result->source = stop.origin_source;
                result->line = stop.origin_line;
                result->column = stop.origin_column;
            } else location(*result, program->sources, stop.offset);
        }
        *out_result = result.release();
        return NIFT_OK;
    } catch (...) { return NIFT_ERROR_INTERNAL; }
}
void nift_program_result_free(nift_program_result* result) { delete result; }
nift_program_outcome nift_program_result_outcome(const nift_program_result* result) {
    return result ? result->outcome : NIFT_PROGRAM_INTERNAL;
}
int nift_program_result_returned(const nift_program_result* result) { return result && result->returned; }
int nift_program_result_catchable(const nift_program_result* result) { return result && result->catchable; }
#define PROGRAM_VIEW(name, field) \
    nift_status nift_program_result_##name(const nift_program_result* r, nift_string* out) { \
        return view(r, r ? &r->field : nullptr, out); \
    }
PROGRAM_VIEW(output, output)
PROGRAM_VIEW(return_text, returned_text)
PROGRAM_VIEW(error_message, message)
PROGRAM_VIEW(error_code, code)
PROGRAM_VIEW(error_source, source)
#undef PROGRAM_VIEW
unsigned long long nift_program_result_error_line(const nift_program_result* r) { return r ? r->line : 0; }
unsigned long long nift_program_result_error_column(const nift_program_result* r) { return r ? r->column : 0; }
} // extern "C"
