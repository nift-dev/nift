// X-macro list of the CP14 built-ins (an .inc file without include guard,
// included twice with NIFT_RT_BUILTIN defined, see builtin.h).
// rt-lint: cold-file table of names, read at compile time only
//
// NIFT_RT_BUILTIN(id, name, receiver, min args, max args, result kinds,
//                 result rule, may fail, instruction-guard target)
//
// The guard target is the instruction budget of one call's fixed part (kind
// checks, argument conversion, result construction for an empty or
// single-element input), excluding per-element and per-byte work, which is
// gated separately as instructions per element (design record, "Guards").

// ---- F1 string methods ----
NIFT_RT_BUILTIN(StrLength, "length", String, 0, 0, kInt, Fixed, true, 120)
NIFT_RT_BUILTIN(StrEmpty, "empty", String, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(StrSplit, "split", String, 1, 1, kArray, Fixed, true, 400)
NIFT_RT_BUILTIN(StrTrim, "trim", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrTrimStart, "trim_start", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrTrimEnd, "trim_end", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrToUpper, "to_upper", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrToLower, "to_lower", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrIndexOf, "index_of", String, 1, 1, kInt, Fixed, true, 150)
NIFT_RT_BUILTIN(StrLastIndexOf, "last_index_of", String, 1, 1, kInt, Fixed, true, 150)
NIFT_RT_BUILTIN(StrContains, "contains", String, 1, 1, kBool, Fixed, true, 150)
NIFT_RT_BUILTIN(StrStartsWith, "starts_with", String, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(StrEndsWith, "ends_with", String, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(StrReplace, "replace", String, 2, 2, kString, Fixed, true, 300)
NIFT_RT_BUILTIN(StrSubstr, "substr", String, 1, 2, kString, Fixed, true, 250)
NIFT_RT_BUILTIN(StrToString, "to_string", String, 0, 0, kString, Fixed, false, 60)
NIFT_RT_BUILTIN(StrToInt, "to_int", String, 0, 0, kInt, Fixed, true, 200)
NIFT_RT_BUILTIN(StrToDouble, "to_double", String, 0, 0, kNumber, Fixed, true, 400)

// ---- F2 array read-only methods ----
NIFT_RT_BUILTIN(ArrSize, "size", Array, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrLength, "length", Array, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrEmpty, "empty", Array, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrFirst, "first", Array, 0, 0, kAny, ElementOf, true, 80)
NIFT_RT_BUILTIN(ArrLast, "last", Array, 0, 0, kAny, ElementOf, true, 80)
NIFT_RT_BUILTIN(ArrIndexOf, "indexOf", Array, 1, 1, kInt, Fixed, false, 150)
NIFT_RT_BUILTIN(ArrContains, "contains", Array, 1, 1, kBool, Fixed, false, 150)
NIFT_RT_BUILTIN(ArrJoin, "join", Array, 1, 1, kString, Fixed, true, 300)
NIFT_RT_BUILTIN(ArrSlice, "slice", Array, 1, 2, kArray, ElementsOf, true, 250)
NIFT_RT_BUILTIN(ArrUnique, "unique", Array, 0, 0, kArray, ElementsOf, false, 400)
NIFT_RT_BUILTIN(ArrFlatten, "flatten", Array, 0, 0, kArray, Fixed, false, 250)
NIFT_RT_BUILTIN(ArrSum, "sum", Array, 0, 0, kNumber, Fixed, true, 150)
NIFT_RT_BUILTIN(ArrMin, "min", Array, 0, 0, kAny, ElementOf, true, 150)
NIFT_RT_BUILTIN(ArrMax, "max", Array, 0, 0, kAny, ElementOf, true, 150)
NIFT_RT_BUILTIN(ArrTake, "take", Array, 1, 1, kArray, ElementsOf, true, 250)
NIFT_RT_BUILTIN(ArrDrop, "drop", Array, 1, 1, kArray, ElementsOf, true, 250)
NIFT_RT_BUILTIN(ArrChunk, "chunk", Array, 1, 1, kArray, Fixed, true, 300)
NIFT_RT_BUILTIN(ArrFromEntries, "from_entries", Array, 0, 0, kObject, Fixed, true, 400)

// ---- F2 free collection functions ----
NIFT_RT_BUILTIN(FnMin, "min", None, 1, kVariadic, kAny, Selected, true, 200)
NIFT_RT_BUILTIN(FnMax, "max", None, 1, kVariadic, kAny, Selected, true, 200)
NIFT_RT_BUILTIN(FnRange, "range", None, 1, 3, kArray, Fixed, true, 300)

// ---- F3 object methods ----
NIFT_RT_BUILTIN(ObjSize, "size", Object, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ObjEmpty, "empty", Object, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(ObjKeys, "keys", Object, 0, 0, kArray, Fixed, false, 250)
NIFT_RT_BUILTIN(ObjValues, "values", Object, 0, 0, kArray, Fixed, false, 250)
NIFT_RT_BUILTIN(ObjEntries, "entries", Object, 0, 0, kArray, Fixed, false, 400)
NIFT_RT_BUILTIN(ObjHas, "has", Object, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(ObjGet, "get", Object, 1, 2, kAny, MemberOf, true, 120)
NIFT_RT_BUILTIN(ObjMerge, "merge", Object, 1, 1, kObject, Merged, true, 300)
NIFT_RT_BUILTIN(ObjMergeDeep, "merge_deep", Object, 1, 1, kObject, Merged, true, 400)
NIFT_RT_BUILTIN(ObjPick, "pick", Object, 1, 1, kObject, Fixed, true, 400)
NIFT_RT_BUILTIN(ObjOmit, "omit", Object, 1, 1, kObject, Fixed, true, 400)
