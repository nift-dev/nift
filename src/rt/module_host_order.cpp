#include "rt/module_host_order.h"

#include "rt/module_host_op.h"
#include "rt/native.h"
#include "rt/opcodes.h"

#include <string_view>

// rt-lint: cold-file admission check, run once per linked unit before execution

namespace nift::rt {

BuiltinEffect builtin_effect(natives::BuiltinId id) {
    using I = natives::BuiltinId;
    switch (id) {
    // Pure: no host, file system, environment, clock or process effect; runs no user code.
    case I::StrLength: case I::StrEmpty: case I::StrSplit: case I::StrTrim: case I::StrTrimStart:
    case I::StrTrimEnd: case I::StrToUpper: case I::StrToLower: case I::StrIndexOf: case I::StrLastIndexOf:
    case I::StrContains: case I::StrStartsWith: case I::StrEndsWith: case I::StrReplace: case I::StrSubstr:
    case I::StrToString: case I::StrToInt: case I::StrToDouble: case I::ArrSize: case I::ArrLength: case I::ArrEmpty:
    case I::ArrFirst: case I::ArrLast: case I::ArrIndexOf: case I::ArrContains: case I::ArrJoin: case I::ArrSlice:
    case I::ArrUnique: case I::ArrFlatten: case I::ArrSum: case I::ArrMin: case I::ArrMax: case I::ArrTake:
    case I::ArrDrop: case I::ArrChunk: case I::ArrFromEntries: case I::FnMin: case I::FnMax: case I::FnRange:
    case I::ObjSize: case I::ObjEmpty: case I::ObjKeys: case I::ObjValues: case I::ObjEntries: case I::ObjHas:
    case I::ObjGet: case I::ObjMerge: case I::ObjMergeDeep: case I::ObjPick: case I::ObjOmit: case I::NumToString:
    case I::BoolToString: case I::NullToString: case I::Stringify: case I::Prettify: case I::Highlight:
    case I::FnHtmlEscape: case I::FnAttrEscape: case I::FnUrlEncode: case I::FnType: case I::FnIsNull:
    case I::FnIsBool: case I::FnIsNumber: case I::FnIsInt: case I::FnIsFloat: case I::FnIsString: case I::FnIsBytes:
    case I::FnIsArray: case I::FnIsObject: case I::FnIsStruct: case I::FnIsFunction: case I::FnIsCollection:
    case I::FnIsEnum: case I::NumToInt: case I::NumAbs: case I::NumFloor: case I::NumCeil: case I::NumRound:
    case I::FnBytes: case I::StrEncode: case I::BytesLength: case I::BytesSize: case I::BytesEmpty:
    case I::BytesSlice: case I::BytesDecode: case I::FnSha256: case I::FnValidate: case I::FnCopy:
    case I::FnDeepcopy: case I::FnMap: case I::FnSet: case I::FnStack: case I::MapGet: case I::MapContains:
    case I::MapSize: case I::MapEmpty: case I::SetContains: case I::SetSize: case I::SetEmpty: case I::StackTop:
    case I::StackFront: case I::StackContains: case I::StackSize: case I::StackEmpty: case I::FnSame:
        return BuiltinEffect::Pure;
    // Host: the CP16 block (files, directories, environment, clock, handles, streams).
    case I::HostOpen: case I::HostOpenBytes: case I::HostCat: case I::HostSha256File: case I::HostExists:
    case I::HostIsFile: case I::HostIsDir: case I::HostStat: case I::HostLs: case I::HostPwd: case I::HostCd:
    case I::HostTouch: case I::HostMkdir: case I::HostMakeDir: case I::HostRemove: case I::HostRm: case I::HostCp:
    case I::HostMv: case I::HostMove: case I::HostWhich: case I::HostGetenv: case I::HostEnv: case I::HostSetenv:
    case I::HostUnsetenv: case I::HostOs: case I::HostArch: case I::HostPlatform: case I::HostHardwareConcurrency:
    case I::HostEpoch: case I::HostSleep: case I::HostSecureRandomBytes: case I::HandleFile: case I::HandleIfstream:
    case I::HandleOfstream: case I::HandleClose: case I::FileOpen: case I::FileClose: case I::FileRead:
    case I::FileReadAll: case I::FileReadLine: case I::FileWrite: case I::FileWriteLine: case I::FileFlush:
    case I::FileSave: case I::FileRevert: case I::FilePath: case I::FileExists: case I::FileModified:
    case I::FileTell: case I::FileSeek: case I::FileEof: case I::FileCopy: case I::FileMove: case I::FileRemove:
    case I::StreamClose: case I::StreamRead: case I::StreamReadAll: case I::StreamReadLine: case I::StreamWrite:
    case I::StreamReadBytes: case I::StreamReadAllBytes: case I::StreamExtractString: case I::FileWriteVal:
    case I::FileAppend: case I::FilePrepend: case I::FileInsert: case I::FileInsertBefore: case I::FileInsertAfter:
    case I::FileReplace: case I::FileReplaceOnce:
    case I::StreamWriteLine: case I::StreamFlush: case I::StreamEof:
        return BuiltinEffect::Host;
    // Shared: synchronisation and channels (may block or observe other tasks).
    case I::SharedMutex: case I::SharedAtomicInt: case I::SharedAtomicBool: case I::SharedChannel:
    case I::SharedSend: case I::SharedRecv: case I::SharedCancel: case I::MutexLock: case I::MutexTryLock:
    case I::MutexUnlock: case I::MutexLocked: case I::MutexGet: case I::MutexSet: case I::AtomicLoad:
    case I::AtomicStore: case I::AtomicExchange: case I::AtomicFetchAdd: case I::AtomicFetchSub:
    case I::AtomicCompareExchange:
        return BuiltinEffect::Shared;
    case I::FnMinify: case I::Count: break;
    }
    return BuiltinEffect::Unclassified;
}

namespace {

// Operations that may run while modules initialise. Every other operation
// (calls, callbacks, tasks, struct construction, atomics, session ops, and
// any future opcode) is refused there.
bool init_safe(OpCode op) {
    switch (op) {
    case OpCode::Nop: case OpCode::LoadK: case OpCode::LoadI: case OpCode::LoadNull: case OpCode::LoadBool:
    case OpCode::Move: case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
    case OpCode::AddI: case OpCode::AddAssign: case OpCode::Eq: case OpCode::Ne: case OpCode::Lt: case OpCode::Le:
    case OpCode::Gt: case OpCode::Ge: case OpCode::Not: case OpCode::Truth: case OpCode::Neg: case OpCode::Pos:
    case OpCode::BindType: case OpCode::CheckType: case OpCode::CheckAssign: case OpCode::TypeMeet: case OpCode::Jmp:
    case OpCode::JmpFalse: case OpCode::JmpTrue: case OpCode::JmpNotNull: case OpCode::JmpNotLt:
    case OpCode::JmpNotLe: case OpCode::JmpNotGt: case OpCode::JmpNotGe: case OpCode::JmpNotEq:
    case OpCode::JmpNotNe: case OpCode::NewArray: case OpCode::NewObject: case OpCode::MakeImmutable:
    case OpCode::LoadKFresh: case OpCode::ObjectSet: case OpCode::GetIndex: case OpCode::SetIndex:
    case OpCode::GetMember: case OpCode::Push: case OpCode::Length: case OpCode::GetGlobal:
    case OpCode::GetGlobalChecked: case OpCode::SetGlobal: case OpCode::MarkGlobal: case OpCode::CheckGlobal:
    case OpCode::LoadFunction: case OpCode::NewCell: case OpCode::CellLoad: case OpCode::CellStore:
    case OpCode::Closure: case OpCode::GetUpvalue: case OpCode::GetUpvalueCell: case OpCode::SetUpvalue:
    case OpCode::Undefined: case OpCode::FieldGet: case OpCode::FieldSet: case OpCode::CheckField:
    case OpCode::StructIs: case OpCode::IterCheck: case OpCode::ArrayElem: case OpCode::ArrayElemValue:
    case OpCode::ObjectKeys: case OpCode::ObjectLookup: case OpCode::ObjectLookupValue: case OpCode::IterMissing:
    case OpCode::TopReturn: case OpCode::Halt: case OpCode::JmpIncLt: case OpCode::CopyCellLocation:
    case OpCode::CellLoadCached: case OpCode::CellStoreCached: case OpCode::NewSourceSlot: case OpCode::CellAnchor:
    case OpCode::CellSlot: case OpCode::TryBegin: case OpCode::TryEnd: case OpCode::Throw: case OpCode::ErrorNew:
    case OpCode::ErrorJson: case OpCode::ArrayMutate: case OpCode::KeyedMutate: case OpCode::Yield:
        return true;
#if defined(NIFT_HOST_ORDER_MUTANT) && NIFT_HOST_ORDER_MUTANT == 3  // test-only: calls and struct construction safe
    case OpCode::Call: case OpCode::CallValue: case OpCode::CallValueAny: case OpCode::NewStruct:
    case OpCode::StructAdd: return true;
#endif
#if defined(NIFT_HOST_ORDER_MUTANT) && NIFT_HOST_ORDER_MUTANT == 4  // test-only: callbacks count as safe
    case OpCode::CollectionSort: case OpCode::CollectionStep: case OpCode::CollectionCall: return true;
#endif
    default:
        return false;
    }
}

// E: only imports (and comments) before the entry's last import.
bool leading_imports(const Ast& entry, NodeId root) {
    const Node& program = entry.at(root);
    const NodeId* items = entry.list(program);
    std::uint32_t last = 0;
    for (std::uint32_t k = 0; k < program.count; ++k)
        if (entry.at(items[k]).kind == NodeKind::Import) last = k;
    for (std::uint32_t k = 0; k < last; ++k) {
        const NodeKind kind = entry.at(items[k]).kind;
        if (kind != NodeKind::Import && kind != NodeKind::Comment) return false;
    }
    return true;
}

// The 11 built-ins a binding of their name shadows in legacy (C120 precedence,
// dead-code-admission.md 5.2): a call of one is a user call unless the linked
// text has no use of the name other than a plain call `name(`.
bool binding_wins(natives::BuiltinId id) {
    using I = natives::BuiltinId;
    return id == I::FnMap || id == I::FnSet || id == I::FnStack || id == I::SharedSend || id == I::SharedRecv ||
           id == I::SharedCancel || id == I::SharedChannel || id == I::FnDeepcopy || id == I::FnValidate ||
           id == I::FnSha256 || id == I::HostSha256File;
}

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool maybe_bound(std::string_view text, std::string_view name) {
    for (std::size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + 1)) {
        const std::size_t end = at + name.size();
        if ((at > 0 && ident_char(text[at - 1])) || (end < text.size() && ident_char(text[end]))) continue;
        std::size_t next = end, prev = at;
        while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) ++next;
        while (prev > 0 && (text[prev - 1] == ' ' || text[prev - 1] == '\t')) --prev;
        if (prev > 0 && text[prev - 1] == '.') continue;  // a member or method name, not a free name
        // `fn(name(`, `fn (name(`, `fn[async](name(`: a declaration of the name.
        std::size_t q = prev > 0 && text[prev - 1] == '(' ? prev - 1 : 0;
        while (q > 0 && (text[q - 1] == ' ' || text[q - 1] == '\t')) --q;
        const bool fn_decl = q > 0 && (text[q - 1] == ']' || (q >= 2 && text[q - 1] == 'n' && text[q - 2] == 'f'));
        if (next >= text.size() || text[next] != '(' || fn_decl) return true;
    }
    return false;
}

