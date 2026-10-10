// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <filesystem>

namespace Native {
// One process owns a session; destroy it after the engine and audio threads stop.
class Diagnostics {
    std::filesystem::path _path;

  public:
    Diagnostics() = default;
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    ~Diagnostics();
    bool start(const std::filesystem::path& directory, const char* version,
               size_t byteLimit = 8 * 1024 * 1024) noexcept;
    const std::filesystem::path& path() const { return _path; }
};

void diagnosticMessage(const char* level, const char* format, ...) noexcept;
// Phase strings must have static storage, so a fatal handler can read them safely.
void diagnosticPhase(const char* phase) noexcept;
void diagnosticContext(unsigned tick, int actor, int map, int u, int v, int z) noexcept;
void diagnosticFatal(const char* message) noexcept;
bool nativeCrashHandlersEnabled() noexcept;
} // namespace Native
