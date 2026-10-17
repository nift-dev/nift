#include "rt/proto.h"

#include "rt/string.h"

#include <sstream>

namespace nift::rt {

const char* opcode_name(OpCode op) {
    static const char* const names[] = {
#define NIFT_RT_OPCODE_NAME(name) #name,
        NIFT_RT_OPCODES(NIFT_RT_OPCODE_NAME)
#undef NIFT_RT_OPCODE_NAME
    };
    const auto index = static_cast<std::size_t>(op);
    return index < sizeof names / sizeof names[0] ? names[index] : "?";
}

const char* bail_reason_name(BailReason reason) {
    switch (reason) {
        case BailReason::None: return "none";
        case BailReason::Error: return "runtime-error";
        case BailReason::NumberRange: return "number-range";
        case BailReason::Aliasing: return "aliasing";
        case BailReason::RecursionBudget: return "recursion-budget";
        case BailReason::StaticError: return "static-error";
        case BailReason::Unverified: return "unverified";
        case BailReason::OutputLimit: return "output-limit";
        case BailReason::Internal: return "internal";
    }
    return "?";
}

namespace {

void constant_text(std::ostringstream& out, const Value& value) {
    switch (value.kind()) {
        case Kind::Int: out << value.as_int(); break;
        case Kind::Double: out << value.as_double(); break;
        case Kind::String: out << '"' << string_view_of(value) << '"'; break;
        default: out << kind_name(value.kind()); break;
    }
}

} // namespace

std::string disassemble(const Program& program) {
    std::ostringstream out;
    for (std::size_t p = 0; p < program.protos.size(); ++p) {
        const Proto& proto = *program.protos[p];
        out << "proto " << p << ' ' << (proto.name.empty() ? "<main>" : proto.name) << " params=" << proto.nparams
            << " regs=" << proto.nregs << '\n';
        for (std::size_t k = 0; k < proto.constants.size(); ++k) {
            out << "  K" << k << " = ";
            constant_text(out, proto.constants[k]);
            out << '\n';
        }
        for (std::size_t i = 0; i < proto.code.size(); ++i) {
            const Instr& in = proto.code[i];
            out << "  " << i << ": " << opcode_name(in.op) << ' ' << in.a << ' ' << in.b << ' ' << in.c << '\n';
        }
    }
    return out.str();
}

} // namespace nift::rt
