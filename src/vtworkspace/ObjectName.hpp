// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// What a user-given name of a workspace, a tab or a pane may hold, decided once for every place one is set:
/// the GUI, the control API, a layout, a tmux client.

#include <algorithm>
#include <string>
#include <string_view>

namespace vtworkspace
{

/// @param c A byte of a name.
/// @return Whether @p c is a control character (C0 or DEL), which no name may hold: one ends a line of the
///         protocols a name is written into (tmux's command lines), or moves the cursor where it is shown.
[[nodiscard]] constexpr bool isNameControlCharacter(char c) noexcept
{
    auto const byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

/// @param c A byte of a name.
/// @return Whether @p c is blank, as a name of nothing but blanks names nothing.
[[nodiscard]] constexpr bool isNameBlank(char c) noexcept
{
    return c == ' ';
}

/// Normalises a name where it is set, so every setter holds and compares the same text.
/// @param name A name as the user gave it.
/// @return @p name without its control characters (C0 and DEL); empty, which clears the name, when nothing
///         but blanks remains. Multi-byte UTF-8 is kept as is: none of its bytes is a control character.
[[nodiscard]] constexpr std::string normalizedName(std::string_view name)
{
    auto normalized = std::string {};
    normalized.reserve(name.size());
    for (auto const c: name)
        if (!isNameControlCharacter(c))
            normalized += c;
    if (std::ranges::all_of(normalized, isNameBlank))
        normalized.clear();
    return normalized;
}

} // namespace vtworkspace
