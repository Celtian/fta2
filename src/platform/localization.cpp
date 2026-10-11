// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/localization.h"
#include "platform/resources.h"
#include "saga2/gdraw.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace Native {
namespace {
struct CatalogEntry {
    const char* key;
    const char* hash;
    const char* text;
};
#include "localization-data.inc"
Language current = Language::English;
std::optional<Language> pending;
std::set<String> reported;
unsigned overflows = 0;

struct FontCopy {
    Saga2::gFont font;
    std::vector<byte> pixels;
};
std::map<Saga2::gFont*, std::unique_ptr<FontCopy>> fonts;

const CatalogEntry* find(const String& key) {
    auto it = std::lower_bound(
        std::begin(catalog), std::end(catalog), key,
        [](const CatalogEntry& entry, const String& value) { return entry.key < value; });
    return it != std::end(catalog) && it->key == key ? it : nullptr;
}

void missing(const String& key) {
    if (reported.size() < 4096 && reported.insert(key).second)
        warning("Missing or stale Czech translation: %s", key.c_str());
}

// Only the accent recipes are source assets. Base glyphs come from the user's EXE.
void extendFont(FontCopy& copy, const Saga2::gFont& original) {
    struct Accent {
        byte target;
        char base;
        char mark;
    };
    static constexpr Accent accents[] = {
        {0xc1, 'A', 'a'},  {0xe1, 'a', 'a'},  {0xc8, 'C', 'v'}, {0xe8, 'c', 'v'}, {0xcf, 'D', 'v'},
        {0xef, 'd', '\''}, {0xc9, 'E', 'a'},  {0xe9, 'e', 'a'}, {0xcc, 'E', 'v'}, {0xec, 'e', 'v'},
        {0xcd, 'I', 'a'},  {0xed, 'i', 'a'},  {0xd2, 'N', 'v'}, {0xf2, 'n', 'v'}, {0xd3, 'O', 'a'},
        {0xf3, 'o', 'a'},  {0xd8, 'R', 'v'},  {0xf8, 'r', 'v'}, {0x8a, 'S', 'v'}, {0x9a, 's', 'v'},
        {0x8d, 'T', 'v'},  {0x9d, 't', '\''}, {0xda, 'U', 'a'}, {0xfa, 'u', 'a'}, {0xd9, 'U', 'o'},
        {0xf9, 'u', 'o'},  {0xdd, 'Y', 'a'},  {0xfd, 'y', 'a'}, {0x8e, 'Z', 'v'}, {0x9e, 'z', 'v'}};
    copy.font = original;
    int extra = 0;
    for (auto a : accents)
        extra += (original.charWidth[byte(a.base)] + 7) / 8;
    const int stride = original.rowMod + extra;
    copy.pixels.assign(stride * original.height, 0);
    for (int y = 0; y < original.height; ++y)
        std::memcpy(copy.pixels.data() + y * stride, original.fontdata + y * original.rowMod,
                    original.rowMod);
    int offset = original.rowMod;
    for (auto a : accents) {
        const byte base = byte(a.base);
        const int width = original.charWidth[base];
        if (width <= 0)
            continue;
        auto sourcePixel = [&](int x, int y) {
            return (original.fontdata[y * original.rowMod + original.charXOffset[base] + x / 8] &
                    (0x80 >> (x % 8))) != 0;
        };
        auto pixel = [&](int x, int y) {
            if (x >= 0 && x < width && y >= 0 && y < original.height)
                copy.pixels[y * stride + offset + x / 8] |= 0x80 >> (x % 8);
        };
        int top = original.height, bottom = 0;
        for (int y = 0; y < original.height; ++y)
            for (int x = 0; x < width; ++x)
                if (sourcePixel(x, y)) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        if (a.base == 'i') {
            // Remove the original dot before drawing an acute accent.
            bool gap = false;
            for (int y = top; y <= bottom; ++y) {
                bool ink = false;
                for (int x = 0; x < width; ++x)
                    ink |= sourcePixel(x, y);
                if (!ink)
                    gap = true;
                else if (gap) {
                    top = y;
                    break;
                }
            }
        }
        // Reserve accent rows inside the cell while retaining the original bottom/baseline.
        const int newTop = std::min(bottom, std::max(top, 3));
        for (int y = top; y <= bottom; ++y)
            for (int x = 0; x < width; ++x)
                if (sourcePixel(x, y))
                    pixel(x, bottom == top
                                 ? bottom
                                 : newTop + (y - top) * (bottom - newTop) / (bottom - top));
        const int mid = width / 2;
        const int y = std::max(0, newTop - 3);
        if (a.mark == 'a') {
            pixel(mid + 1, y);
            pixel(mid, y + 1);
        }
        if (a.mark == '\'') {
            pixel(width - 1, y);
            pixel(width - 1, y + 1);
            pixel(width - 2, y + 2);
        }
        if (a.mark == 'v') {
            pixel(mid - 1, y);
            pixel(mid + 1, y);
            pixel(mid, y + 1);
        }
        if (a.mark == 'o') {
            pixel(mid, y);
            pixel(mid - 1, y + 1);
            pixel(mid + 1, y + 1);
            pixel(mid, y + 2);
        }
        copy.font.charXOffset[a.target] = offset;
        copy.font.charWidth[a.target] = width;
        copy.font.charKern[a.target] = original.charKern[base];
        copy.font.charSpace[a.target] = original.charSpace[base];
        offset += (width + 7) / 8;
    }
    copy.font.rowMod = stride;
    copy.font.fontdata = copy.pixels.data();
}
} // namespace

