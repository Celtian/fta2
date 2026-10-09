// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/probe.h"
#include "platform/resources.h"
#include "saga2/fta.h"
#include "saga2/saga2.h"
#include "saga2/actor.h"
#include "saga2/annoy.h"
#include "saga2/audio.h"
#include "saga2/music.h"
#include "saga2/panel.h"
#include "saga2/player.h"
#include "saga2/saveload.h"
namespace Native {
std::function<void()> gameFrameHook;
void configureSmokeProbe(const Path& output, int frames) {
    std::filesystem::create_directories(output);
    struct State {
        int frame = 0;
        bool active = false;
        Saga2::TilePoint initial, saved;
        uint16 selected = 0;
    };
    auto state = std::make_shared<State>();
    gameFrameHook = [output, state, frames] {
        if (state->active)
            return;
        state->active = true;
        struct Guard {
            bool& flag;
            ~Guard() { flag = false; }
        } guard{state->active};
        auto& s = *state;
        auto vm = Saga2::g_vm;
        ++s.frame;
        auto keypad = [](SDL_Scancode scancode, SDL_Keycode key) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = scancode;
            ev.key.key = key;
            ev.key.down = true;
            if (!SDL_PushEvent(&ev))
                throw std::runtime_error("Smoke probe: cannot inject keypad input");
        };
        auto record = [&](const char* phase) {
            auto pos = Saga2::centerActorCoords();
            std::ofstream log(output / "state.jsonl", std::ios::app);
            log << "{\"phase\":\"" << phase << "\",\"frame\":" << s.frame
                << ",\"actor\":" << Saga2::getCenterActorID() << ",\"u\":" << pos.u
                << ",\"v\":" << pos.v << ",\"z\":" << pos.z << ",\"rng\":" << vm->_rnd->getSeed()
                << "}\n";
        };
        if (s.frame == 1) {
            s.initial = Saga2::centerActorCoords();
            record("initial");
            g_platform->capture(output / "initial.ppm");
        }
        if (s.frame == 20 || s.frame == 21 || s.frame == 100) {
            Event ev;
            ev.mouse = {380, 330};
            ev.type = s.frame == 20   ? EVENT_MOUSEMOVE
                      : s.frame == 21 ? EVENT_LBUTTONDOWN
                                      : EVENT_LBUTTONUP;
            vm->_toolBase->handleMouse(ev, g_platform->getMillis());
        }
        if (s.frame == 180) {
            record("moved");
            auto pos = Saga2::centerActorCoords();
            if (pos.u == s.initial.u && pos.v == s.initial.v)
                throw std::runtime_error("Smoke probe: movement did not change actor position");
        }
        if (s.frame == 181) {
            Saga2::playSound(MKTAG('C', 'L', 'K', 1));
            keypad(SDL_SCANCODE_KP_2, SDLK_KP_2);
        }
        if (s.frame == 190) {
            record("switched");
            if (Saga2::getCenterActorID() != 32769)
                throw std::runtime_error("Smoke probe: keypad 2 did not select Phillip");
            g_platform->capture(output / "controlled.ppm");
        }
        if (s.frame == 220) {
            s.saved = Saga2::centerActorCoords();
            s.selected = Saga2::getCenterActorID();
            if (vm->saveGameState(1, "Native round trip", false) != kNoError)
                throw std::runtime_error("Smoke probe: save failed");
            record("saved");
        }
        if (s.frame == 221) {
            keypad(SDL_SCANCODE_KP_3, SDLK_KP_3);
        }
        if (s.frame == 222 && Saga2::getCenterActorID() != 32770)
            throw std::runtime_error("Smoke probe: keypad 3 did not select Kevin");
        if (s.frame == 230) {
            vm->loadGameState(1);
            auto pos = Saga2::centerActorCoords();
            if (Saga2::getCenterActorID() != s.selected || pos.u != s.saved.u ||
                pos.v != s.saved.v || pos.z != s.saved.z)
                throw std::runtime_error("Smoke probe: restore changed actor state");
            record("restored");
        }
        if (s.frame == 235)
            keypad(SDL_SCANCODE_KP_1, SDLK_KP_1);
        if (s.frame == 236 && Saga2::getCenterActorID() != 32768)
            throw std::runtime_error("Smoke probe: keypad 1 did not select Julian");
        if (s.frame == 240) {
            std::unique_ptr<InSaveFile> original(
                vm->getSaveFileManager()->openForLoading("fta2.002"));
            if (original) {
                vm->loadGameState(2);
                record("original-save-loaded");
            }
        }
        if (s.frame == frames) {
            auto [music, effects] = g_platform->getMixer()->sampleCounts();
            std::ofstream audio(output / "audio.json");
            audio << "{\"music_nonzero_samples\":" << music
                  << ",\"effects_nonzero_samples\":" << effects << "}\n";
            if (!music || !effects)
                throw std::runtime_error("Smoke probe: silent music/effects");
            record("finished");
            g_platform->capture(output / "final.ppm");
            g_platform->quitting = true;
        }
    };
}
} // namespace Native