// M: the main proto up to its last instruction located in a module.
bool init_safe_prefix(const Program& program, const LinkedUnit& link) {
    const Proto& main = program.main();
    std::size_t last = 0;
    for (std::size_t pc = 0; pc < main.code.size() && pc < main.offsets.size(); ++pc)
        if (link.locate(main.offsets[pc]).module != 0) last = pc + 1;
    for (std::size_t pc = 0; pc < last; ++pc) {
        const Instr& in = main.code[pc];
        if (in.op != OpCode::CallNative) {
            if (!init_safe(in.op)) return false;
            continue;
        }
        if (in.b < kFirstBuiltin) continue;  // print (NativeId::Print): presentation, no host effect
        const auto id = static_cast<natives::BuiltinId>(in.b - kFirstBuiltin);
        if (builtin_effect(id) != BuiltinEffect::Pure) return false;
#if !(defined(NIFT_HOST_ORDER_MUTANT) && NIFT_HOST_ORDER_MUTANT == 5)  // test-only: no shadowing check
        if (binding_wins(id) && maybe_bound(link.text, natives::builtin_spec(id).name)) return false;
#endif
    }
    return true;
}

} // namespace

bool host_order_admits(const Program& program, const LinkedUnit& link, const Ast& entry, NodeId root) {
#if !(defined(NIFT_HOST_ORDER_MUTANT) && NIFT_HOST_ORDER_MUTANT == 1)  // test-only: no leading-block check
    if (!leading_imports(entry, root)) return false;
#endif
#if defined(NIFT_HOST_ORDER_MUTANT) && NIFT_HOST_ORDER_MUTANT == 2  // test-only: no module-init check
    return true;
#else
    return init_safe_prefix(program, link);
#endif
}

bool host_order_refusal(const Program& program, const LinkedUnit& link, const Ast& entry, NodeId root,
                        std::string& reason, std::uint32_t& offset, bool* positioned) {
    return link.linked && first_module_host_op(program, reason, offset, positioned) &&
           !host_order_admits(program, link, entry, root);
}

} // namespace nift::rt
