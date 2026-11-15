// SPDX-License-Identifier: Apache-2.0
//
// compat(0.9.0): the command line's old spellings, rewritten before any parse.

#include <contour/cli/LegacyArguments.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

using contour::cli::LegacyArgumentRow;
using contour::cli::LegacyPlace;
using contour::cli::rewriteLegacyArguments;

namespace
{
using Strings = std::vector<std::string>;

/// @return The rewrite of @p args, as main() would see them.
template <typename... Args>
[[nodiscard]] contour::cli::RewrittenArguments rewrite(Args... args)
{
    auto const argv = std::array<char const*, sizeof...(Args)> { args... };
    return rewriteLegacyArguments(argv);
}

template <typename... Args>
[[nodiscard]] contour::cli::RewrittenArguments rewriteWith(std::span<LegacyArgumentRow const> rows,
                                                           Args... args)
{
    auto const argv = std::array<char const*, sizeof...(Args)> { args... };
    return rewriteLegacyArguments(argv, rows);
}
} // namespace

TEST_CASE("the label verb is rewritten to rename", "[contour][cli][compat-0.9.0]")
{
    auto const result = rewrite("contour", "label", "pane:logs", "tail");
    CHECK(result.arguments == Strings { "contour", "rename", "pane:logs", "tail" });
    REQUIRE(result.notes.size() == 1);
    CHECK(result.notes.front().contains("contour rename"));
}

TEST_CASE("workspace rename is rewritten to rename workspace:", "[contour][cli][compat-0.9.0]")
{
    CHECK(rewrite("contour", "workspace", "rename", "build", "x").arguments
          == Strings { "contour", "rename", "workspace:build", "x" });
    CHECK(rewrite("contour", "workspace", "rename", "1", "").arguments
          == Strings { "contour", "rename", "workspace:1", "" });

    SECTION("a workspace with a kind prefix keeps it")
    {
        CHECK(rewrite("contour", "workspace", "rename", "workspace:build", "x").arguments
              == Strings { "contour", "rename", "workspace:build", "x" });
    }
    SECTION("options before the operands stay where they are")
    {
        CHECK(rewrite("contour", "workspace", "rename", "--json", "build", "x").arguments
              == Strings { "contour", "rename", "--json", "workspace:build", "x" });
        CHECK(rewrite("contour", "workspace", "rename", "--", "build", "-x").arguments
              == Strings { "contour", "rename", "--", "workspace:build", "-x" });
    }
    SECTION("a line short of an operand is left for rename to refuse")
    {
        CHECK(rewrite("contour", "workspace", "rename", "build").arguments
              == Strings { "contour", "rename", "build" });
    }
}

TEST_CASE("a line with no legacy spelling comes back unchanged", "[contour][cli][compat-0.9.0]")
{
    auto const result = rewrite("contour", "rename", "pane:logs", "tail");
    CHECK(result.arguments == Strings { "contour", "rename", "pane:logs", "tail" });
    CHECK(result.notes.empty());
    CHECK(rewrite("contour").arguments == Strings { "contour" });
    CHECK(rewriteLegacyArguments(std::span<char const* const> {}).arguments.empty());
}

TEST_CASE("only the verb position is rewritten", "[contour][cli][compat-0.9.0]")
{
    // A positional value, or a command after `--`, that happens to read "label" is not a verb.
    auto const named = rewrite("contour", "rename", "pane:x", "label");
    CHECK(named.arguments == Strings { "contour", "rename", "pane:x", "label" });
    CHECK(named.notes.empty());
    CHECK(rewrite("contour", "new", "--", "cmd", "label", "x").arguments
          == Strings { "contour", "new", "--", "cmd", "label", "x" });
    CHECK(rewrite("contour", "send", "workspace", "rename", "a", "b").arguments
          == Strings { "contour", "send", "workspace", "rename", "a", "b" });
}

TEST_CASE("option rows rewrite a flag under its verbs, up to --", "[contour][cli][compat-0.9.0]")
{
    static constexpr auto Daemon = std::array<std::string_view, 1> { "daemon" };
    static constexpr auto OldFlag = std::array<std::string_view, 1> { "--old" };
    static constexpr auto NewFlag = std::array<std::string_view, 1> { "--new" };
    static constexpr auto Selector = std::array<std::string_view, 1> { "--pick" };
    static constexpr auto Kept = std::array<std::string_view, 2> { "name:", "host:" };
    static constexpr auto Rows = std::array {
        LegacyArgumentRow { .place = LegacyPlace::Option,
                            .verbPath = Daemon,
                            .oldTokens = OldFlag,
                            .newTokens = NewFlag,
                            .note = "daemon --old" },
        LegacyArgumentRow { .place = LegacyPlace::Option,
                            .oldTokens = OldFlag,
                            .newTokens = Selector,
                            .operandPrefix = "name:",
                            .keptPrefixes = Kept,
                            .note = "--old" },
    };

    SECTION("the first row that matches wins")
    {
        auto const result = rewriteWith(Rows, "contour", "daemon", "--old", "work");
        CHECK(result.arguments == Strings { "contour", "daemon", "--new", "work" });
        CHECK(result.notes == Strings { "daemon --old" });
    }
    SECTION("the operand is the next argument, or what follows '='")
    {
        CHECK(rewriteWith(Rows, "contour", "ls", "--old", "work", "--json").arguments
              == Strings { "contour", "ls", "--pick", "name:work", "--json" });
        CHECK(rewriteWith(Rows, "contour", "ls", "--old=work").arguments
              == Strings { "contour", "ls", "--pick=name:work" });
        CHECK(rewriteWith(Rows, "contour", "ls", "--old", "host:box").arguments
              == Strings { "contour", "ls", "--pick", "host:box" });
    }
    SECTION("one note per row, however often it applies")
    {
        auto const result = rewriteWith(Rows, "contour", "ls", "--old", "a", "--old", "b");
        CHECK(result.arguments == Strings { "contour", "ls", "--pick", "name:a", "--pick", "name:b" });
        CHECK(result.notes == Strings { "--old" });
    }
    SECTION("nothing after -- is an option")
    {
        auto const result = rewriteWith(Rows, "contour", "new", "--", "cmd", "--old", "x");
        CHECK(result.arguments == Strings { "contour", "new", "--", "cmd", "--old", "x" });
        CHECK(result.notes.empty());
    }
}
