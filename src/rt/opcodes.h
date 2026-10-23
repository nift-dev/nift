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
    X(GetGlobal)  /* R[a] = G[b]: main's register b (a global, CP12b)           */                    \
    X(GetGlobalChecked) /* R[a] = G[b], but only after its declaration ran    */                    \
    X(SetGlobal)  /* G[a] = R[b]                                                */                    \
    X(MarkGlobal) /* G[a] is now initialised                                    */                    \
    X(CheckGlobal) /* fail unless G[a] is initialised                            */                    \
    X(LoadFunction) /* R[a] = the named function of protos[b] (identity)          */                    \
    X(NewCell)    /* R[a] = cell(R[b]); binding type R[c]                       */                    \
    X(CellLoad)   /* R[a] = cell(R[b]).value                                   */                    \
    X(CellStore)  /* check/cell-store R[b] into cell R[a]; text type RK[c]      */                    \
    X(Closure)    /* R[a] = closure(protos[b], cells R[a+1 .. a+c])             */                    \
    X(CallValue)  /* R[a] = R[b](R[a] .. R[a+c-1]); callee frame base = a       */                    \
    X(GetUpvalue) /* R[a] = value of closure cell b                            */                    \
    X(GetUpvalueCell) /* R[a] = the cell Value of closure upvalue b            */                    \
    X(SetUpvalue) /* check/store R[b] into closure cell a; text type RK[c]      */                    \
    X(Undefined)  /* raise "unknown value or malformed expression" (lambda free name) */                \
    X(SlotOf)     /* R[a] = the absolute stack slot of register b (CP12b structs) */                  \
    X(NewStruct)  /* R[a] = a new instance of struct (atom b), no fields yet      */                   \
    X(StructAdd)  /* R[a].(atom b) = RK[c] while constructing                    */                   \
    X(FieldGet)   /* R[a] = S[R[b]].(atom c): a field of the instance at a slot  */                   \
    X(FieldSet)   /* S[R[a]].(atom b) = RK[c]                                    */                   \
    X(CheckField) /* RK[a] assignable to field type RK[b]; c: legacy's wording    */                  \
    X(IterCheck)  /* R[a] is the iterable; b: 0 single-array, 1 pair-object,       */                    \
                  /* 2 force non-iterable, 3 force array-pair, 4 force object-single*/                 \
    X(ArrayElem)  /* R[a] = R[b][R[c]] (array); R[a+1] = found (CP12C slice 2)   */                    \
    X(ArrayElemValue) /* R[a] = R[b][R[c]] (array), no found flag (safe lengths) */                    \
    X(ObjectKeys) /* R[a] = insertion-ordered key array of object R[b]          */                     \
    X(ObjectLookup) /* R[a] = R[b][R[c]] (object); R[a+1] = found               */                     \
    X(ObjectLookupValue) /* R[a] = R[b][R[c]] (object), no found flag           */                     \
    X(IterMissing) /* raise "reference target no longer exists: <note>"         */                     \
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
