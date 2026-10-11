// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "platform/runtime.h"
#include <optional>
#include <vector>

namespace Saga2 {
struct gFont;
}

namespace Native {
enum class Language : uint8 { English, Czech };
// Owned presentation inputs can be rendered again after the language changes.
struct LocalizedMessage {
    String key, original;
    std::optional<int> integer;
    std::vector<LocalizedMessage> arguments;
    bool formatted = false;
    bool englishOnly = false;
    static LocalizedMessage text(String key, String original);
    static LocalizedMessage ui(const char* original);
    static LocalizedMessage name(uint16 index, const char* original);
    static LocalizedMessage number(int value);
    String render(Language language) const;
};
Language language();
Language requestedLanguage();
void initializeLanguage();
void requestLanguage(Language value);
bool applyPendingLanguage();
const char* ui(const char* original, bool reportMissing = false);
const char* localizedName(uint16 index, const char* original);
const char* localizedScript(uint16 segment, uint16 table, uint16 index, const char* original,
                            Language value);
const char* translateKey(const String& key, const char* original, Language value);
Saga2::gFont* localizedFont(Saga2::gFont* original, Language value);
void releaseLocalizedFonts();
int czechPlural(unsigned count);
String countedName(uint16 index, const char* original, unsigned count);
// Checked against the same pixel metrics used by the original renderer.
void checkTextFit(const char* text, Saga2::gFont* font, int width, int height, int styles = 0,
                  const char* context = "UI");
void reportTextOverflow(const char* context, int width, int height, int availableWidth,
                        int availableHeight);
unsigned textOverflowCount();
void resetTextOverflowCount();
} // namespace Native
