// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/diagnostics.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <csignal>
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#else
#include <csignal>
#include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define FTA2_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FTA2_ASAN 1
#endif
#endif

namespace Native {
namespace {
size_t maxLogBytes = 8 * 1024 * 1024;
std::mutex logMutex;
FILE* logFile = nullptr;
size_t logBytes = 0;
SDL_LogOutputFunction oldOutput = nullptr;
void* oldUserdata = nullptr;
std::terminate_handler oldTerminate = nullptr;
std::atomic<const char*> phase{"startup"};
std::array<std::atomic<int>, 6> context{};
std::chrono::steady_clock::time_point lastContext;
bool installed = false;
#ifndef _WIN32
size_t signalCount = 0;
#endif
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<const char*>::is_always_lock_free);
#ifdef _WIN32
HANDLE crashLog = INVALID_HANDLE_VALUE;
HANDLE dumpEvent = nullptr, dumpDone = nullptr, dumpThread = nullptr;
HMODULE dbgHelp = nullptr;
using DumpFunction = decltype(&MiniDumpWriteDump);
DumpFunction writeDump = nullptr;
EXCEPTION_POINTERS* dumpException = nullptr;
DWORD dumpThreadID = 0;
std::array<wchar_t, 32768> dumpPath{};
LPTOP_LEVEL_EXCEPTION_FILTER oldFilter = nullptr;
LONG crashEntered = 0;
using SignalHandler = void (*)(int);
SignalHandler oldAbort = nullptr;
#else
int crashLog = -1;
constexpr int fatalSignals[] = {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS};
std::array<struct sigaction, std::size(fatalSignals)> oldActions{};
#endif

// Fixed storage and OS writes only: no stream locks, allocation or printf in signals.
struct CrashLine {
    char bytes[4096];
    size_t length = 0;
    void text(const char* value) {
        if (value)
            while (*value && length < sizeof(bytes))
                bytes[length++] = *value++;
    }
    void number(uintptr_t value, unsigned base = 10) {
        char digits[32];
        size_t count = 0;
        do {
            digits[count++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value && count < sizeof(digits));
        while (count && length < sizeof(bytes))
            bytes[length++] = digits[--count];
    }
    void emit() {
#ifdef _WIN32
        if (crashLog != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(crashLog, bytes, static_cast<DWORD>(length), &written, nullptr);
            FlushFileBuffers(crashLog);
        }
#else
        if (crashLog >= 0) {
            // A short best-effort record is preferable to retrying indefinitely in a crash.
            auto ignored = write(crashLog, bytes, length);
            (void)ignored;
        }
#endif
    }
};

void fatalRecord(const char* reason, uintptr_t code, uintptr_t address) {
    CrashLine line;
    line.text("\nFATAL: ");
    line.text(reason);
    line.text(" code=0x");
    line.number(code, 16);
    line.text(" address=0x");
    line.number(address, 16);
#ifdef _WIN32
    line.text(" executable_base=0x");
    line.number(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), 16);
#endif
    line.text("\nLast phase: ");
    line.text(phase.load(std::memory_order_relaxed));
    constexpr const char* names[] = {" tick=", " actor=", " map=", " u=", " v=", " z="};
    for (size_t i = 0; i < context.size(); ++i) {
        line.text(names[i]);
        const int value = context[i].load(std::memory_order_relaxed);
        if (value < 0)
            line.text("-");
        line.number(value < 0 ? -int64_t(value) : value);
    }
    line.text("\n");
    line.emit();
}

#ifdef _WIN32
BOOL CALLBACK dumpCallback(void* parameter, MINIDUMP_CALLBACK_INPUT* input,
                           MINIDUMP_CALLBACK_OUTPUT*) {
    // DbgHelp cannot reliably capture the thread executing MiniDumpWriteDump.
    // Exclude only that worker; retain the faulting thread and its saved context.
    switch (input->CallbackType) {
    case IncludeThreadCallback:
        return input->IncludeThread.ThreadId != *static_cast<const DWORD*>(parameter);
    case IncludeModuleCallback:
    case ModuleCallback:
    case ThreadCallback:
    case ThreadExCallback:
        return TRUE;
    default:
        // No extra memory, custom I/O or ignored read failures.
        return FALSE;
    }
}

DWORD WINAPI dumpWorker(void*) {
    WaitForSingleObject(dumpEvent, INFINITE);
    if (!dumpException)
        return 0; // Clean shutdown.
    HANDLE file = CreateFileW(dumpPath.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL success = FALSE;
    DWORD failure = GetLastError();
    DWORD writerThreadID = GetCurrentThreadId();
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{dumpThreadID, dumpException, FALSE};
        MINIDUMP_CALLBACK_INFORMATION callback{dumpCallback, &writerThreadID};
        success = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info,
                            nullptr, &callback);
        failure = success ? 0 : GetLastError();
        CloseHandle(file);
        if (!success)
            DeleteFileW(dumpPath.data());
    }
    CrashLine line;
    line.text("Crash dump writer thread=");
    line.number(writerThreadID);
    line.text("\n");
    line.text(success ? "Crash dump written beside the session log.\n" : "Crash dump failed: ");
    if (!success) {
        line.number(failure);
        line.text("\n");
    }
    line.emit();
    SetEvent(dumpDone);
    return 0;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* exception) {
    if (InterlockedCompareExchange(&crashEntered, 1, 0) != 0)
        return EXCEPTION_EXECUTE_HANDLER;
    fatalRecord("Windows exception", exception->ExceptionRecord->ExceptionCode,
                reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress));
    if (dumpThread) {
        dumpException = exception;
        dumpThreadID = GetCurrentThreadId();
        SetEvent(dumpEvent);
        if (WaitForSingleObject(dumpDone, 10000) != WAIT_OBJECT_0) {
            CrashLine line;
            line.text("Crash dump timed out.\n");
            line.emit();
        }
    }
    // Preserve the original dump before MSVC's filter converts C++ exceptions
    // to std::terminate: abort inside that filter need not reenter our handler.
    if (exception->ExceptionRecord->ExceptionCode == 0xe06d7363 && oldFilter &&
        oldFilter != crashFilter) {
        const LONG result = oldFilter(exception);
        if (result != EXCEPTION_CONTINUE_SEARCH)
            return result;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void abortHandler(int) {
    fatalRecord("abort", SIGABRT, 0);
    RaiseException(0xe000f7a2, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    _Exit(134);
}
#else
void signalHandler(int signal, siginfo_t* info, void*) {
    fatalRecord("POSIX signal", signal,
                reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
    // Restore the OS disposition so exit status and system core-dump handling survive.
    for (size_t i = 0; i < std::size(fatalSignals); ++i)
        if (fatalSignals[i] == signal)
            sigaction(signal, &oldActions[i], nullptr);
    raise(signal);
}
#endif

void terminateHandler() {
    const char* reason = "std::terminate without a current exception";
    try {
        if (auto exception = std::current_exception())
            std::rethrow_exception(exception);
    } catch (const std::exception& exception) {
        diagnosticFatal(exception.what());
        reason = "unhandled C++ exception";
    } catch (...) {
        reason = "unhandled non-standard C++ exception";
    }
    diagnosticFatal(reason);
    std::abort();
}

void SDLCALL sdlOutput(void* userdata, int category, SDL_LogPriority priority, const char* text) {
    diagnosticMessage("SDL", "category=%d priority=%d %s", category, int(priority), text);
    if (oldOutput)
        oldOutput(userdata, category, priority, text);
}

void prune(const std::filesystem::path& directory, const char* extension, size_t keep) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("fta2-") &&
            entry.path().extension() == extension)
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    for (size_t i = 0; i + keep < paths.size(); ++i) {
        std::error_code ignored;
        std::filesystem::remove(paths[i], ignored);
    }
}
} // namespace

