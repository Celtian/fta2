// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/localization.h"
#include "platform/resources.h"
#include "platform/saves.h"
#include "saga2/gdraw.h"
#include <iostream>

using namespace Native;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

static std::vector<byte> speechSave(const String& display, unsigned ordinal = 0,
                                    bool validHash = true, bool localization = true) {
    MemoryWriteStreamDynamic out;
    byte header[128]{};
    memcpy(header, "FTA2", 4);
    out.write(header, sizeof(header));
    constexpr const char* tags[] = {"GLOB", "TIME", "CALE", "WRLD", "ACTR", "OBJS", "BAND", "PLYR",
                                    "CNTR", "TAGS", "CYCL", "SDTA", "SAGA", "MOTN", "TSTK", "TASK",
                                    "TACT", "AREG", "TIMR", "SENS", "ACNT", "MISS", "FACT", "TMST",
                                    "SPEL", "AMAP", "UIST", "PALE", "CONT"};
    for (auto* tag : tags) {
        out.write(tag, 4);
        out.writeUint32LE(0);
    }
    const String original = "Synthetic: @Yes@No";
    out.write("SPCH", 4);
    out.writeUint32LE(24 + original.size());
    out.writeUint16LE(1);
    out.writeUint16LE(0);
    out.writeUint16LE(original.size());
    byte fields[18]{};
    out.write(fields, sizeof(fields));
    out.write(original.data(), original.size());
    if (localization) {
        out.write("NLOC", 4);
        out.writeUint32LE(74 + display.size());
        out.writeUint16LE(1);
        out.writeUint16LE(1);
        out.writeUint16LE(ordinal);
        out.writeByte(1);
        out.writeByte(0);
        auto hash = sha256(reinterpret_cast<const byte*>(original.data()), original.size());
        if (!validHash)
            hash[0] = hash[0] == '0' ? '1' : '0';
        out.write(hash.data(), hash.size());
        out.writeUint16LE(display.size());
        out.write(display.data(), display.size());
    }
    return {out.getData(), out.getData() + out.size()};
}

