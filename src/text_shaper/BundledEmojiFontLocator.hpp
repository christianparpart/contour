// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <text_shaper/EmojiSpan.hpp>
#include <text_shaper/FontLocator.hpp>

#include <filesystem>
#include <optional>

namespace text
{

/// A font locator that backs another with the bundled emoji font, as the last answer for emoji text.
///
/// A system's emoji font knows the emoji sequences of the Unicode version it was built for. One older
/// than a sequence draws it as its pieces -- "people with bunny ears" in two skin tones as a person, a rabbit
/// and a person (#2381)
/// -- and nothing about that is a missing glyph, so ordinary fallback never notices. The shaper does notice,
/// and asks resolve() for fonts to try; for an emoji span (see isEmojiSpan) this locator answers with
/// everything the locator it wraps answers, and then the bundled font. The system's fonts are therefore
/// always tried first, and the bundled one only where none of them renders the sequence whole.
///
/// Only resolve() is affected. The bundled font is never registered with the platform's font registry,
/// so no description the user configures can match it.
class BundledEmojiFontLocator final: public FontLocator
{
  public:
    /// @param inner     The locator asked first, and for everything else. It must outlive this one.
    /// @param emojiFont The bundled emoji font file. Where it is not a file, as in a build without it, this
    ///                  locator simply forwards to @p inner.
    BundledEmojiFontLocator(FontLocator& inner, std::filesystem::path const& emojiFont);

    /// @return Whether the bundled emoji font was found, so that this locator does more than forward.
    [[nodiscard]] bool hasEmojiFont() const noexcept { return _emojiFont.has_value(); }

    [[nodiscard]] CoverageFallbackSpacing coverageFallbackSpacing() const noexcept override;
    [[nodiscard]] FontSourceList all() override;
    [[nodiscard]] FontSourceList locate(FontDescription const& description) override;
    [[nodiscard]] FontSourceList resolve(gsl::span<char32_t const> codepoints) override;

  private:
    FontLocator& _inner;
    std::optional<FontPath> _emojiFont;
};

/// @return Where the bundled emoji font is, given the bundled fonts directory: beside it, never in it.
/// @param bundledFontsDirectory The directory registered with the platform's font registry at startup.
[[nodiscard]] inline std::filesystem::path bundledEmojiFontPath(
    std::filesystem::path const& bundledFontsDirectory)
{
    return bundledFontsDirectory.parent_path() / BundledEmojiFontDirectoryName / BundledEmojiFontFileName;
}

} // namespace text
