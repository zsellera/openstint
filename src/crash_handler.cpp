#include "crash_handler.hpp"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <ctime>
#include <string>

static LONG WINAPI crash_dump_handler(EXCEPTION_POINTERS* exception_pointers) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);

    std::string dump_path(exe_path);
    dump_path += ".";
    dump_path += std::to_string(static_cast<long long>(std::time(nullptr)));
    dump_path += ".dmp";

    HANDLE file = CreateFileA(dump_path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dump_info;
        dump_info.ThreadId = GetCurrentThreadId();
        dump_info.ExceptionPointers = exception_pointers;
        dump_info.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                           MiniDumpWithFullMemory, &dump_info, nullptr, nullptr);
        CloseHandle(file);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void install_crash_handler() {
    SetUnhandledExceptionFilter(crash_dump_handler);
}
#else
void install_crash_handler() {
}
#endif
