@import("cli")

assertions := 0
fn(expect(condition, label)) {
    if(!condition) { print("FAIL " + label); missing.value }
    assertions += 1
}

fn(expect_diagnostic(result, code, source, option, index, label)) {
    expect(!result.ok, label + " rejects")
    expect(result.diagnostics.size() > 0, label + " diagnostic exists")
    diagnostic := result.diagnostics[0]
    expect(diagnostic.keys().join(",") == "code,message,source,option,index", label + " diagnostic shape")
    expect(diagnostic.code == code, label + " code")
    expect(diagnostic.source == source, label + " source")
    expect(diagnostic.option == option, label + " option")
    expect(diagnostic.index == index, label + " index")
    expect(type(diagnostic.message) == "string" && diagnostic.message != "", label + " message")
}

fn(expect_schema(spec, code, option, label)) {
    validated := cli.validate_spec(spec)
    expect(validated.keys().join(",") == "ok,diagnostics", label + " validation shape")
    expect_diagnostic(validated, code, "schema", option, null, label)
    parsed := cli.parse([], spec)
    expect(parsed.keys().join(",") == "ok,options,present,positionals,diagnostics", label + " parse shape")
    expect_diagnostic(parsed, code, "schema", option, null, label + " parse")
    expect(parsed.options.size() == 0 && parsed.present.size() == 0 && parsed.positionals.size() == 0, label + " no partial data")
}

fn(expect_argv(argv, spec, code, option, index, label)) {
    parsed := cli.parse(argv, spec)
    expect_diagnostic(parsed, code, "argv", option, index, label)
    expect(parsed.options.size() == 0 && parsed.present.size() == 0 && parsed.positionals.size() == 0, label + " no partial data")
}

empty_spec := {"options":{}}
validated := cli.validate_spec(empty_spec)
expect(validated.ok && validated.diagnostics.size() == 0, "empty spec validates")
expect(validated.keys().join(",") == "ok,diagnostics", "validation exact shape")
empty := cli.parse([], empty_spec)
expect(empty.ok, "empty parse")
expect(empty.keys().join(",") == "ok,options,present,positionals,diagnostics", "parse exact shape")
expect(empty.options.size() == 0 && empty.present.size() == 0 && empty.positionals.size() == 0 && empty.diagnostics.size() == 0, "empty materialization")

expect_schema(null, "invalid_spec", null, "null spec")
expect_schema([], "invalid_spec", null, "array spec")
expect_schema({}, "missing_options", null, "missing options")
expect_schema({"extra":1,"options":{}}, "unknown_spec_field", null, "unknown top field")
expect_schema({"options":[]}, "invalid_options", null, "options array")
expect_schema({"options":{"bad_thing":{"type":"bool"}}}, "invalid_option_name", "bad_thing", "underscore name")
expect_schema({"options":{"a--b":{"type":"bool"}}}, "invalid_option_name", "a--b", "repeated hyphen")
expect_schema({"options":{"a-":{"type":"bool"}}}, "invalid_option_name", "a-", "trailing hyphen")
expect_schema({"options":{"1a":{"type":"bool"}}}, "invalid_option_name", "1a", "digit first")
expect_schema({"options":{"Alpha9-z":1}}, "invalid_descriptor", "Alpha9-z", "descriptor type")
expect_schema({"options":{"alpha":{}}}, "missing_type", "alpha", "missing type")
expect_schema({"options":{"alpha":{"type":"float"}}}, "invalid_type", "alpha", "unknown type")
expect_schema({"options":{"alpha":{"type":"bool","extra":true}}}, "unknown_descriptor_field", "alpha", "unknown descriptor field")
expect_schema({"options":{"alpha":{"type":"bool","short":null}}}, "invalid_short", "alpha", "null short")
expect_schema({"options":{"alpha":{"type":"bool","short":"1"}}}, "invalid_short", "alpha", "digit short")
expect_schema({"options":{"alpha":{"type":"bool","short":"ab"}}}, "invalid_short", "alpha", "long short")
expect_schema({"options":{"alpha":{"type":"bool","short":"a"},"beta":{"type":"string","short":"a"}}}, "duplicate_short", "beta", "duplicate short")
expect_schema({"options":{"alpha":{"type":"bool","required":1}}}, "invalid_required", "alpha", "required type")
expect_schema({"options":{"alpha":{"type":"bool","repeat":"yes"}}}, "invalid_repeat", "alpha", "repeat type")
expect_schema({"options":{"alpha":{"type":"bool","negatable":null}}}, "invalid_negatable", "alpha", "negatable type")
expect_schema({"options":{"alpha":{"type":"string","negatable":true}}}, "invalid_negatable_type", "alpha", "negatable string")
expect_schema({"options":{"alpha":{"type":"int","required":true,"default":1}}}, "required_default", "alpha", "required default")
expect_schema({"options":{"alpha":{"type":"bool","default":"false"}}}, "invalid_default", "alpha", "bool default type")
expect_schema({"options":{"alpha":{"type":"int","default":1.5}}}, "invalid_default", "alpha", "int default type")
expect_schema({"options":{"alpha":{"type":"string","repeat":true,"default":"x"}}}, "invalid_default", "alpha", "repeat default array")
expect_schema({"options":{"alpha":{"type":"int","repeat":true,"default":[1,"2"]}}}, "invalid_default", "alpha", "repeat element type")
expect_schema({"options":{"foo":{"type":"bool","negatable":true},"no-foo":{"type":"bool"}}}, "negated_name_collision", "foo", "negated collision")
expect_schema({"options":{"no-foo":{"type":"bool"},"foo":{"type":"bool","negatable":true}}}, "negated_name_collision", "foo", "reversed negated collision")
expect_schema({"options":{"no-no-foo":{"type":"bool"},"no-foo":{"type":"bool","negatable":true}}}, "negated_name_collision", "no-foo", "nested negated collision")

