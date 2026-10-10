// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/diagnostics.h"
#include "platform/probe.h"
#include "platform/resources.h"
#include "platform/runtime.h"
#include "platform/startup.h"
#include "saga2/saga2.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
static int runGame(int argc, char** argv) {
    bool interactive = argc == 1;
    Native::Diagnostics diagnostics;
    try {
        Native::Path data, saves, voice;
        bool probe = false;
        bool widescreenProbe = false, voiceProbe = false;
        int deathActor = 0;
        int smokeFrames = 260;
        Native::Path probeOutput;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                puts("fta2-native [--data-dir PATH] [--save-dir PATH] [--voice-file PATH]\nNative "
                     "Faery Tale Adventure "
                     "II; bundled game data is loaded automatically.\n--help       Show "
                     "help\n--version    "
                     "Show version");
                return 0;
            }
            if (arg == "--version") {
                puts("fta2-native " FTA2_VERSION " (SAGA2 1a3e816a; GPL-3.0-or-later)");
                return 0;
            }
            if (arg == "--smoke-test" && i + 1 < argc) {
                probe = true;
                probeOutput = argv[++i];
                continue;
            }
            if (arg == "--smoke-death" && i + 1 < argc) {
                deathActor = std::stoi(argv[++i]);
                if (deathActor != 32788 && deathActor != 32797)
                    throw std::runtime_error("Death probe requires villager 32788 or 32797");
                continue;
            }
            if (arg == "--smoke-voices") {
                voiceProbe = true;
                continue;
            }
            if (arg == "--smoke-widescreen") {
                widescreenProbe = true;
                continue;
            }
            if (arg == "--smoke-frames" && i + 1 < argc) {
                smokeFrames = std::stoi(argv[++i]);
                if (smokeFrames < 260 || smokeFrames > 1000000)
                    throw std::runtime_error("Smoke frames must be 260..1000000");
                continue;
            }
            if ((arg == "--data-dir" || arg == "--save-dir" || arg == "--voice-file") &&
                i + 1 < argc) {
                auto value = Native::Path(argv[++i]);
                if (arg == "--data-dir")
                    data = value;
                else if (arg == "--save-dir")
                    saves = value;
                else
                    voice = value;
                continue;
            }
            throw std::runtime_error("Unknown or incomplete option: " + arg);
        }

        if (deathActor && !probe)
            throw std::runtime_error("--smoke-death requires --smoke-test");
        if (voiceProbe && !probe)
            throw std::runtime_error("--smoke-voices requires --smoke-test");
        if (widescreenProbe && !probe)
            throw std::runtime_error("--smoke-widescreen requires --smoke-test PATH");
        if (saves.empty()) {
            char* p = SDL_GetPrefPath("fta2-native", "fta2-native");
            if (!p)
                throw std::runtime_error(SDL_GetError());
            saves = p;
            SDL_free(p);
        }
        saves = std::filesystem::absolute(saves).lexically_normal();
        ConfMan.file = saves / "config.ini";
        ConfMan.load();
        if (data.empty()) {
            const char* base = SDL_GetBasePath();
            data = Native::discoverGameDirectory(ConfMan.getPath("data_dir"),
                                                 std::filesystem::current_path(),
                                                 base ? Native::Path(base) : Native::Path());
#ifdef _WIN32
            interactive = !probe;
            if (data.empty() && interactive) {
                data = Native::chooseGameDirectory();
                if (data.empty())
                    return 0;
            }
#endif
        }
        if (data.empty())
            throw std::runtime_error("Game data folder not found. Use --data-dir PATH to select "
                                     "the folder containing FTA2.EXE.");
        std::error_code ec;
        Native::dataDirectory = std::filesystem::canonical(data, ec);
        if (ec)
            throw std::runtime_error("Cannot open game data folder: " + data.toString());
        Native::requireSeparateOutput(Native::dataDirectory, saves);
        std::filesystem::create_directories(saves);
        if (!diagnostics.start(saves / "logs", FTA2_VERSION))
            fprintf(stderr, "Warning: Cannot create session log in %s\n", saves.toString().c_str());
        Native::diagnosticMessage("STARTUP", "data=%s saves=%s",
                                  Native::dataDirectory.toString().c_str(),
                                  saves.toString().c_str());
        Native::diagnosticPhase("validate resources");
        Native::resourceDirectory = saves / "resources";

        ConfMan.setPath("path", Native::dataDirectory);
        if (!ConfMan.hasKey("subtitles"))
            ConfMan.setBool("subtitles", true);
        // Older unvoiced builds persisted a forced mute, rather than a user setting.
        ConfMan.setBool("speech_mute", false);
        if (!ConfMan.hasKey("widescreen"))
            ConfMan.setBool("widescreen", false);
        if (!ConfMan.hasKey("show_game_time"))
            ConfMan.setBool("show_game_time", false);
        for (auto k : {"music_volume", "sfx_volume", "speech_volume"})
            if (!ConfMan.hasKey(k))
                ConfMan.setInt(k, 192);
        try {
            Native::prepareDosResources();
            Native::prepareVoiceArchive(voice);
            if (voiceProbe && Native::voiceArchive.empty())
                throw std::runtime_error("--smoke-voices requires a verified voice archive");
        } catch (const std::exception& e) {
            throw std::runtime_error("Game data in " + Native::dataDirectory.toString() + ": " +
                                     e.what());
        }
        ConfMan.setPath("data_dir", Native::dataDirectory);
        ConfMan.flushToDisk();
        Native::diagnosticMessage(
            "STARTUP", "voice=%s widescreen=%d new_layout=%d",
            Native::voiceArchive.empty() ? "disabled" : Native::voiceArchive.toString().c_str(),
            ConfMan.getBool("widescreen"), ConfMan.getBool("alternate_sidebar"));
        Native::diagnosticPhase("initialize graphics and audio");
        Native::Platform platform;
        platform.saves.directory = saves;
        if (probe) {
            ConfMan.setBool("native_probe", true);
            ConfMan.setInt("random_seed", 42);
            Native::requireSeparateOutput(Native::dataDirectory, probeOutput);
            if (deathActor)
                Native::configureDeathProbe(probeOutput, deathActor);
            else if (voiceProbe)
                Native::configureVoiceProbe(probeOutput);
            else if (widescreenProbe)
                Native::configureWidescreenProbe(probeOutput);
            else
                Native::configureSmokeProbe(probeOutput, smokeFrames);
            platform.deterministic = true;
            platform.getMixer()->enableDeterministic();
        }
        Native::diagnosticPhase("initialize engine");
        Saga2::Saga2Engine engine;
        engine.run();
        ConfMan.flushToDisk();
        Native::diagnosticMessage("EXIT", "Game exited normally");
        Native::diagnosticPhase("engine shutdown");
        return 0;
    } catch (const std::exception& e) {
        if (diagnostics.path().empty()) {
            if (char* user = SDL_GetPrefPath("fta2-native", "fta2-native")) {
                diagnostics.start(Native::Path(user) / "logs", FTA2_VERSION);
                SDL_free(user);
            }
        }
        Native::diagnosticFatal(e.what());
        std::string message = e.what();
        if (!diagnostics.path().empty())
            message += "\n\nDiagnostic log: " + Native::Path(diagnostics.path()).toString();
        fprintf(stderr, "fta2-native: %s\n", message.c_str());
#ifdef _WIN32
        if (interactive)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Faery Tale Adventure II", message.c_str(),
                                     nullptr);
#endif
        return 1;
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // A null argv asks SDL to convert the Windows Unicode command line to UTF-8.
    return SDL_RunApp(0, nullptr, runGame, nullptr);
#else
    return SDL_RunApp(argc, argv, runGame, nullptr);
#endif
}
