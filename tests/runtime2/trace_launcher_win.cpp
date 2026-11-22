// Test-only Windows CRT launcher for an inherited, dedicated trace descriptor.
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int fail(const char* message) {
    std::fprintf(stderr, "trace launcher: %s\n", message);
    return 125;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return fail("expected TRACE_FILE NIFT SCRIPT");

    // The Python timeout terminates this launcher. Keep the job handle open
    // until process exit so that a timeout also terminates its descendants.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, GetCurrentProcess()))
        return fail("could not bound child lifetime");

    const int fd = _wopen(argv[1], _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd <= 2) return fail("could not open a dedicated trace descriptor");
    if (_wputenv_s(L"NIFT_TRACE_HOST", std::to_wstring(fd).c_str()))
        return fail("could not set trace descriptor");

    // _wspawnv passes the inherited CRT descriptor table to the native child.
    // The two arguments are file names, so neither can contain a quotation mark.
    const std::wstring executable = std::wstring(L"\"") + argv[2] + L"\"";
    const std::wstring script = std::wstring(L"\"") + argv[3] + L"\"";
    const wchar_t* args[] = {executable.c_str(), script.c_str(), nullptr};
    const intptr_t status = _wspawnv(_P_WAIT, argv[2], args);
    _close(fd);
    return status == -1 ? fail("could not launch Nift") : static_cast<int>(status);
}
