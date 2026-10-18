#pragma once
// Register bytecode instruction set of the Runtime 2.0 VM (CP11).
//
// An instruction is 8 bytes: an opcode and three 16-bit operands a, b, c.
// Registers are frame-relative. An "RK" operand with bit 15 set names a
// constant of the current Proto (index = operand & 0x7fff) instead of a
// register. Jump offsets are signed 16-bit values relative to the next
// instruction. The X-macro keeps the opcode enum, the names used by the
// disassembler and the VM dispatch table in one order.
#include <cstdint>

namespace nift::rt {

#define NIFT_RT_OPCODES(X)                                                                            \
    X(Nop)        /* -                                                          */                    \
    X(LoadK)      /* R[a] = K[b]                                                */                    \
    X(LoadI)      /* R[a] = Int(int16 b)                                        */                    \
    X(LoadNull)   /* R[a] = null                                                */                    \
    X(LoadBool)   /* R[a] = Bool(b != 0)                                        */                    \
    X(Move)       /* R[a] = R[b] (copy)                                         */                    \
    X(Add)        /* R[a] = RK[b] + RK[c]  (numbers, or string concatenation)   */                    \
    X(Sub)        /* R[a] = RK[b] - RK[c]                                       */                    \
    X(Mul)        /* R[a] = RK[b] * RK[c]                                       */                    \
    X(Div)        /* R[a] = RK[b] / RK[c]                                       */                    \
    X(Mod)        /* R[a] = RK[b] % RK[c]                                       */                    \
    X(AddI)       /* R[a] = R[b] + int16 c                                      */                    \
    X(AddAssign)  /* R[a] = R[a] += RK[b]  (numbers or two strings only)        */                    \
    X(Eq)         /* R[a] = RK[b] == RK[c]                                      */                    \
    X(Ne)         /* R[a] = RK[b] != RK[c]                                      */                    \
    X(Lt)         /* R[a] = RK[b] <  RK[c]                                      */                    \
    X(Le)         /* R[a] = RK[b] <= RK[c]                                      */                    \
    X(Gt)         /* R[a] = RK[b] >  RK[c]                                      */                    \
    X(Ge)         /* R[a] = RK[b] >= RK[c]                                      */                    \
    X(Not)        /* R[a] = !truthy(R[b])                                       */                    \
    X(Truth)      /* R[a] = Bool(truthy(R[b]))                                  */                    \
    X(Neg)        /* R[a] = -R[b]                                               */                    \
    X(Pos)        /* R[a] = +R[b] (numeric check)                               */                    \
    X(BindType)   /* R[a] = legacy binding type of value R[b], text type RK[c]  */                    \
    X(CheckType)  /* error unless R[a]'s value type may be assigned to tag R[b] */                    \
    X(CheckAssign)/* as CheckType for a textual `x = e` (text type RK[c])       */                    \
        X(TypeMeet)   /* R[a] = legacy meet of text types RK[b], RK[c]              */                    \
    X(Jmp)        /* pc += int16 c                                              */                    \
    X(JmpFalse)   /* if !truthy(R[a]) pc += int16 c                             */                    \
    X(JmpTrue)    /* if truthy(R[a]) pc += int16 c                              */                    \
    X(JmpNotNull) /* if R[a] is not null pc += int16 c                          */                    \
    X(JmpNotLt)   /* if !(RK[a] <  RK[b]) pc += int16 c                         */                    \
    X(JmpNotLe)   /* if !(RK[a] <= RK[b]) pc += int16 c                         */                    \
    X(JmpNotGt)   /* if !(RK[a] >  RK[b]) pc += int16 c                         */                    \
    X(JmpNotGe)   /* if !(RK[a] >= RK[b]) pc += int16 c                         */                    \
    X(JmpNotEq)   /* if !(RK[a] == RK[b]) pc += int16 c                         */                    \
    X(JmpNotNe)   /* if !(RK[a] != RK[b]) pc += int16 c                         */                    \
    X(Call)       /* R[a] = protos[b](R[a] .. R[a+c-1]); callee frame base = a  */                    \
    X(CallNative) /* R[a] = natives[b](R[a] .. R[a+c-1])                        */                    \
    X(Return)     /* return R[a] to the caller                                  */                    \
    X(ReturnNull) /* return null to the caller                                  */                    \
    X(NewArray)   /* R[a] = [R[b] .. R[b+c-1]]                                  */                    \
    X(NewObject)  /* R[a] = {}                                                  */                    \
    X(ObjectSet)  /* R[a][atom b] = R[c] (object literal construction)          */                    \
    X(GetIndex)   /* R[a] = R[b][RK[c]]                                         */                    \
    X(SetIndex)   /* R[a][RK[b]] = RK[c]                                        */                    \
    X(GetMember)  /* R[a] = R[b].(atom c)                                       */                    \
    X(Push)       /* R[a].push(RK[b])                                           */                    \
    X(Length)     /* R[a] = R[b].length() (c = 0) or R[b].size() (c = 1)        */                    \
    X(JmpLarge)   /* if R[a] is a number legacy calls large, pc += int16 c      */                    \
    X(TopReturn)  /* end the script; R[a] is rendered as the script result      */                    \
    X(Halt)       /* end the script                                             */

enum class OpCode : std::uint8_t {
#define NIFT_RT_OPCODE_ENUM(name) name,
    NIFT_RT_OPCODES(NIFT_RT_OPCODE_ENUM)
#undef NIFT_RT_OPCODE_ENUM
    Count
};

struct Instr {
    OpCode op = OpCode::Nop;
    std::uint8_t flags = 0;  // kInstr* (proto.h)
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
};
static_assert(sizeof(Instr) == 8, "rt::Instr must be 8 bytes");

inline constexpr std::uint16_t kConstantBit = 0x8000;
inline constexpr std::uint16_t kMaxConstants = 0x7fff;
inline constexpr std::uint16_t kMaxRegisters = 0x7fff;

inline std::uint16_t jump_operand(int offset) { return static_cast<std::uint16_t>(static_cast<std::int16_t>(offset)); }
inline int jump_offset(std::uint16_t operand) { return static_cast<std::int16_t>(operand); }

const char* opcode_name(OpCode op);

} // namespace nift::rt
