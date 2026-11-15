// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <vtworkspace/TabName.hpp>

using vtworkspace::TabNameCandidates;
using vtworkspace::TabNameSource;

TEST_CASE("TabName: a name wins over a tab's title, either over its first pane's", "[tab][name]")
{
    struct Row
    {
        std::string_view name;
        std::string_view title;
        std::string_view firstPane;
        TabNameSource source;
        std::string_view expected;
    };
    auto const rows = std::array {
        Row { .name = "build",
              .title = "zsh",
              .firstPane = "vim",
              .source = TabNameSource::Name,
              .expected = "build" },
        Row { .name = "",
              .title = "zsh",
              .firstPane = "vim",
              .source = TabNameSource::Title,
              .expected = "zsh" },
        Row {
            .name = "", .title = "", .firstPane = "vim", .source = TabNameSource::Panes, .expected = "vim" },
        Row { .name = "build",
              .title = "",
              .firstPane = "vim",
              .source = TabNameSource::Name,
              .expected = "build" },
    };
    for (auto const& row: rows)
    {
        INFO("name '" << row.name << "', title '" << row.title << "', first pane '" << row.firstPane << "'");
        auto const candidates = TabNameCandidates { .name = row.name, .title = row.title };
        CHECK(vtworkspace::tabNameSourceOf(candidates) == row.source);
        CHECK(vtworkspace::tabNameOf(candidates, row.firstPane) == row.expected);
    }
}

TEST_CASE("TabName: the decision is a constant expression", "[tab][name]")
{
    STATIC_CHECK(vtworkspace::tabNameSourceOf({ .name = "a", .title = "b" }) == TabNameSource::Name);
    STATIC_CHECK(vtworkspace::tabNameOf({}, "pane") == "pane");
}

TEST_CASE("TabName: a restored tab without a name is named by its old runtime title",
          "[tab][name][compat-0.9.0]")
{
    CHECK(vtworkspace::restoredTabName("build", std::optional<std::string> { "edited {Cwd}" }) == "build");
    CHECK(vtworkspace::restoredTabName("", std::optional<std::string> { "mine" }) == "mine");
    CHECK(vtworkspace::restoredTabName("", std::nullopt).empty());
    CHECK(vtworkspace::restoredTabName("build", std::nullopt) == "build");
}
