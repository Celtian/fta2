// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/resources.h"
#include "platform/startup.h"
#include "saga2/tcoords.h"
using namespace Native;
static void check(bool condition, const char* what) {
    if (!condition)
        throw std::runtime_error(what);
}
static void mustFail(const std::function<void()>& fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    check(failed, "Malformed input was accepted");
}
int main() {
    try {
        check(sha256(nullptr, 0) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA256 empty");
        check(sha256(reinterpret_cast<const byte*>("abc"), 3) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 abc");
        std::vector<byte> million(1000000, 'a');
        check(sha256(million.data(), million.size()) ==
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "SHA256 long blocks");
        MemoryWriteStreamDynamic out;
        out.writeUint16LE(0xFEDC);
        out.writeSint32BE(-123456);
        out.writeSByte(-7);
        MemoryReadStream in(out.getData(), out.size());
        check(in.readUint16LE() == 0xFEDC && in.readSint32BE() == -123456 && in.readSByte() == -7,
              "Stream endian round trip");
        check(!in.eos() && in.pos() == 7, "EOF must be set by over-read");
        check(in.readByte() == 0 && in.eos(), "EOF over-read");
        check(!in.seek(-1), "Negative seek accepted");
        check(in.seek(-1, SEEK_END) && in.readSByte() == -7, "Seek end");
        std::vector<byte> hrs(39);
        memcpy(hrs.data(), "HRES", 4);
        WRITE_LE_UINT32(hrs.data() + 4, 12);
        WRITE_LE_UINT32(hrs.data() + 8, 12);
        WRITE_LE_UINT32(hrs.data() + 16, 24);
        WRITE_LE_UINT32(hrs.data() + 20, 12);
        WRITE_LE_UINT32(hrs.data() + 28, 36);
        WRITE_LE_UINT32(hrs.data() + 32, 3);
        validateHrs(hrs.data(), hrs.size());
        mustFail([&] { validateHrs(hrs.data(), 38); });
        WRITE_LE_UINT32(hrs.data() + 4, 0xFFFFFFFC);
        mustFail([&] { validateHrs(hrs.data(), hrs.size()); });
        auto root =
            std::filesystem::temp_directory_path() /
            String::format("fta2-test-%llu", static_cast<unsigned long long>(SDL_GetTicksNS()))
                .c_str();
        std::filesystem::create_directories(root / "data");
        std::filesystem::create_directories(root / "package/bin");
        std::filesystem::create_directories(root / "package/original-game");
        check(discoverGameDirectory({}, root / "data", root / "package/bin").empty(),
              "Missing installation accepted by discovery");
        std::ofstream(root / "package/original-game/FTA2.EXE").put('x');
        check(discoverGameDirectory({}, root / "data", root / "package/bin") ==
                  root / "package/original-game",
              "Package-relative game discovery failed");
        std::ofstream(root / "data/FTA2.EXE").put('x');
        check(discoverGameDirectory(root / "data", root, root / "package/bin") == root / "data",
              "Remembered installation was not preferred");
        mustFail([&] { requireSeparateOutput(Path(root / "data"), Path(root / "data/cache")); });
        requireSeparateOutput(Path(root / "data"), Path(root / "saves"));
        Platform platform;
        platform.saves.directory = root / u8"saves-\u017e-\u4e16";
        std::filesystem::create_directories(platform.saves.directory);
        check(platform.saves.openForSaving("../data/999.SAV") == nullptr,
              "Save traversal accepted");
        std::unique_ptr<OutSaveFile> save(platform.saves.openForSaving("fta2.001"));
        save->write(out.getData(), out.size());
        save->finalize();
        std::unique_ptr<InSaveFile> restored(platform.saves.openForLoading("fta2.001"));
        check(restored && restored->readUint16LE() == 0xFEDC, "Save directory round trip");
        platform.initGraphics(640, 480);
        auto point = platform.windowToGame(320, 240);
        check(point.x == 320 && point.y == 240, "Mouse 1:1 mapping");
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        check(count > 0, "Window handle");
        auto window = windows[0];
        SDL_free(windows);
        SDL_SetWindowSize(window, 1280, 720);
        SDL_PumpEvents();
        point = platform.windowToGame(160, 0);
        check(point.x == 0 && point.y == 0, "Letterbox left edge");
        point = platform.windowToGame(1120, 720);
        check(point.x == 640 && point.y == 480, "Letterbox right edge");
        ConfMan.setInt("random_seed", 42);
        RandomSource rng("test");
        check(rng.getRandomNumber(UINT_MAX) == 1570459262u, "Reference random sequence");
        Audio::Mixer mixer;
        mixer.enableDeterministic();
        auto pcm = new Audio::AudioStream;
        pcm->rate = 48000;
        pcm->samples = {0.5f, -0.5f};
        Audio::SoundHandle sound;
        mixer.playStream(Audio::Mixer::kSFXSoundType, &sound,
                         Audio::makeLoopingAudioStream(pcm, 2));
        mixer.advance(3);
        check(mixer.isSoundHandleActive(sound), "Finite audio loop ended early");
        mixer.advance(2);
        check(!mixer.isSoundHandleActive(sound) && mixer.sampleCounts().second == 4,
              "Finite audio loop did not stop after two repetitions");
        std::filesystem::remove_all(root);
        puts("Native runtime checks passed");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "runtime-test: %s\n", e.what());
        return 1;
    }
}
