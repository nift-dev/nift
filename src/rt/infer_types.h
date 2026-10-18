#pragma once
// Structural types of the unit kind inference (rt/infer.h), internal (CP12).
#include "rt/ast.h"
#include "rt/infer.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt::infer_detail {

struct Ty;
using TyRef = std::shared_ptr<const Ty>;

struct Ty {
    std::uint8_t kinds = 0;
    bool any = false;  // every kind at every depth
    TyRef elem;        // arrays: the elements
    // Arrays that are all literals of one length never changed in place:
    // the type of each position (an index written as an integer literal).
    bool tuple = false;
    std::vector<TyRef> items;
    std::map<std::string, TyRef, std::less<>> fields;  // objects: members by key
};

const TyRef& empty_ty();
// Every kind at every depth ("top"), e.g. beyond the depth cap.
const TyRef& top_ty();
// The value of something the compiler refuses anyway (kKindUnknown).
const TyRef& any_ty();
TyRef kinds_ty(std::uint8_t kinds);
TyRef unite(const TyRef& a, const TyRef& b, int depth = 0);
bool same(const TyRef& a, const TyRef& b);
// The type of `value[key]` (key: a literal string, or null when unknown;
// position: an integer literal index, or -1).
TyRef element_of(const TyRef& t, const std::string* key, bool by_member, long position = -1);
// Result kinds of `l op r` for an arithmetic operator.
std::uint8_t arithmetic_kinds(Op op, std::uint8_t l, std::uint8_t r);

} // namespace nift::rt::infer_detail