unicode_letter := bytes([195, 169]).decode("utf-8")
unicode_name_spec := {"options":{}}
unicode_options := unicode_name_spec.options
unicode_options[unicode_letter] = {"type":"bool"}
unicode_name_spec["options"] = unicode_options
expect_schema(unicode_name_spec, "invalid_option_name", unicode_letter, "Unicode long name")
expect_schema({"options":{"alpha":{"type":"bool","short":unicode_letter}}}, "invalid_short", "alpha", "Unicode short")

precedence_name_first := {"options":{"bad_name":{"type":"bool"},"alpha":{"type":"float"}}}
precedence_type_first := {"options":{"alpha":{"type":"float"},"bad_name":{"type":"bool"}}}
expect_schema(precedence_name_first, "invalid_option_name", "bad_name", "schema order name first")
expect_schema(precedence_type_first, "invalid_type", "alpha", "schema order type first")
expect_schema({"options":{"alpha":{"type":"bool","z":1,"a":2}}}, "unknown_descriptor_field", "alpha", "unknown descriptor order za")
expect_schema({"options":{"alpha":{"a":2,"z":1,"type":"bool"}}}, "unknown_descriptor_field", "alpha", "unknown descriptor order az")
expect_schema({"z":1,"options":{},"a":2}, "unknown_spec_field", null, "unknown top order zoa")
expect_schema({"a":2,"options":{},"z":1}, "unknown_spec_field", null, "unknown top order aoz")
expect_schema({"options":{"alpha":{"required":1,"unknown":true}}}, "unknown_descriptor_field", "alpha", "unknown before missing and malformed")

case_short_spec := {"options":{"lower":{"type":"bool","short":"a"},"upper":{"type":"bool","short":"A"}}}
expect(cli.validate_spec(case_short_spec).ok, "case-sensitive shorts validate")
case_shorts := cli.parse(["-a", "-A"], case_short_spec)
expect(case_shorts.ok && case_shorts.options.lower && case_shorts.options.upper, "case-sensitive shorts parse")

nested_negation_spec := {"options":{"no-foo":{"type":"bool","negatable":true}}}
nested_negation := cli.parse(["--no-no-foo"], nested_negation_spec)
expect(nested_negation.ok && !nested_negation.options["no-foo"] && nested_negation.present[0] == "no-foo", "nested no spelling")

