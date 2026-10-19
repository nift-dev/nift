// X-macro list of the CP14 built-ins (an .inc file without include guard,
// included twice with NIFT_RT_BUILTIN defined, see builtin.h).
// rt-lint: cold-file table of names, read at compile time only
//
// NIFT_RT_BUILTIN(id, name, receiver, min args, max args, result kinds,
//                 result rule, may fail, instruction-guard target)
//
// The guard is the instruction count of one call's fixed part (kind checks,
// argument conversion, result construction on a minimal input), excluding
// per-element and per-byte work. Measured (callgrind, n = 1 workloads of
// tests/rt/builtins_bench.cpp, rounded up to 10) for the natives with a
// workload (24 in CP14, 8 more in CP15); locked with every size in tests/runtime2/builtins/builtin_guards.json
// (+10 %). The others are design targets until their workloads exist.

// ---- F1 string methods ----
NIFT_RT_BUILTIN(StrLength, "length", String, 0, 0, kInt, Fixed, true, 160)
NIFT_RT_BUILTIN(StrEmpty, "empty", String, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(StrSplit, "split", String, 1, 1, kArray, Fixed, true, 1190)
NIFT_RT_BUILTIN(StrTrim, "trim", String, 0, 0, kString, Fixed, false, 300)
NIFT_RT_BUILTIN(StrTrimStart, "trim_start", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrTrimEnd, "trim_end", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrToUpper, "to_upper", String, 0, 0, kString, Fixed, false, 270)
NIFT_RT_BUILTIN(StrToLower, "to_lower", String, 0, 0, kString, Fixed, false, 200)
NIFT_RT_BUILTIN(StrIndexOf, "index_of", String, 1, 1, kInt, Fixed, true, 250)
NIFT_RT_BUILTIN(StrLastIndexOf, "last_index_of", String, 1, 1, kInt, Fixed, true, 150)
NIFT_RT_BUILTIN(StrContains, "contains", String, 1, 1, kBool, Fixed, true, 150)
NIFT_RT_BUILTIN(StrStartsWith, "starts_with", String, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(StrEndsWith, "ends_with", String, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(StrReplace, "replace", String, 2, 2, kString, Fixed, true, 500)
NIFT_RT_BUILTIN(StrSubstr, "substr", String, 1, 2, kString, Fixed, true, 270)
NIFT_RT_BUILTIN(StrToString, "to_string", String, 0, 0, kString, Fixed, false, 60)
NIFT_RT_BUILTIN(StrToInt, "to_int", String, 0, 0, kInt, Fixed, true, 170)
NIFT_RT_BUILTIN(StrToDouble, "to_double", String, 0, 0, kNumber, Fixed, true, 990)

// ---- F2 array read-only methods ----
NIFT_RT_BUILTIN(ArrSize, "size", Array, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrLength, "length", Array, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrEmpty, "empty", Array, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(ArrFirst, "first", Array, 0, 0, kAny, ElementOf, true, 130)
NIFT_RT_BUILTIN(ArrLast, "last", Array, 0, 0, kAny, ElementOf, true, 80)
NIFT_RT_BUILTIN(ArrIndexOf, "indexOf", Array, 1, 1, kInt, Fixed, false, 150)
NIFT_RT_BUILTIN(ArrContains, "contains", Array, 1, 1, kBool, Fixed, false, 190)
NIFT_RT_BUILTIN(ArrJoin, "join", Array, 1, 1, kString, Fixed, true, 330)
NIFT_RT_BUILTIN(ArrSlice, "slice", Array, 1, 2, kArray, ElementsOf, true, 520)
NIFT_RT_BUILTIN(ArrUnique, "unique", Array, 0, 0, kArray, ElementsOf, false, 1390)
NIFT_RT_BUILTIN(ArrFlatten, "flatten", Array, 0, 0, kArray, Fixed, false, 250)
NIFT_RT_BUILTIN(ArrSum, "sum", Array, 0, 0, kNumber, Fixed, true, 160)
NIFT_RT_BUILTIN(ArrMin, "min", Array, 0, 0, kAny, ElementOf, true, 150)
NIFT_RT_BUILTIN(ArrMax, "max", Array, 0, 0, kAny, ElementOf, true, 190)
NIFT_RT_BUILTIN(ArrTake, "take", Array, 1, 1, kArray, ElementsOf, true, 630)
NIFT_RT_BUILTIN(ArrDrop, "drop", Array, 1, 1, kArray, ElementsOf, true, 250)
NIFT_RT_BUILTIN(ArrChunk, "chunk", Array, 1, 1, kArray, Fixed, true, 300)
NIFT_RT_BUILTIN(ArrFromEntries, "from_entries", Array, 0, 0, kObject, Fixed, true, 400)

// ---- F2 free collection functions ----
NIFT_RT_BUILTIN(FnMin, "min", None, 1, kVariadic, kAny, Selected, true, 200)
NIFT_RT_BUILTIN(FnMax, "max", None, 1, kVariadic, kAny, Selected, true, 200)
NIFT_RT_BUILTIN(FnRange, "range", None, 1, 3, kArray, Fixed, true, 620)

// ---- F3 object methods ----
NIFT_RT_BUILTIN(ObjSize, "size", Object, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(ObjEmpty, "empty", Object, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(ObjKeys, "keys", Object, 0, 0, kArray, Fixed, false, 2330)
NIFT_RT_BUILTIN(ObjValues, "values", Object, 0, 0, kArray, Fixed, false, 250)
NIFT_RT_BUILTIN(ObjEntries, "entries", Object, 0, 0, kArray, Fixed, false, 400)
NIFT_RT_BUILTIN(ObjHas, "has", Object, 1, 1, kBool, Fixed, true, 120)
NIFT_RT_BUILTIN(ObjGet, "get", Object, 1, 2, kAny, MemberOf, true, 240)
NIFT_RT_BUILTIN(ObjMerge, "merge", Object, 1, 1, kObject, Merged, true, 1920)
NIFT_RT_BUILTIN(ObjMergeDeep, "merge_deep", Object, 1, 1, kObject, Merged, true, 400)
NIFT_RT_BUILTIN(ObjPick, "pick", Object, 1, 1, kObject, Fixed, true, 400)
NIFT_RT_BUILTIN(ObjOmit, "omit", Object, 1, 1, kObject, Fixed, true, 400)

// ---- F6 conversion and formatting ----
NIFT_RT_BUILTIN(NumToString, "to_string", Number, 0, 0, kString, Fixed, false, 1010)
NIFT_RT_BUILTIN(BoolToString, "to_string", Bool, 0, 0, kString, Fixed, false, 60)
NIFT_RT_BUILTIN(NullToString, "to_string", Null, 0, 0, kString, Fixed, false, 60)
NIFT_RT_BUILTIN(Stringify, "stringify", Any, 0, 0, kString, Fixed, true, 4540)
NIFT_RT_BUILTIN(Prettify, "prettify", Any, 0, 0, kString, Fixed, true, 3790)
NIFT_RT_BUILTIN(Highlight, "highlight", Any, 0, 0, kString, Fixed, true, 300)
NIFT_RT_BUILTIN(FnHtmlEscape, "html_escape", None, 1, 1, kString, Fixed, true, 200)
NIFT_RT_BUILTIN(FnAttrEscape, "attr_escape", None, 1, 1, kString, Fixed, true, 200)
NIFT_RT_BUILTIN(FnUrlEncode, "url_encode", None, 1, 1, kString, Fixed, true, 200)
NIFT_RT_BUILTIN(FnType, "type", None, 1, 1, kString, Fixed, false, 80)
NIFT_RT_BUILTIN(FnIsNull, "is_null", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsBool, "is_bool", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsNumber, "is_number", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsInt, "is_int", None, 1, 1, kBool, Fixed, false, 80)
NIFT_RT_BUILTIN(FnIsFloat, "is_float", None, 1, 1, kBool, Fixed, false, 80)
NIFT_RT_BUILTIN(FnIsString, "is_string", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsBytes, "is_bytes", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsArray, "is_array", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsObject, "is_object", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsStruct, "is_struct", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsFunction, "is_function", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsCollection, "is_collection", None, 1, 1, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(FnIsEnum, "is_enum", None, 1, 1, kBool, Fixed, false, 60)

// ---- CP15 N1 number methods ----
NIFT_RT_BUILTIN(NumToInt, "to_int", Number, 0, 0, kInt, Fixed, true, 180)
NIFT_RT_BUILTIN(NumAbs, "abs", Number, 0, 0, kNumber, Fixed, false, 100)
NIFT_RT_BUILTIN(NumFloor, "floor", Number, 0, 0, kInt, Fixed, false, 100)
NIFT_RT_BUILTIN(NumCeil, "ceil", Number, 0, 0, kInt, Fixed, false, 100)
NIFT_RT_BUILTIN(NumRound, "round", Number, 0, 0, kInt, Fixed, false, 150)

// ---- CP15 B1 bytes ----
NIFT_RT_BUILTIN(FnBytes, "bytes", None, 0, 1, kBytes, Fixed, true, 360)
NIFT_RT_BUILTIN(StrEncode, "encode", String, 1, 1, kBytes, Fixed, true, 370)
NIFT_RT_BUILTIN(BytesLength, "length", Bytes, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(BytesSize, "size", Bytes, 0, 0, kInt, Fixed, false, 60)
NIFT_RT_BUILTIN(BytesEmpty, "empty", Bytes, 0, 0, kBool, Fixed, false, 60)
NIFT_RT_BUILTIN(BytesSlice, "slice", Bytes, 1, 2, kBytes, Fixed, true, 360)
NIFT_RT_BUILTIN(BytesDecode, "decode", Bytes, 1, 1, kString, Fixed, true, 220)
NIFT_RT_BUILTIN(FnSha256, "sha256", None, 1, 1, kString, Fixed, true, 4540)

// ---- CP15 J1 JSON values (json_parse is an API, not a script built-in) ----
NIFT_RT_BUILTIN(FnValidate, "validate", None, 2, 2, kAny, Selected, true, 103670)
NIFT_RT_BUILTIN(FnCopy, "copy", None, 1, 1, kAny, Selected, false, 60)
NIFT_RT_BUILTIN(FnDeepcopy, "deepcopy", None, 1, 1, kAny, Selected, false, 60)
