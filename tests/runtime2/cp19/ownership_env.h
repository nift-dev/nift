#pragma once
// Keep the frozen reviewer probe byte-for-byte while providing Windows test
// environment setters. Production environment handling is not changed.
#ifdef _WIN32
#include <cstdlib>
inline int cp19_review_setenv(const char* name, const char* value, int overwrite) {
    return !overwrite && std::getenv(name) ? 0 : _putenv_s(name, value);
}
inline int cp19_review_unsetenv(const char* name) { return _putenv_s(name, ""); }
#define setenv cp19_review_setenv
#define unsetenv cp19_review_unsetenv
#endif