void diagnosticMessage(const char* level, const char* format, ...) noexcept {
    try {
        std::lock_guard lock(logMutex);
        if (!logFile || logBytes >= maxLogBytes)
            return;
        char text[2048];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        auto now = std::chrono::system_clock::now();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        const auto seconds = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &seconds);
#else
        gmtime_r(&seconds, &utc);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &utc);
        const int count = fprintf(logFile, "%s.%03lldZ [%s] %s\n", stamp,
                                  static_cast<long long>(ms % 1000), level, text);
        if (count > 0)
            logBytes += size_t(count);
        if (logBytes >= maxLogBytes)
            fputs(
                "Log size limit reached; routine logging stopped. Fatal records remain enabled.\n",
                logFile);
    } catch (...) {
        // Logging must never prevent gameplay or replace the original error.
    }
}

void diagnosticPhase(const char* value) noexcept { phase.store(value, std::memory_order_relaxed); }
void diagnosticContext(unsigned tick, int actor, int map, int u, int v, int z) noexcept {
    const int values[] = {static_cast<int>(tick), actor, map, u, v, z};
    for (size_t i = 0; i < context.size(); ++i)
        context[i].store(values[i], std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    if (now - lastContext >= std::chrono::seconds(5)) {
        lastContext = now;
        diagnosticMessage("WORLD", "tick=%u actor=%d map=%d position=(%d,%d,%d)", tick, actor, map,
                          u, v, z);
    }
}
void diagnosticFatal(const char* message) noexcept { fatalRecord(message, 0, 0); }
bool nativeCrashHandlersEnabled() noexcept { return installed; }

bool Diagnostics::start(const std::filesystem::path& directory, const char* version,
                        size_t byteLimit) noexcept {
    if (!_path.empty())
        return true;
    try {
        if (logFile)
            return false;
        std::filesystem::create_directories(directory);
        prune(directory, ".log", 9);
        prune(directory, ".dmp", 3);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &time);
        const unsigned pid = GetCurrentProcessId();
#else
        gmtime_r(&time, &utc);
        const unsigned pid = getpid();
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
        static unsigned sequence = 0;
        char name[96];
        snprintf(name, sizeof(name), "fta2-%s-%03lld-%u-%u.log", stamp,
                 static_cast<long long>(
                     std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                         .count() %
                     1000),
                 pid, ++sequence);
        auto path = directory / name;
        const auto utf8 = path.u8string();
#ifdef _WIN32
        auto dumpFile = path;
        const auto dump = dumpFile.replace_extension(".dmp").wstring();
#endif
        _path = path;
#ifdef _WIN32
        logFile = _wfopen(path.c_str(), L"ab");
#else
        logFile = fopen(path.c_str(), "ab");
#endif
        if (!logFile) {
            _path.clear();
            return false;
        }
        setvbuf(logFile, nullptr, _IONBF, 0);
        logBytes = 0;
        maxLogBytes = byteLimit;
        phase.store("startup");
        for (auto& value : context)
            value.store(0);
        lastContext = {};
#ifdef _WIN32
        crashLog = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump.size() < dumpPath.size()) {
            std::copy(dump.begin(), dump.end(), dumpPath.begin());
            dumpPath[dump.size()] = 0;
            dbgHelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (dbgHelp)
                writeDump =
                    reinterpret_cast<DumpFunction>(GetProcAddress(dbgHelp, "MiniDumpWriteDump"));
            if (writeDump) {
                dumpException = nullptr;
                dumpEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                dumpDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (dumpEvent && dumpDone)
                    dumpThread = CreateThread(nullptr, 0, dumpWorker, nullptr, 0, nullptr);
            }
        }
        crashEntered = 0;
        oldFilter = SetUnhandledExceptionFilter(crashFilter);
        oldAbort = std::signal(SIGABRT, abortHandler);
        installed = true;
#else
        crashLog = fileno(logFile);
#ifndef FTA2_ASAN
        struct sigaction action{};
        action.sa_sigaction = signalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        signalCount = 0;
        for (size_t i = 0; i < std::size(fatalSignals); ++i) {
            if (sigaction(fatalSignals[i], &action, &oldActions[i]) != 0)
                break;
            ++signalCount;
        }
        installed = signalCount == std::size(fatalSignals);
#endif
#endif
        oldTerminate = std::set_terminate(terminateHandler);
        SDL_GetLogOutputFunction(&oldOutput, &oldUserdata);
        SDL_SetLogOutputFunction(sdlOutput, oldUserdata);
        diagnosticMessage("SESSION", "fta2-ce %s; SDL %d; platform=%s; log=%s", version,
                          SDL_GetVersion(), SDL_GetPlatform(),
                          reinterpret_cast<const char*>(utf8.c_str()));
        diagnosticMessage("SESSION", "Platform crash handlers=%d", int(installed));
        return true;
    } catch (...) {
        return false;
    }
}

