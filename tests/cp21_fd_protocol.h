#pragma once
// Strict grammar for the D3 descriptor-scanner transcript.
#include <cstddef>
#include <set>
#include <sstream>
#include <string>

namespace cp21 {

struct FdProtocolResult {
    bool valid = false;
    bool defect = false;
    long count = -1;
    std::string error;
};

inline bool parse_decimal(const std::string& text, long max_value, long& value) {
    if (text.empty() || text.size() > 9) return false;
    if (text.size() > 1 && text[0] == '0') return false;
    long parsed = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        parsed = parsed * 10 + (c - '0');
        if (parsed > max_value) return false;
    }
    value = parsed;
    return true;
}

inline FdProtocolResult parse_fd_protocol(const std::string& output, bool exited_normally,
                                          int exit_code) {
    FdProtocolResult result;
    std::istringstream stream(output);
    std::string line;
    long declared = -1;
    long records = 0;
    std::set<long> descriptors;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            result.error = "empty line";
            return result;
        }
        static const std::string marker = "exec_extra_fds=";
        static const std::string record = "extra_fd=";
        if (line.rfind(marker, 0) == 0) {
            if (declared >= 0) {
                result.error = "duplicate count line";
                return result;
            }
            long value = 0;
            if (!parse_decimal(line.substr(marker.size()), 512, value)) {
                result.error = "invalid count field";
                return result;
            }
            declared = value;
            continue;
        }
        if (line.rfind(record, 0) == 0) {
            const std::size_t fd_end = line.find(' ', record.size());
            if (fd_end == std::string::npos) {
                result.error = "malformed descriptor record";
                return result;
            }
            long fd = 0;
            if (!parse_decimal(line.substr(record.size(), fd_end - record.size()), 511, fd) || fd < 3) {
                result.error = "invalid descriptor number";
                return result;
            }
            if (!descriptors.insert(fd).second) {
                result.error = "duplicate descriptor record";
                return result;
            }
            const std::string flags_prefix = "flags=";
            if (line.compare(fd_end + 1, flags_prefix.size(), flags_prefix) != 0) {
                result.error = "missing flags field";
                return result;
            }
            const std::size_t flags_begin = fd_end + 1 + flags_prefix.size();
            const std::size_t flags_end = line.find(' ', flags_begin);
            if (flags_end == std::string::npos) {
                result.error = "malformed flags field";
                return result;
            }
            long flags = 0;
            if (!parse_decimal(line.substr(flags_begin, flags_end - flags_begin), 1, flags)) {
                result.error = "invalid flags field";
                return result;
            }
            const std::string target_prefix = "target=";
            if (line.compare(flags_end + 1, target_prefix.size(), target_prefix) != 0) {
                result.error = "missing target field";
                return result;
            }
            const std::string target = line.substr(flags_end + 1 + target_prefix.size());
            if (target.empty() || target.find_first_of(" \t") != std::string::npos) {
                result.error = "invalid target field";
                return result;
            }
            ++records;
            continue;
        }
        result.error = "unexpected output line";
        return result;
    }
    if (declared < 0) {
        result.error = "missing count line";
        return result;
    }
    if (records != declared) {
        result.error = "descriptor records disagree with count";
        return result;
    }
    if (!exited_normally) {
        result.error = "child did not exit normally";
        return result;
    }
    const int expected_exit = declared == 0 ? 0 : 1;
    if (exit_code != expected_exit) {
        result.error = "unexpected child exit status";
        return result;
    }
    result.valid = true;
    result.count = declared;
    result.defect = declared > 0;
    return result;
}

} // namespace cp21
