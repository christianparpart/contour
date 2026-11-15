// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <libunicode/ucd.h>

#include <algorithm>
#include <span>
#include <string_view>

namespace text
{

/// The file name of the bundled emoji font (Noto Color Emoji, COLRv1 build).
inline constexpr std::string_view BundledEmojiFontFileName = "Noto-COLRv1.ttf";

/// The directory the bundled emoji font lives in: a sibling of the bundled fonts directory, not a child.
///
/// The bundled fonts directory is registered with the platform's font registry at startup, and fontconfig
/// scans a registered directory recursively. A registered emoji font would become the system's own
/// choice for every emoji -- on Ubuntu 24 it even outranks the system's bitmap-only Noto Color Emoji of
/// the same family, as it has outlines. The bundled emoji font is meant to be the LAST one asked.
inline constexpr std::string_view BundledEmojiFontDirectoryName = "emoji-font";

/// @return Whether @p codepoints are emoji text: every one an emoji character, an emoji component (ZWJ,
///         variation selectors, skin tone modifiers, keycap, tags, regional indicators, keycap bases) or
///         VS15, and at least one of them a pictograph, a regional indicator or the keycap mark -- so that a
///         run of digits,
///         which are emoji components too, is not mistaken for one.
/// @param codepoints The span to classify.
[[nodiscard]] inline bool isEmojiSpan(std::span<char32_t const> codepoints) noexcept
{
    constexpr auto Vs15 = char32_t { 0xFE0E };
    auto const isRegionalIndicator = [](char32_t codepoint) noexcept {
        return codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF;
    };
    auto const isEmojiCodepoint = [](char32_t codepoint) noexcept {
        return unicode::is_extended_pictographic(codepoint) || unicode::is_emoji_component(codepoint)
               || unicode::is_emoji(codepoint) || codepoint == Vs15;
    };
    constexpr auto CombiningEnclosingKeycap = char32_t { 0x20E3 };
    auto const isPictograph = [&](char32_t codepoint) noexcept {
        return unicode::is_extended_pictographic(codepoint) || isRegionalIndicator(codepoint)
               || codepoint == CombiningEnclosingKeycap;
    };
    return std::ranges::all_of(codepoints, isEmojiCodepoint) && std::ranges::any_of(codepoints, isPictograph);
}

} // namespace text
