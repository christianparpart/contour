// SPDX-License-Identifier: Apache-2.0
#include <text_shaper/BundledEmojiFontLocator.hpp>

#include <text_shaper/EmojiSpan.hpp>

#include <core/Utf8Path.hpp>

#include <span>
#include <system_error>

namespace text
{

BundledEmojiFontLocator::BundledEmojiFontLocator(FontLocator& inner, std::filesystem::path const& emojiFont):
    _inner { inner }
{
    auto error = std::error_code {};
    if (std::filesystem::is_regular_file(emojiFont, error))
        _emojiFont = FontPath { .value = core::pathToUtf8(emojiFont) };
}

CoverageFallbackSpacing BundledEmojiFontLocator::coverageFallbackSpacing() const noexcept
{
    return _inner.coverageFallbackSpacing();
}

FontSourceList BundledEmojiFontLocator::all()
{
    return _inner.all();
}

FontSourceList BundledEmojiFontLocator::locate(FontDescription const& description)
{
    return _inner.locate(description);
}

FontSourceList BundledEmojiFontLocator::resolve(gsl::span<char32_t const> codepoints)
{
    auto sources = _inner.resolve(codepoints);
    if (_emojiFont && isEmojiSpan(std::span<char32_t const>(codepoints.data(), codepoints.size())))
        sources.emplace_back(*_emojiFont);
    return sources;
}

} // namespace text
