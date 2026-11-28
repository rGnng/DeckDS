#include "music_identity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

extern "C" {
#include <libavutil/md5.h>
}

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#endif

namespace {
std::string normalized(std::string_view value, bool trim, bool lower)
{
    if (value.empty())
        return {};
#if defined(__APPLE__)
    const auto input = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(value.data()),
                                               value.size(), kCFStringEncodingUTF8, false);
    if (!input)
        throw std::runtime_error("A library name or path is not valid UTF-8.");
    const auto text = CFStringCreateMutableCopy(nullptr, 0, input);
    CFRelease(input);
    if (!text)
        throw std::bad_alloc();
    if (trim) {
        const auto space = CFCharacterSetGetPredefined(kCFCharacterSetWhitespaceAndNewline);
        CFIndex first = 0, last = CFStringGetLength(text);
        while (first < last && CFCharacterSetIsCharacterMember(space, CFStringGetCharacterAtIndex(text, first)))
            ++first;
        while (last > first && CFCharacterSetIsCharacterMember(space, CFStringGetCharacterAtIndex(text, last - 1)))
            --last;
        CFStringDelete(text, CFRangeMake(last, CFStringGetLength(text) - last));
        CFStringDelete(text, CFRangeMake(0, first));
    }
    CFStringNormalize(text, kCFStringNormalizationFormC);
    if (lower) {
        // Swift's String.lowercased() maps capital sigma without the final-sigma rule.
        CFStringFindAndReplace(text, CFSTR("Σ"), CFSTR("σ"), CFRangeMake(0, CFStringGetLength(text)), 0);
        CFStringLowercase(text, nullptr);
    }
    const auto length = CFStringGetLength(text);
    std::string result(CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8), '\0');
    CFIndex used = 0;
    const auto converted = CFStringGetBytes(text, CFRangeMake(0, length), kCFStringEncodingUTF8, 0, false,
                                            reinterpret_cast<UInt8*>(result.data()), result.size(), &used);
    CFRelease(text);
    if (converted != length)
        throw std::runtime_error("A library name or path could not be normalized.");
    result.resize(used);
    return result;
#else
    const auto checked = [](UErrorCode error) {
        if (U_FAILURE(error) && error != U_BUFFER_OVERFLOW_ERROR)
            throw std::runtime_error("A library name or path is not valid UTF-8.");
    };
    auto error = U_ZERO_ERROR;
    int32_t length = 0;
    u_strFromUTF8(nullptr, 0, &length, value.data(), value.size(), &error);
    checked(error);
    std::u16string input(length, 0);
    error = U_ZERO_ERROR;
    u_strFromUTF8(input.data(), length, nullptr, value.data(), value.size(), &error);
    checked(error);
    std::u16string_view trimmed(input);
    if (trim) {
        // Foundation's whitespace set also includes zero-width space.
        const auto isFoundationSpace = [](char16_t ch) { return ch == u'\u200b' || u_isUWhiteSpace(ch); };
        while (!trimmed.empty() && isFoundationSpace(trimmed.front()))
            trimmed.remove_prefix(1);
        while (!trimmed.empty() && isFoundationSpace(trimmed.back()))
            trimmed.remove_suffix(1);
    }
    if (trimmed.empty())
        return {};
    error = U_ZERO_ERROR;
    const auto* nfc = unorm2_getNFCInstance(&error);
    checked(error);
    length = unorm2_normalize(nfc, trimmed.data(), trimmed.size(), nullptr, 0, &error);
    checked(error);
    std::u16string text(length, 0);
    error = U_ZERO_ERROR;
    unorm2_normalize(nfc, trimmed.data(), trimmed.size(), text.data(), length, &error);
    checked(error);
    if (lower) {
        // Match Swift's locale-independent, context-free lowercase mapping for sigma.
        std::ranges::replace(text, u'Σ', u'σ');
        error = U_ZERO_ERROR;
        length = u_strToLower(nullptr, 0, text.data(), text.size(), "", &error);
        checked(error);
        std::u16string lowered(length, 0);
        error = U_ZERO_ERROR;
        u_strToLower(lowered.data(), length, text.data(), text.size(), "", &error);
        checked(error);
        text = std::move(lowered);
    }
    error = U_ZERO_ERROR;
    u_strToUTF8(nullptr, 0, &length, text.data(), text.size(), &error);
    checked(error);
    std::string result(length, '\0');
    error = U_ZERO_ERROR;
    u_strToUTF8(result.data(), length, nullptr, text.data(), text.size(), &error);
    checked(error);
    return result;
#endif
}

std::string stableUuid(std::string_view key)
{
    std::array<uint8_t, 16> bytes{};
    av_md5_sum(bytes.data(), reinterpret_cast<const uint8_t*>(key.data()), key.size());
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string result;
    result.reserve(36);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            result += '-';
        result += hex[bytes[i] >> 4];
        result += hex[bytes[i] & 15];
    }
    return result;
}
}

std::string musicIdentifier(std::string_view value) { return normalized(value, true, false); }
std::string musicNamespace(std::string_view value) { return normalized(value, true, true); }
std::string musicLibraryId(std::string_view root) { return stableUuid("library|" + std::string(root)); }

std::string musicSongId(std::string_view nameSpace, std::string_view relative)
{
    while (relative.starts_with('/'))
        relative.remove_prefix(1);
    return stableUuid("song|" + musicNamespace(nameSpace) + "|" + normalized(relative, false, false));
}

std::string musicArtistId(std::string_view name)
{
    return stableUuid("artist|" + normalized(name, false, true));
}

std::string musicAlbumId(std::string_view artistId, std::string_view title)
{
    return stableUuid("album|" + std::string(artistId) + "|" + normalized(title, false, true));
}
