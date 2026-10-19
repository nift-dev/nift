#include "rt/handles/limit_setting.h"

#include <cstdlib>

namespace nift::rt::handles {

HandleLimitSetting parse_handle_limit(const char* value) {
    HandleLimitSetting setting;
    if (!value || !*value) return setting;
    const std::string text(value);
    std::uint64_t number = 0;
    std::size_t i = 0;
    bool valid = true;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        number = number * 10 + static_cast<std::uint64_t>(text[i] - '0');
        if (number > 0xFFFFFFFFull) { valid = false; break; }
    }
    unsigned shift = 0;
    if (valid && i + 1 == text.size() && (text[i] == 'K' || text[i] == 'k')) shift = 10;
    else if (valid && i + 1 == text.size() && (text[i] == 'M' || text[i] == 'm')) shift = 20;
    else if (i != text.size()) valid = false;
    if (i == 0 || number == 0 || (number << shift) > 0xFFFFFFFFull) valid = false;
    if (!valid) {
        setting.valid = false;
        setting.raw = text;
        return setting;
    }
    setting.capacity = static_cast<std::uint32_t>(number << shift);
    return setting;
}

TableOptions table_options_from_environment() {
    const HandleLimitSetting setting = parse_handle_limit(std::getenv(kHandleLimitSetting));
    TableOptions options;
    options.capacity = setting.capacity;
    if (!setting.valid) options.invalid_setting = setting.raw;
    return options;
}

} // namespace nift::rt::handles
