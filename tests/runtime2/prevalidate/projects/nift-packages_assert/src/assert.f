/*
    assert package for Nift: lightweight assertion/matcher helpers for scripts
    and package tests.

    Public API: the exported `assert` struct. Helpers stay private.

    Failure model: a failed assertion throws an Error via the Nift `throw`
    statement (recoverable/user-visible). In an ordinary script a failed
    assertion therefore terminates the script with a non-zero exit and a
    deterministic diagnostic on stderr; callers may also catch it with
    `try { ... } catch (e) { ... }`. Every matcher returns true on success.

    Equality uses Nift's `==`, which deep-compares arrays, objects and bytes.

    Diagnostic rendering of compared values is bounded so huge values are
    truncated, not dumped.
*/

assert_max_brief_chars := 200

struct(assert) {
    private fn(truthy(value)) {
        if(value) { return true }
        return false
    }

    private fn(brief(value)) {
        t := type(value)
        if(t == "null") { return "null" }
        if(t == "string") {
            rendered := value.stringify()
            return this.trim(rendered)
        }
        if(t == "bytes") { return "<bytes:" + value.size().to_string() + ">" }
        if(t == "int" || t == "float" || t == "bool") { return value.to_string() }
        rendered_value := ""
        try { rendered_value = value.stringify() } catch (any) { rendered_value = "<" + t + ">" }
        return this.trim(rendered_value)
    }

    private fn(trim(text)) {
        if(text.length() <= assert_max_brief_chars) { return text }
        return text.split("").slice(0, assert_max_brief_chars).join("") + "..."
    }

    private fn(fail(base, ...rest)) {
        if(rest.size() > 0) { throw error(base + ": " + rest[0]) }
        throw error(base)
    }

    fn(ok(value, ...rest)) {
        if(!this.truthy(value)) { this.fail("assert.ok failed: expected a truthy value", ...rest) }
        return true
    }

    fn(falsy(value, ...rest)) {
        if(this.truthy(value)) { this.fail("assert.falsy failed: expected a falsy value, got " + this.brief(value), ...rest) }
        return true
    }

    fn(eq(actual, expected, ...rest)) {
        equal := actual == expected
        if(!equal) {
            this.fail("assert.eq failed: expected " + this.brief(actual) + " == " + this.brief(expected), ...rest)
        }
        return true
    }

    fn(ne(actual, expected, ...rest)) {
        equal := actual == expected
        if(equal) {
            this.fail("assert.ne failed: expected different values, both " + this.brief(actual), ...rest)
        }
        return true
    }

    fn(is_null(value, ...rest)) {
        if(value != null) { this.fail("assert.is_null failed: expected null, got " + this.brief(value), ...rest) }
        return true
    }

    fn(not_null(value, ...rest)) {
        if(value == null) { this.fail("assert.not_null failed: expected a non-null value", ...rest) }
        return true
    }

    fn(contains(container, value, ...rest)) {
        t := type(container)
        found := false
        if(t == "string") { found = container.contains(value) }
        else if(t == "array") { found = container.contains(value) }
        else if(t == "object") { found = container.has(value) }
        else { this.fail("assert.contains failed: expected a string, array or object container, got " + t, ...rest) }
        if(!found) { this.fail("assert.contains failed: " + this.brief(container) + " does not contain " + this.brief(value), ...rest) }
        return true
    }

    fn(throws(callable, ...rest)) {
        if(type(callable) != "function") { this.fail("assert.throws failed: expected a callable", ...rest) }
        try { callable() } catch (thrown_error) {
            return true
        }
        this.fail("assert.throws failed: callable did not raise an error", ...rest)
        return true
    }
}

assert := assert()
export(assert)