spec := {"options":{
    "verbose":{"type":"bool","short":"v","negatable":true},
    "name":{"type":"string","short":"n"},
    "count":{"type":"int","short":"c"},
    "tag":{"type":"string","short":"t","repeat":true,"default":["default"]},
    "switch":{"type":"bool","repeat":true,"negatable":true},
    "mode":{"type":"string","default":"safe"},
    "jobs":{"type":"int","default":2},
    "color":{"type":"bool","default":true}
}}
expect(cli.validate_spec(spec).ok, "full spec validates")

defaults := cli.parse([], spec)
expect(defaults.ok, "defaults parse")
expect(defaults.options.keys().join(",") == "verbose,name,count,tag,switch,mode,jobs,color", "option insertion order")
expect(!defaults.options.verbose && defaults.options["name"] == null && defaults.options.count == null, "absent scalar values")
expect(defaults.options.tag.size() == 1 && defaults.options.tag[0] == "default", "repeat declared default")
expect(defaults.options.switch.size() == 0, "repeat absent default")
expect(defaults.options.mode == "safe" && defaults.options.jobs == 2 && defaults.options.color, "declared scalar defaults")
expect(defaults.present.size() == 0, "defaults excluded from present")

mutable_values := [1, 2]
mutation_spec := {"options":{"values":{"type":"int","repeat":true,"default":mutable_values}}}
mutation_first := cli.parse([], mutation_spec)
mutable_values.push(3)
expect(mutation_first.options.values.size() == 2, "result independent from source default array")
mutation_second := cli.parse([], mutation_spec)
expect(mutation_second.options.values.size() == 2, "spec independent from source default array")
changed_result := mutation_first.options.values
changed_result.push(9)
changed_options := mutation_first.options
changed_options["values"] = changed_result
mutation_first["options"] = changed_options
mutation_third := cli.parse([], mutation_spec)
expect(mutation_third.options.values.size() == 2 && mutation_third.options.values[1] == 2, "parse results mutation-independent")

mixed := cli.parse(["input", "-v", "--name=a=b", "middle", "-c", "-0002", "--tag=x", "-t", "y", "--switch", "--no-switch", "--", "--name", "-", ""], spec)
expect(mixed.ok, "mixed parse")
expect(mixed.options.verbose && mixed.options["name"] == "a=b" && mixed.options.count == -2, "mixed scalar values")
expect(mixed.options.tag.size() == 2 && mixed.options.tag[0] == "x" && mixed.options.tag[1] == "y", "explicit repeat replaces default")
expect(mixed.options.switch.size() == 2 && mixed.options.switch[0] && !mixed.options.switch[1], "repeat bool polarity")
expect(mixed.present.join(",") == "verbose,name,count,tag,switch", "present canonical encounter order")
expect(mixed.positionals.size() == 5 && mixed.positionals[0] == "input" && mixed.positionals[1] == "middle", "interspersed positionals")
expect(mixed.positionals[2] == "--name" && mixed.positionals[3] == "-" && mixed.positionals[4] == "", "marker verbatim tail")

unicode_value := "caf" + bytes([195, 169]).decode("utf-8")
tail := cli.parse(["-", "", "--", "--x", "$HOME;$(touch nope)", unicode_value], empty_spec)
expect(tail.ok && tail.positionals.size() == 5, "lone and marker positionals")
expect(tail.positionals[0] == "-" && tail.positionals[1] == "" && tail.positionals[2] == "--x", "positionals verbatim")
expect(tail.positionals[3] == "$HOME;$(touch nope)" && tail.positionals[4] == unicode_value, "unicode and shell data inert")

string_spec := {"options":{"value":{"type":"string","short":"s"}}}
expect(cli.parse(["--value="], string_spec).options.value == "", "empty equals string")
expect(cli.parse(["--value=a=b=c"], string_spec).options.value == "a=b=c", "equals retained")
expect(cli.parse(["-s", ""], string_spec).options.value == "", "empty separated string")
expect(cli.parse(["--value=-file"], string_spec).options.value == "-file", "dash string equals")
expect_argv(["--value", "-file"], string_spec, "missing_value", "value", 0, "dash separated string")
expect_argv(["-s", "--value=x"], string_spec, "missing_value", "value", 0, "short missing before option")

