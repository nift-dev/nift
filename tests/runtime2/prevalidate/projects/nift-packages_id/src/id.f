/*
    id package for Nift: UUID v4 and ULID (Crockford Base32) generation and
    validation.

    Public API: the exported `id` struct. Helpers stay private.

    Randomness comes from Nift's cryptographically secure
    `secure_random_bytes`; there is no pseudo-random fallback and no process
    backend. ULID timestamps come from Nift's millisecond `epoch()`.

    UUID output is RFC 4122/9562 version 4 with the RFC variant bits, in
    canonical lowercase 8-4-4-4-12 form.

    ULID output is the canonical 26-character Crockford Base32 form
    (uppercase), encoding a 48-bit millisecond timestamp followed by 80 bits of
    randomness. `id.ulid_at(ms)` generates deterministically at a supplied
    timestamp; `id.ulid()` uses the current time.

    rand_bytes_to_ints converts the immutable bytes value into an integer array
    via an explicit loop so the rest of the package only depends on array
    indexing.
*/

id_hex := "0123456789abcdef"
id_crock := "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
id_pow2 := [1, 2, 4, 8, 16, 32, 64, 128]
id_max_ulid_ms := 281474976710655

struct(id) {
    private fn(rand_bytes_to_ints(count)) {
        bytes := secure_random_bytes(count)
        ints := []
        i := 0
        while(i < count) { ints.push(bytes[i]); i += 1 }
        return ints
    }

    private fn(hex_byte(value)) {
        return id_hex.substr((value / 16).floor().to_int(), 1) + id_hex.substr(value % 16, 1)
    }

    fn(uuid()) {
        ints := this.rand_bytes_to_ints(16)
        text := ""
        i := 0
        while(i < 16) {
            value := ints[i]
            if(i == 6) { value = (value % 16) + 64 }
            else if(i == 8) { value = (value % 64) + 128 }
            text += this.hex_byte(value)
            if(i == 3 || i == 5 || i == 7 || i == 9) { text += "-" }
            i += 1
        }
        return text
    }

    private fn(encode_ulid(ints)) {
        text := ""
        acc := 0
        bits := 2
        i := 0
        while(i < 16) {
            value := ints[i]
            b := 7
            while(b >= 0) {
                acc = acc * 2 + ((value / id_pow2[b]).floor().to_int() % 2)
                bits += 1
                if(bits == 5) {
                    text += id_crock.substr(acc, 1)
                    acc = 0
                    bits = 0
                }
                b -= 1
            }
            i += 1
        }
        return text
    }

    private fn(timestamp_ints(milliseconds)) {
        little := []
        remaining := milliseconds
        k := 0
        while(k < 6) { little.push(remaining % 256); remaining = (remaining / 256).floor().to_int(); k += 1 }
        ordered := []
        k = 5
        while(k >= 0) { ordered.push(little[k]); k -= 1 }
        return ordered
    }

    fn(ulid_at(milliseconds)) {
        if(type(milliseconds) != "int" || milliseconds < 0 || milliseconds > id_max_ulid_ms) { return null }
        ints := this.timestamp_ints(milliseconds)
        random_ints := this.rand_bytes_to_ints(10)
        i := 0
        while(i < 10) { ints.push(random_ints[i]); i += 1 }
        return this.encode_ulid(ints)
    }

    fn(ulid()) { return this.ulid_at(epoch()) }

    private fn(is_hex_lower(c)) {
        return (c >= "0" && c <= "9") || (c >= "a" && c <= "f")
    }

    fn(valid_uuid(value)) {
        if(type(value) != "string" || value.length() != 36) { return false }
        i := 0
        while(i < 36) {
            c := value.substr(i, 1)
            if(i == 8 || i == 13 || i == 18 || i == 23) {
                if(c != "-") { return false }
            }
            else if(!this.is_hex_lower(c)) { return false }
            i += 1
        }
        version := value.substr(14, 1)
        if(!(version >= "1" && version <= "8")) { return false }
        variant := value.substr(19, 1)
        return variant == "8" || variant == "9" || variant == "a" || variant == "b"
    }

    fn(valid_ulid(value)) {
        if(type(value) != "string" || value.length() != 26) { return false }
        first := value.substr(0, 1)
        if(!(first >= "0" && first <= "7")) { return false }
        i := 0
        while(i < 26) {
            if(!id_crock.contains(value.substr(i, 1))) { return false }
            i += 1
        }
        return true
    }
}

id := id()
export(id)