Diagnostics::~Diagnostics() {
    if (_path.empty())
        return;
    diagnosticMessage("SESSION", "Session logging closed");
    SDL_SetLogOutputFunction(oldOutput, oldUserdata);
    std::set_terminate(oldTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(oldFilter);
    std::signal(SIGABRT, oldAbort);
    if (dumpThread) {
        SetEvent(dumpEvent);
        WaitForSingleObject(dumpThread, INFINITE);
        CloseHandle(dumpThread);
    }
    if (dumpEvent)
        CloseHandle(dumpEvent);
    if (dumpDone)
        CloseHandle(dumpDone);
    if (dbgHelp)
        FreeLibrary(dbgHelp);
    dumpThread = dumpEvent = dumpDone = nullptr;
    dbgHelp = nullptr;
    writeDump = nullptr;
    if (crashLog != INVALID_HANDLE_VALUE)
        CloseHandle(crashLog);
    crashLog = INVALID_HANDLE_VALUE;
#else
    for (size_t i = 0; i < signalCount; ++i)
        sigaction(fatalSignals[i], &oldActions[i], nullptr);
    signalCount = 0;
    crashLog = -1;
#endif
    installed = false;
    std::lock_guard lock(logMutex);
    fclose(logFile);
    logFile = nullptr;
}
} // namespace Native
