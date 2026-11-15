// SPDX-License-Identifier: Apache-2.0
#include <text_shaper/BundledEmojiFontLocator.hpp>
#include <text_shaper/EmojiSpan.hpp>
#include <text_shaper/MockFontLocator.hpp>

#include <core/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

using namespace text;

namespace
{

/// A file standing in for the emoji font, removed again when it goes out of scope. The locator only asks
/// whether it is a file, so its contents do not matter here.
struct StandInFont
{
    std::filesystem::path path;

    StandInFont():
        path { std::filesystem::temp_directory_path()
               / std::format("contour-bundled-emoji-test-{}.ttf", reinterpret_cast<std::uintptr_t>(this)) }
    {
        std::ofstream { path, std::ios::binary } << "not a font";
    }

    ~StandInFont()
    {
        auto error = std::error_code {};
        std::filesystem::remove(path, error);
    }

    StandInFont(StandInFont const&) = delete;
    StandInFont& operator=(StandInFont const&) = delete;
};

constexpr auto SystemFont = std::string_view { "system-fallback.ttf" };

/// People with bunny ears: medium skin tone, light skin tone (Emoji 17.0).
constexpr auto BunnyEars =
    std::u32string_view { U"\U0001F9D1\U0001F3FD‍\U0001F430‍\U0001F9D1\U0001F3FB" };

[[nodiscard]] std::string pathOf(FontSource const& source)
{
    return std::get<FontPath>(source).value;
}

[[nodiscard]] FontSourceList resolve(FontLocator& locator, std::u32string_view text)
{
    return locator.resolve(gsl::span<char32_t const>(text.data(), text.size()));
}

[[nodiscard]] bool emojiSpan(std::u32string_view text)
{
    return isEmojiSpan(std::span<char32_t const>(text.data(), text.size()));
}

} // namespace

TEST_CASE("EmojiSpan.classifies", "[emoji]")
{
    CHECK(emojiSpan(BunnyEars));
    CHECK(emojiSpan(U"\U0001F1E9\U0001F1EA")); // a flag: two regional indicators
    CHECK(emojiSpan(U"#️⃣"));                    // a keycap
    CHECK(emojiSpan(U"\U0001F600"));
    CHECK_FALSE(emojiSpan(U"12")); // digits are Emoji_Component, but alone they are text
    CHECK_FALSE(emojiSpan(U"A"));
    CHECK_FALSE(emojiSpan(U""));
    CHECK_FALSE(emojiSpan(U"\U0001F600A"));
}

TEST_CASE("BundledEmojiFontLocator.appends_after_inner_for_emoji_spans", "[emoji]")
{
    auto const _ = core::Finally { [] { MockFontLocator::configure({}); } };
    MockFontLocator::configureCoverage({ FontPath { .value = std::string { SystemFont } } });
    auto inner = MockFontLocator {};
    auto const font = StandInFont {};
    auto locator = BundledEmojiFontLocator { inner, font.path };
    REQUIRE(locator.hasEmojiFont());

    auto const sources = resolve(locator, BunnyEars);
    REQUIRE(sources.size() == 2);
    CHECK(pathOf(sources[0]) == SystemFont); // the system's answer first: it is always preferred
    CHECK(std::filesystem::path(pathOf(sources[1])) == font.path);
}

TEST_CASE("BundledEmojiFontLocator.forwards_text_spans", "[emoji]")
{
    auto const _ = core::Finally { [] { MockFontLocator::configure({}); } };
    MockFontLocator::configureCoverage({ FontPath { .value = std::string { SystemFont } } });
    auto inner = MockFontLocator {};
    auto const font = StandInFont {};
    auto locator = BundledEmojiFontLocator { inner, font.path };

    auto const sources = resolve(locator, U"A");
    REQUIRE(sources.size() == 1);
    CHECK(pathOf(sources[0]) == SystemFont);
}

TEST_CASE("BundledEmojiFontLocator.forwards_without_the_file", "[emoji]")
{
    auto const _ = core::Finally { [] { MockFontLocator::configure({}); } };
    MockFontLocator::configureCoverage({ FontPath { .value = std::string { SystemFont } } });
    auto inner = MockFontLocator {};
    auto locator =
        BundledEmojiFontLocator { inner, std::filesystem::path { "/nonexistent/Noto-COLRv1.ttf" } };
    CHECK_FALSE(locator.hasEmojiFont());

    auto const sources = resolve(locator, BunnyEars);
    REQUIRE(sources.size() == 1);
    CHECK(pathOf(sources[0]) == SystemFont);
}

TEST_CASE("BundledEmojiFontLocator.the_font_lies_outside_the_registered_fonts_directory", "[emoji]")
{
    // The bundled fonts directory is handed to fontconfig (FcConfigAppFontAddDir) and CoreText at startup,
    // and fontconfig scans it RECURSIVELY. An emoji font anywhere beneath it would be registered, and on a
    // system with an older, bitmap-only emoji font of the same family, would be chosen for every emoji.
    auto const fontsDirectory = std::filesystem::path { "/opt/contour/share/contour/fonts" };
    auto const emojiFont = bundledEmojiFontPath(fontsDirectory);

    CHECK(emojiFont.filename() == BundledEmojiFontFileName);
    auto const relative = emojiFont.lexically_relative(fontsDirectory);
    CHECK((relative.empty() || *relative.begin() == ".."));
}
