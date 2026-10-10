// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/diagnostics.h"
#include "platform/runtime.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <csignal>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int run(int argc, char** argv) {
    if (argc != 3)
        return 2;
    Native::Diagnostics session;
    const std::string mode = argv[1];
    const Native::Path directory(argv[2]);
    const bool started =
        session.start(directory, "synthetic-test", mode == "limit" ? 65536 : 8 * 1024 * 1024);
    if (mode == "unwritable")
        return started ? 3 : 0;
    if (!started)
        return 4;
    Native::diagnosticContext(123, 32768, 0, 13258, 12521, 8);
    Native::diagnosticPhase("synthetic operation");
    Native::diagnosticMessage("TEST", "ordinary message %d", 42);
    warning("synthetic warning");
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "synthetic SDL warning");
    if (mode == "capabilities") {
        puts(Native::nativeCrashHandlersEnabled() ? "native-handlers" : "sanitizer-handlers");
    } else if (mode == "fault") {
#ifdef _WIN32
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
        std::raise(SIGSEGV);
#endif
        return 5;
    } else if (mode == "abort") {
        std::abort();
    } else if (mode == "terminate") {
        throw std::runtime_error("synthetic uncaught exception");
    } else if (mode == "limit") {
        const std::string text(1000, 'x');
        for (int i = 0; i < 100; ++i)
            Native::diagnosticMessage("TEST", "%s", text.c_str());
        Native::diagnosticFatal("fatal after size limit");
    } else {
        // Reading while still open proves messages are not lost in stdio buffers.
        std::ifstream input(session.path());
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        if (text.find("ordinary message 42") == std::string::npos ||
            text.find("synthetic SDL warning") == std::string::npos)
            return 6;
    }
    return 0;
}
int main(int argc, char** argv) {
#ifdef _WIN32
    // Match the application's Unicode command-line handling, independent of the CRT code page.
    return SDL_RunApp(0, nullptr, run, nullptr);
#else
    return SDL_RunApp(argc, argv, run, nullptr);
#endif
}
