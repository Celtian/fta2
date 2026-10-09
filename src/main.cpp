// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/probe.h"
#include "platform/resources.h"
#include "platform/runtime.h"
#include "saga2/saga2.h"
int main(int argc, char** argv) {
    try {
        Native::Path data = "original-game", saves;
        bool probe = false;
        int smokeFrames = 260;
        Native::Path probeOutput;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                puts("fta2-native [--data-dir PATH] [--save-dir PATH]\nNative Faery Tale Adventure "
                     "II; original DOS game data required.\n--help       Show help\n--version    "
                     "Show version");
                return 0;
            }
            if (arg == "--version") {
                puts("fta2-native 0.1.0 (SAGA2 1a3e816a; GPL-3.0-or-later)");
                return 0;
            }
            if (arg == "--smoke-test" && i + 1 < argc) {
                probe = true;
                probeOutput = argv[++i];
                continue;
            }
            if (arg == "--smoke-frames" && i + 1 < argc) {
                smokeFrames = std::stoi(argv[++i]);
                if (smokeFrames < 260 || smokeFrames > 1000000)
                    throw std::runtime_error("Smoke frames must be 260..1000000");
                continue;
            }
            if ((arg == "--data-dir" || arg == "--save-dir") && i + 1 < argc) {
                auto value = Native::Path(argv[++i]);
                if (arg == "--data-dir")
                    data = value;
                else
                    saves = value;
                continue;
            }
            throw std::runtime_error("Unknown or incomplete option: " + arg);
        }

        if (saves.empty()) {
            char* p = SDL_GetPrefPath("fta2-native", "fta2-native");
            if (!p)
                throw std::runtime_error(SDL_GetError());
            saves = p;
            SDL_free(p);
        }
        Native::dataDirectory = std::filesystem::canonical(data);
        saves = std::filesystem::absolute(saves).lexically_normal();
        Native::requireSeparateOutput(Native::dataDirectory, saves);
        std::filesystem::create_directories(saves);
        Native::resourceDirectory = saves / "resources";

        ConfMan.file = saves / "config.ini";
        ConfMan.load();
        ConfMan.setPath("path", data);
        ConfMan.setBool("subtitles", true);
        ConfMan.setBool("speech_mute", true);
        for (auto k : {"music_volume", "sfx_volume", "speech_volume"})
            if (!ConfMan.hasKey(k))
                ConfMan.setInt(k, 192);
        Native::prepareDosResources();
        Native::Platform platform;
        platform.saves.directory = saves;
        if (probe) {
            ConfMan.setBool("native_probe", true);
            ConfMan.setInt("random_seed", 42);
            Native::requireSeparateOutput(Native::dataDirectory, probeOutput);
            Native::configureSmokeProbe(probeOutput, smokeFrames);
            platform.deterministic = true;
            platform.getMixer()->enableDeterministic();
        }
        Saga2::Saga2Engine engine;
        engine.run();
        ConfMan.flushToDisk();
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "fta2-native: %s\n", e.what());
        return 1;
    }
}