int main() {
    const Path root =
        std::filesystem::temp_directory_path() /
        String::format("fta2-l10n-%llu", (unsigned long long)SDL_GetTicksNS()).c_str();
    try {
        std::filesystem::create_directories(root);
        ConfMan.file = root / "config.ini";
        ConfMan.setPath("language", Path("en"));
        ConfMan.setBool("subtitles", false);
        initializeLanguage();
        check(String(ui("Options")) == "Options", "English default changed");
        auto queued = LocalizedMessage::ui("%s is hurt.");
        {
            String temporary = "Julian";
            queued.arguments.push_back(LocalizedMessage::name(545, temporary.c_str()));
        }
        queued.formatted = true;
        check(queued.render(Language::English) == "Julian is hurt.",
              "Captured status lost its inputs");
        check(queued.render(Language::Czech).find("Juli\341n") != String::npos &&
                  queued.render(Language::Czech) != queued.render(Language::English),
              "Captured status cannot be translated after enqueue");
        auto numeric = LocalizedMessage::text({}, "%d %x %% %s");
        numeric.formatted = true;
        numeric.arguments = {LocalizedMessage::number(-2), LocalizedMessage::number(31),
                             LocalizedMessage::text({}, "literal %d")};
        check(numeric.render(Language::English) == "-2 1f % literal %d",
              "Owned message formatting changed arguments or percent escapes");
        requestLanguage(Language::Czech);
        check(language() == Language::English && !ConfMan.getBool("subtitles"),
              "Language applied before modal closure");
        check(applyPendingLanguage() && language() == Language::Czech &&
                  ConfMan.getBool("subtitles"),
              "Czech switch must enable subtitles");
        check(String(ui("Options")) == "Nastaven\355", "Czech menu lookup failed");
        check(String(localizedName(545, "Julian")) == "Juli\341n" &&
                  String(localizedName(722, "Phillip")) == "Filip",
              "Brother names differ");
        check(String(localizedName(545, "synthetic changed source")) == "synthetic changed source",
              "Stale source fingerprint accepted");
        check(String(translateKey("name:65535", "synthetic fallback", Language::Czech)) ==
                  "synthetic fallback",
              "Missing translation must retain source");
        check(czechPlural(1) == 0 && czechPlural(2) == 1 && czechPlural(4) == 1 &&
                  czechPlural(0) == 2 && czechPlural(5) == 2 && czechPlural(21) == 2,
              "Czech grammatical numbers are incorrect");
        check(countedName(437, "Gold Coin", 2) == "2 zlat\351 mince" &&
                  countedName(437, "Gold Coin", 5) == "5 zlat\375ch minc\355",
              "Merged item names do not use Czech forms");
        ConfMan.setBool("subtitles", false);
        check(!applyPendingLanguage() && !ConfMan.getBool("subtitles"),
              "Normal updates overrode the player's subtitle preference");
        ConfMan.flushToDisk();
        ConfMan.load();
        initializeLanguage();
        check(language() == Language::Czech && !ConfMan.getBool("subtitles"),
              "Language/subtitle preferences did not survive reload");

        Saga2::gFont font{};
        font.height = 11;
        font.baseLine = 8;
        font.rowMod = 256;
        std::vector<byte> original(font.height * font.rowMod, 0);
        for (int c = 0; c < 256; ++c) {
            font.charXOffset[c] = c;
            font.charWidth[c] = 5;
            font.charSpace[c] = 6;
            for (int y = 3; y < 8; ++y)
                original[y * font.rowMod + c] = 0x70;
        }
        font.fontdata = original.data();
        const auto baseline = original;
        auto* cs = localizedFont(&font, Language::Czech);
        check(cs != &font && localizedFont(&font, Language::English) == &font &&
                  cs->height == font.height && cs->baseLine == font.baseLine &&
                  original == baseline,
              "Czech font modified the original font");
        for (byte c : {byte(0xe8), byte(0xf8), byte(0x9a), byte(0x9e), byte(0xec), byte(0xf9)}) {
            check(cs->charWidth[c] == 5 && cs->charSpace[c] == 6, "Accent changes glyph advance");
            check(cs->fontdata[cs->charXOffset[c]] != 0 ||
                      cs->fontdata[cs->rowMod + cs->charXOffset[c]] != 0,
                  "Czech accent is missing");
        }
        {
            Saga2::gPixelMap bitmap;
            bitmap._size = Saga2::Point16(100, 30);
            std::vector<byte> pixels(bitmap.bytes(), 0);
            bitmap._data = pixels.data();
            Saga2::gPort port;
            port.setMap(&bitmap);
            port.setFont(cs);
            port.setColor(10);
            port.setStyle(Saga2::kTextStyleThickOutline);
            port.moveTo(5, 5);
            port.drawText("\310e\232tina", -1);
            check(port._penPos.x == 47, "CP1250 text moved the pen by signed character metrics");
            check(std::count(pixels.begin(), pixels.end(), byte(10)) > 0,
                  "CP1250 text produced no glyph pixels");
        }
        for (bool localization : {false, true}) {
            auto bytes = speechSave("Zkou\232ka: @Ano@Ne", 0, true, localization);
            MemoryReadStream input(bytes.data(), bytes.size());
            validateSave(input);
        }
        for (auto bytes :
             {speechSave("Zkou\232ka: @Ano"), speechSave("Zkou\232ka: @Ano@Ne", 1),
              speechSave("Zkou\232ka: @Ano@Ne", 0, false), speechSave(String("bad\0text", 8))}) {
            MemoryReadStream input(bytes.data(), bytes.size());
            bool rejected = false;
            try {
                validateSave(input);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            check(rejected, "Invalid NLOC was accepted before game-state cleanup");
        }
        resetTextOverflowCount();
        checkTextFit("abc", cs, 30, 11);
        check(textOverflowCount() == 0, "Fitting text was rejected");
        checkTextFit("abc", cs, 5, 11);
        check(textOverflowCount() == 1, "Pixel overflow was not detected");
        releaseLocalizedFonts();
        requestLanguage(Language::English);
        check(applyPendingLanguage() && String(ui("Options")) == "Options",
              "Returning to English failed");
        std::filesystem::remove_all(root);
        std::cout << "Localization checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::filesystem::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
