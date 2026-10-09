// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/startup.h"
namespace Native {
Path discoverGameDirectory(const Path& remembered, const Path& working, const Path& executable) {
    // SDL_GetBasePath includes a trailing separator; parent_path must operate
    // on the directory itself rather than on its empty final component.
    Path executableDirectory = executable;
    if (executableDirectory.filename().empty())
        executableDirectory = executableDirectory.parent_path();
    // A bundled data directory is authoritative even when incomplete or damaged.
    // Validation must report the problem instead of selecting another installation.
    if (!executableDirectory.empty()) {
        for (const auto& bundled : {Path(executableDirectory / "data"),
                                    Path(executableDirectory.parent_path() / "data")}) {
            std::error_code ec;
            bool exists = std::filesystem::exists(bundled, ec);
            if (ec)
                throw std::runtime_error("Cannot inspect bundled game data: " +
                                         bundled.toString() + ": " + ec.message());
            if (exists)
                return bundled;
        }
    }
    for (const auto& candidate : {remembered, Path(working / "original-game"),
                                  Path(executableDirectory / "original-game"),
                                  Path(executableDirectory.parent_path() / "original-game"),
                                  working, executableDirectory}) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate / "FTA2.EXE", ec))
            return candidate;
    }
    return {};
}
#ifdef _WIN32
Path chooseGameDirectory() {
    std::unique_ptr<SDL_Semaphore, decltype(&SDL_DestroySemaphore)> ready(
        SDL_CreateSemaphore(0), SDL_DestroySemaphore);
    if (!ready)
        throw std::runtime_error(SDL_GetError());
    struct Selection {
        SDL_Semaphore* ready;
        Path path;
        String error;
    } selection{ready.get(), {}, {}};
    auto props = SDL_CreateProperties();
    if (!props)
        throw std::runtime_error(SDL_GetError());
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                          "Select the Faery Tale Adventure II game data folder (FTA2.EXE)");
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, "Use game folder");
    if (const char* base = SDL_GetBasePath())
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, base);
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_OPENFOLDER,
        [](void* userdata, const char* const* files, int) {
            auto& result = *static_cast<Selection*>(userdata);
            if (!files)
                result.error = SDL_GetError();
            else if (files[0])
                result.path = files[0];
            SDL_SignalSemaphore(result.ready);
        },
        &selection, props);
    SDL_WaitSemaphore(ready.get());
    SDL_DestroyProperties(props);
    if (!selection.error.empty())
        throw std::runtime_error("Cannot open the game folder dialog: " + selection.error);
    return selection.path;
}
#endif
} // namespace Native
