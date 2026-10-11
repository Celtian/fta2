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
#include <span>

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
using FontKey = std::pair<Saga2::gFont*, Language>;
std::map<FontKey, std::unique_ptr<FontCopy>> fonts;

const CatalogEntry* find(const String& key, Language value) {
    const String qualified = String(languageCode(value)) + ":" + key;
    auto it = std::lower_bound(
        std::begin(catalog), std::end(catalog), qualified,
        [](const CatalogEntry& entry, const String& value) { return entry.key < value; });
    return it != std::end(catalog) && it->key == qualified ? it : nullptr;
}

void missing(const String& key, Language value) {
    const String qualified = String(languageCode(value)) + ":" + key;
    if (reported.size() < 4096 && reported.insert(qualified).second)
        warning("Missing or stale %s translation: %s", languageCode(value), key.c_str());
}

// Only the accent recipes are source assets. Base glyphs come from the user's EXE.
void extendFont(FontCopy& copy, const Saga2::gFont& original, Language value) {
    struct Accent {
        byte target;
        char base;
        char mark;
    };
    static constexpr Accent czechAccents[] = {
        {0xc1, 'A', 'a'},  {0xe1, 'a', 'a'},  {0xc8, 'C', 'v'}, {0xe8, 'c', 'v'}, {0xcf, 'D', 'v'},
        {0xef, 'd', '\''}, {0xc9, 'E', 'a'},  {0xe9, 'e', 'a'}, {0xcc, 'E', 'v'}, {0xec, 'e', 'v'},
        {0xcd, 'I', 'a'},  {0xed, 'i', 'a'},  {0xd2, 'N', 'v'}, {0xf2, 'n', 'v'}, {0xd3, 'O', 'a'},
        {0xf3, 'o', 'a'},  {0xd8, 'R', 'v'},  {0xf8, 'r', 'v'}, {0x8a, 'S', 'v'}, {0x9a, 's', 'v'},
        {0x8d, 'T', 'v'},  {0x9d, 't', '\''}, {0xda, 'U', 'a'}, {0xfa, 'u', 'a'}, {0xd9, 'U', 'o'},
        {0xf9, 'u', 'o'},  {0xdd, 'Y', 'a'},  {0xfd, 'y', 'a'}, {0x8e, 'Z', 'v'}, {0x9e, 'z', 'v'}};
    static constexpr Accent westernAccents[] = {
        {0xc0, 'A', '`'}, {0xe0, 'a', '`'}, {0xc1, 'A', 'a'}, {0xe1, 'a', 'a'}, {0xc2, 'A', '^'},
        {0xe2, 'a', '^'}, {0xc4, 'A', ':'}, {0xe4, 'a', ':'}, {0xc7, 'C', ','}, {0xe7, 'c', ','},
        {0xc8, 'E', '`'}, {0xe8, 'e', '`'}, {0xc9, 'E', 'a'}, {0xe9, 'e', 'a'}, {0xca, 'E', '^'},
        {0xea, 'e', '^'}, {0xcb, 'E', ':'}, {0xeb, 'e', ':'}, {0xcc, 'I', '`'}, {0xec, 'i', '`'},
        {0xcd, 'I', 'a'}, {0xed, 'i', 'a'}, {0xce, 'I', '^'}, {0xee, 'i', '^'}, {0xcf, 'I', ':'},
        {0xef, 'i', ':'}, {0xd1, 'N', '~'}, {0xf1, 'n', '~'}, {0xd2, 'O', '`'}, {0xf2, 'o', '`'},
        {0xd3, 'O', 'a'}, {0xf3, 'o', 'a'}, {0xd4, 'O', '^'}, {0xf4, 'o', '^'}, {0xd6, 'O', ':'},
        {0xf6, 'o', ':'}, {0xd9, 'U', '`'}, {0xf9, 'u', '`'}, {0xda, 'U', 'a'}, {0xfa, 'u', 'a'},
        {0xdb, 'U', '^'}, {0xfb, 'u', '^'}, {0xdc, 'U', ':'}, {0xfc, 'u', ':'}, {0xff, 'y', ':'},
        {0x9f, 'Y', ':'}, {0xa1, '!', '!'}, {0xbf, '?', '!'}};
    const std::span<const Accent> accents = value == Language::Czech
                                                ? std::span<const Accent>(czechAccents)
                                                : std::span<const Accent>(westernAccents);
    copy.font = original;
    int extra = 0;
    for (auto a : accents)
        extra += (original.charWidth[byte(a.base)] + 7) / 8;
    // Reserve four paired Latin ligatures in addition to the accent recipes.
    for (const auto& pair : {std::pair{'O', 'E'}, {'o', 'e'}, {'A', 'E'}, {'a', 'e'}})
        extra +=
            (original.charWidth[byte(pair.first)] + original.charWidth[byte(pair.second)] + 7) / 8;
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
        const int newBottom =
            a.mark == ',' ? std::max(top, std::min(bottom, original.height - 3)) : bottom;
        const int newTop =
            a.mark == ',' || a.mark == '!' ? top : std::min(newBottom, std::max(top, 3));
        for (int y = top; y <= bottom; ++y)
            for (int x = 0; x < width; ++x)
                if (sourcePixel(x, y))
                    pixel(a.mark == '!' ? width - 1 - x : x,
                          a.mark == '!' ? top + bottom - y
                          : bottom == top
                              ? newBottom
                              : newTop + (y - top) * (newBottom - newTop) / (bottom - top));
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
        if (a.mark == '`') {
            pixel(mid - 1, y);
            pixel(mid, y + 1);
        }
        if (a.mark == '^') {
            pixel(mid, y);
            pixel(mid - 1, y + 1);
            pixel(mid + 1, y + 1);
        }
        if (a.mark == ':') {
            pixel(mid - 1, y);
            pixel(mid + 1, y);
        }
        if (a.mark == '~') {
            pixel(mid - 2, y + 1);
            pixel(mid - 1, y);
            pixel(mid, y + 1);
            pixel(mid + 1, y);
        }
        if (a.mark == ',') {
            pixel(mid, newBottom + 1);
            pixel(mid - 1, newBottom + 2);
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
    if (value != Language::Czech) {
        struct Ligature {
            byte target;
            char left, right;
        };
        for (auto a :
             {Ligature{0x8c, 'O', 'E'}, {0x9c, 'o', 'e'}, {0xc6, 'A', 'E'}, {0xe6, 'a', 'e'}}) {
            const int leftWidth = original.charWidth[byte(a.left)];
            const int width = leftWidth + original.charWidth[byte(a.right)];
            for (int y = 0; y < original.height; ++y)
                for (int x = 0; x < width; ++x) {
                    const byte base = byte(x < leftWidth ? a.left : a.right);
                    const int sx = x < leftWidth ? x : x - leftWidth;
                    if (original
                            .fontdata[y * original.rowMod + original.charXOffset[base] + sx / 8] &
                        (0x80 >> (sx % 8)))
                        copy.pixels[y * stride + offset + x / 8] |= 0x80 >> (x % 8);
                }
            copy.font.charXOffset[a.target] = offset;
            copy.font.charWidth[a.target] = width;
            copy.font.charKern[a.target] = original.charKern[byte(a.left)];
            copy.font.charSpace[a.target] =
                original.charSpace[byte(a.left)] + original.charSpace[byte(a.right)];
            offset += (width + 7) / 8;
        }
    }
    copy.font.rowMod = stride;
    copy.font.fontdata = copy.pixels.data();
}
} // namespace