Language language() { return current; }
Language requestedLanguage() { return pending.value_or(current); }
void initializeLanguage() {
    const String setting = ConfMan.getPath("language").toString();
    current = setting == "cs" ? Language::Czech : Language::English;
    if (!setting.empty() && setting != "cs" && setting != "en")
        warning("Unknown configured language; using English");
    pending.reset();
    reported.clear();
    overflows = 0;
}
void requestLanguage(Language value) { pending = value; }
bool applyPendingLanguage() {
    if (!pending)
        return false;
    const Language value = *pending;
    pending.reset();
    if (value == current)
        return false;
    current = value;
    ConfMan.setPath("language", Path(value == Language::Czech ? "cs" : "en"));
    if (value == Language::Czech)
        ConfMan.setBool("subtitles", true);
    ConfMan.flushToDisk();
    return true;
}
LocalizedMessage LocalizedMessage::text(String key, String original) {
    LocalizedMessage result;
    result.key = std::move(key);
    result.original = std::move(original);
    return result;
}
LocalizedMessage LocalizedMessage::ui(const char* original) {
    return text("ui:" + sha256(reinterpret_cast<const byte*>(original), std::strlen(original)),
                original);
}
LocalizedMessage LocalizedMessage::name(uint16 index, const char* original) {
    return text(String::format("name:%u", index), original);
}
LocalizedMessage LocalizedMessage::number(int value) {
    auto result = text({}, {});
    result.integer = value;
    return result;
}
String LocalizedMessage::render(Language value) const {
    if (englishOnly && value != Language::English)
        return {};
    if (integer)
        return String::format("%d", *integer);
    const char* format =
        key.empty() ? original.c_str() : translateKey(key, original.c_str(), value);
    if (!formatted)
        return format;
    String result;
    size_t argument = 0;
    for (size_t position = 0; format[position]; ++position) {
        if (format[position] != '%' || !format[position + 1]) {
            result += format[position];
            continue;
        }
        const char type = format[++position];
        if (type == '%') {
            result += '%';
        } else if (std::strchr("sdxnu", type) && argument < arguments.size()) {
            const auto& input = arguments[argument++];
            if (type == 'x' && input.integer)
                result += String::format("%x", static_cast<unsigned>(*input.integer));
            else if (type == 'u' && input.integer)
                result += String::format("%u", static_cast<unsigned>(*input.integer));
            else
                result += input.render(value);
        } else {
            // SAGA stringf writes the character following an unknown percent escape.
            result += type;
        }
    }
    return result;
}
const char* translateKey(const String& key, const char* original, Language value) {
    if (!original || value == Language::English)
        return original;
    const auto* entry = find(key);
    if (entry &&
        sha256(reinterpret_cast<const byte*>(original), std::strlen(original)) == entry->hash)
        return entry->text ? entry->text : original;
    missing(key);
    return original;
}
const char* ui(const char* original, bool reportMissing) {
    if (!original || current == Language::English)
        return original;
    const String hash = sha256(reinterpret_cast<const byte*>(original), std::strlen(original));
    const auto* entry = find("ui:" + hash);
    if (!entry && reportMissing && *original)
        missing("ui:" + hash);
    // Dynamic/previously translated text is allowed through unchanged.
    return entry && entry->text ? entry->text : original;
}
const char* localizedName(uint16 index, const char* original) {
    return translateKey(String::format("name:%u", index), original, current);
}
const char* localizedScript(uint16 segment, uint16 table, uint16 index, const char* original,
                            Language value) {
    return translateKey(String::format("script:%u:%u:%u", segment, table, index), original, value);
}
Saga2::gFont* localizedFont(Saga2::gFont* original, Language value) {
    if (!original || value == Language::English || !original->fontdata)
        return original;
    for (const auto& [base, copy] : fonts)
        if (&copy->font == original)
            return original;
    auto& copy = fonts[original];
    if (!copy) {
        copy = std::make_unique<FontCopy>();
        extendFont(*copy, *original);
    }
    return &copy->font;
}
void releaseLocalizedFonts() { fonts.clear(); }
int czechPlural(unsigned count) { return count == 1 ? 0 : count >= 2 && count <= 4 ? 1 : 2; }
String countedName(uint16 index, const char* original, unsigned count) {
    const String key = String::format("ui:plural.%u.%d", index, czechPlural(count));
    const auto* entry = find(key);
    if (current == Language::Czech && entry &&
        sha256(reinterpret_cast<const byte*>(original), std::strlen(original)) == entry->hash)
        return String::format("%u %s", count, entry->text);
    if (current == Language::Czech) {
        missing(key);
        return String::format("%u x %s", count, localizedName(index, original));
    }
    return String::format("%u %ss", count, original);
}
void checkTextFit(const char* text, Saga2::gFont* font, int width, int height, int styles,
                  const char* context) {
    if (!text || current == Language::English || !font)
        return;
    font = localizedFont(font, current);
    int top = font->height, bottom = -1;
    for (const auto* p = reinterpret_cast<const byte*>(text); *p; ++p)
        for (int y = 0; y < font->height; ++y)
            for (int x = 0; x < font->charWidth[*p]; ++x)
                if (font->fontdata[y * font->rowMod + font->charXOffset[*p] + x / 8] &
                    (0x80 >> (x % 8))) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
    int inkHeight = bottom < top ? 0 : bottom - top + 1;
    if (styles & Saga2::kTextStyleOutline)
        inkHeight += 2;
    else if (styles & Saga2::kTextStyleThickOutline)
        inkHeight += 4;
    if (styles & Saga2::kTextStyleShadow)
        ++inkHeight;
    const int textWidth = Saga2::TextWidth(font, text, -1, styles);
    if (textWidth > width || inkHeight > height)
        reportTextOverflow(context, textWidth, inkHeight, width, height);
}
void reportTextOverflow(const char* context, int width, int height, int availableWidth,
                        int availableHeight) {
    ++overflows;
    warning("Localized text exceeds %s bounds: %d x %d needs %d x %d", context, availableWidth,
            availableHeight, width, height);
}
unsigned textOverflowCount() { return overflows; }
void resetTextOverflowCount() { overflows = 0; }
} // namespace Native