int_spec := {"options":{"number":{"type":"int","short":"i"}}}
expect(cli.parse(["--number=+000"], int_spec).options.number == 0, "plus leading zero integer")
expect(cli.parse(["--number", "-9223372036854775808"], int_spec).options.number == -9223372036854775808, "int64 minimum")
expect(cli.parse(["-i", "9223372036854775807"], int_spec).options.number == 9223372036854775807, "int64 maximum")
expect_argv(["--number="], int_spec, "invalid_integer", "number", 0, "empty integer")
expect_argv(["--number= 1"], int_spec, "invalid_integer", "number", 0, "integer whitespace")
expect_argv(["--number=0x10"], int_spec, "invalid_integer", "number", 0, "integer prefix")
expect_argv(["--number=12x"], int_spec, "invalid_integer", "number", 0, "integer partial")
expect_argv(["--number=9223372036854775808"], int_spec, "integer_overflow", "number", 0, "positive overflow")
expect_argv(["--number", "-9223372036854775809"], int_spec, "integer_overflow", "number", 1, "negative overflow consumed")
expect_argv(["--number", "--unknown"], int_spec, "missing_value", "number", 0, "integer does not consume option")

bool_spec := {"options":{"force":{"type":"bool","short":"f","negatable":true},"plain":{"type":"bool","short":"p"}}}
expect(cli.parse(["--force"], bool_spec).options.force, "positive bool")
expect(!cli.parse(["--no-force"], bool_spec).options.force, "negative bool")
expect_argv(["--force=false"], bool_spec, "unexpected_value", "force", 0, "bool long value")
expect_argv(["--no-force=x"], bool_spec, "unexpected_value", "force", 0, "negated bool value")
expect_argv(["--no-plain"], bool_spec, "unknown_option", "no-plain", 0, "undeclared negation")
expect_argv(["--force", "-f"], bool_spec, "duplicate_option", "force", 1, "alias duplicate")
expect_argv(["--force", "--no-force"], bool_spec, "duplicate_option", "force", 1, "polarity duplicate")

expect_argv(["--missing"], empty_spec, "unknown_option", "missing", 0, "unknown long")
expect_argv(["---"], empty_spec, "unknown_option", "-", 0, "triple dash")
expect_argv(["-z"], empty_spec, "unknown_option", "z", 0, "unknown short")
expect_argv(["-ff"], bool_spec, "invalid_short_form", null, 0, "short cluster")
expect_argv(["-svalue"], string_spec, "invalid_short_form", null, 0, "attached short value")
expect_argv(["-s=value"], string_spec, "invalid_short_form", null, 0, "short equals")
expect_argv(["--value"], string_spec, "missing_value", "value", 0, "long value at end")
expect_argv(["-s"], string_spec, "missing_value", "value", 0, "short value at end")

required_spec := {"options":{"first":{"type":"string","required":true},"second":{"type":"bool","required":true},"other":{"type":"int","default":9}}}
missing := cli.parse([], required_spec)
expect(!missing.ok && missing.diagnostics.size() == 2, "multiple missing required")
expect(missing.diagnostics[0].code == "missing_required" && missing.diagnostics[0].option == "first", "first required order")
expect(missing.diagnostics[1].code == "missing_required" && missing.diagnostics[1].option == "second", "second required order")
expect(missing.options.size() == 0 && missing.present.size() == 0 && missing.positionals.size() == 0, "missing required no partial")
expect(cli.parse(["--first=x", "--second"], required_spec).ok, "required explicit")

expect_argv(null, empty_spec, "invalid_argv", null, null, "null argv")
expect_argv(["ok", 1], empty_spec, "invalid_argument", null, 1, "non-string argv")

first := cli.parse(["--no-force", "x"], bool_spec)
second := cli.parse(["--no-force", "x"], bool_spec)
expect(first.ok == second.ok && first.options.force == second.options.force, "deterministic values")
expect(first.present.join(",") == second.present.join(",") && first.positionals.join(",") == second.positionals.join(","), "deterministic arrays")

print("PASS cli contract " + assertions.to_string() + " assertions")
