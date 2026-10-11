// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/startup.h"
namespace Native {
void importLegacyProfile(const Path& legacy, const Path& destination) {
    // Import only player-owned settings and save slots. Never copy extracted resources,
    // follow links, overwrite newer files, or remove the previous profile.
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::symlink_status(destination / "config.ini", ec)) ||
        legacy.empty())
        return;
    if (!std::filesystem::is_directory(legacy, ec))
        return;
    auto ancestor = std::filesystem::weakly_canonical(destination);
    while (!ancestor.empty()) {
        if (std::filesystem::is_regular_file(ancestor / "FTA2.EXE", ec))
            throw std::runtime_error("Player profile must be outside the game data directory");
        const auto parent = ancestor.parent_path();
        if (parent == ancestor)
            break;
        ancestor = parent;
    }
    std::filesystem::create_directories(destination);
    for (const auto& entry : std::filesystem::directory_iterator(legacy)) {
        const auto name = entry.path().filename().string();
        const bool save = name.size() == 8 && name.substr(0, 5) == "fta2." &&
                          name.find_first_not_of("0123456789", 5) == std::string::npos;
        if (!save || !std::filesystem::is_regular_file(entry.symlink_status()))
            continue;
        if (std::filesystem::exists(std::filesystem::symlink_status(destination / name, ec)))
            continue;
        std::filesystem::copy_file(entry.path(), destination / name,
                                   std::filesystem::copy_options::skip_existing);
    }
    const auto config = legacy / "config.ini";
    if (std::filesystem::is_regular_file(std::filesystem::symlink_status(config, ec)))
        std::filesystem::copy_file(config, destination / "config.ini",
                                   std::filesystem::copy_options::skip_existing);
}

Path defaultUserDirectory() {
    char* current = SDL_GetPrefPath("fta2-ce", "fta2-ce");
    if (!current)
        throw std::runtime_error(SDL_GetError());
    Path destination(current);
    SDL_free(current);
    if (destination.filename().empty())
        destination = destination.parent_path();
    // Pinned SDL uses organization/application on all supported desktop platforms.
    // Resolve the previous profile without creating it on a fresh installation.
    const Path legacy = destination.parent_path().parent_path() / "fta2-native" / "fta2-native";
    importLegacyProfile(legacy, destination);
    return destination;
}

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
                throw std::runtime_error("Cannot inspect bundled game data: " + bundled.toString() +
                                         ": " + ec.message());
            if (exists)
                return bundled;
        }
    }
    for (const auto& candidate :
         {remembered, Path(working / "original-game"), Path(executableDirectory / "original-game"),
          Path(executableDirectory.parent_path() / "original-game"), working,
          executableDirectory}) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate / "FTA2.EXE", ec))
            return candidate;
    }
    return {};
}
#ifdef _WIN32
Path chooseGameDirectory() {
    std::unique_ptr<SDL_Semaphore, decltype(&SDL_DestroySemaphore)> ready(SDL_CreateSemaphore(0),
                                                                          SDL_DestroySemaphore);
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