const char* languageCode(Language value) {
    switch (value) {
    case Language::Czech:
        return "cs";
    case Language::Spanish:
        return "es";
    case Language::French:
        return "fr";
    default:
        return "en";
    }
}
bool validLanguage(uint8 value) { return value <= static_cast<uint8>(Language::French); }
Language language() { return current; }
Language requestedLanguage() { return pending.value_or(current); }
void initializeLanguage() {
    const String setting = ConfMan.getPath("language").toString();
    current = Language::English;
    for (auto value : {Language::Czech, Language::Spanish, Language::French})
        if (setting == languageCode(value))
            current = value;
    if (!setting.empty() && setting != languageCode(current))
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
    ConfMan.setPath("language", Path(languageCode(value)));
    if (value != Language::English)
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
    const auto* entry = find(key, value);
    if (entry &&
        sha256(reinterpret_cast<const byte*>(original), std::strlen(original)) == entry->hash)
        return entry->text ? entry->text : original;
    missing(key, value);
    return original;
}
const char* ui(const char* original, bool reportMissing) {
    if (!original || current == Language::English)
        return original;
    const String hash = sha256(reinterpret_cast<const byte*>(original), std::strlen(original));
    const auto* entry = find("ui:" + hash, current);
    if (!entry && reportMissing && *original)
        missing("ui:" + hash, current);
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
    if (!original)
        return original;
    for (const auto& [key, copy] : fonts)
        if (&copy->font == original)
            return original; // Explicitly bound fonts include radio labels and captured speech.
    if (value == Language::English || !original->fontdata)
        return original;
    auto& copy = fonts[{original, value}];
    if (!copy) {
        copy = std::make_unique<FontCopy>();
        extendFont(*copy, *original, value);
    }
    return &copy->font;
}
void releaseLocalizedFonts() { fonts.clear(); }
int czechPlural(unsigned count) { return count == 1 ? 0 : count >= 2 && count <= 4 ? 1 : 2; }
String countedName(uint16 index, const char* original, unsigned count) {
    const int form = current == Language::Czech    ? czechPlural(count)
                     : current == Language::French ? (count <= 1 ? 0 : 1)
                                                   : (count == 1 ? 0 : 1);
    const String key = String::format("ui:plural.%u.%d", index, form);
    const auto* entry = find(key, current);
    if (current != Language::English && entry &&
        sha256(reinterpret_cast<const byte*>(original), std::strlen(original)) == entry->hash)
        return String::format("%u %s", count, entry->text);
    if (current != Language::English) {
        missing(key, current);
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
