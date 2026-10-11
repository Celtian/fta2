// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

static int launchError(DWORD code) {
    const std::wstring message =
        L"Cannot start bin\\fta2-native.exe. Keep the extracted package together.\n\nWindows error: " +
        std::to_wstring(code);
    MessageBoxW(nullptr, message.c_str(), L"Faery Tale Adventure II", MB_OK | MB_ICONERROR);
    return 1;
}

static int launchGame() {
    std::vector<wchar_t> filename(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, filename.data(), DWORD(filename.size()));
        if (!length)
            return launchError(GetLastError());
        if (length < filename.size())
            break;
        if (filename.size() >= 32768)
            return launchError(ERROR_FILENAME_EXCED_RANGE);
        filename.resize(32768);
    }
    std::wstring directory(filename.data());
    const auto separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return launchError(ERROR_BAD_PATHNAME);
    directory.resize(separator);
    const std::wstring executable = directory + L"\\bin\\fta2-native.exe";

    // Preserve the child's argument quoting exactly. A Windows executable path
    // cannot contain a literal quote, so only its enclosing quotes need skipping.
    const wchar_t* arguments = GetCommandLineW();
    bool quoted = false;
    while (*arguments) {
        if (*arguments == L'"')
            quoted = !quoted;
        else if (!quoted && (*arguments == L' ' || *arguments == L'\t'))
            break;
        ++arguments;
    }
    while (*arguments == L' ' || *arguments == L'\t')
        ++arguments;
    std::wstring command = L"\"" + executable + L"\"";
    if (*arguments)
        command += std::wstring(L" ") + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
        return launchError(GetLastError());
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    return int(result);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        return launchGame();
    } catch (...) {
        MessageBoxW(nullptr, L"Cannot start the game. Keep the extracted package together.",
                    L"Faery Tale Adventure II", MB_OK | MB_ICONERROR);
        return 1;
    }
}
