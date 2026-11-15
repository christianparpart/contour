// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

#include <vtworkspace/TabName.hpp>

using vtworkspace::TabNameCandidates;
using vtworkspace::TabNameSource;

TEST_CASE("TabName: a name wins over a tab's title, either over its panes", "[tab][name]")
{
    struct Row
    {
        std::string_view name;
        std::string_view title;
        TabNameSource source;
        std::string_view expected;
    };
    constexpr auto Panes = std::string_view { "zsh" };
    auto const rows = std::array {
        Row { .name = "", .title = "", .source = TabNameSource::Panes, .expected = Panes },
        Row { .name = "", .title = "renamed", .source = TabNameSource::Title, .expected = "renamed" },
        Row { .name = "build", .title = "", .source = TabNameSource::Name, .expected = "build" },
        Row { .name = "build", .title = "renamed", .source = TabNameSource::Name, .expected = "build" },
    };
    for (auto const& row: rows)
    {
        INFO("name '" << row.name << "', title '" << row.title << "'");
        auto const candidates = TabNameCandidates { .name = row.name, .title = row.title };
        CHECK(vtworkspace::tabNameSourceOf(candidates) == row.source);
        CHECK(vtworkspace::tabNameOf(candidates, Panes) == row.expected);
    }
}

TEST_CASE("TabName: the decision is a constant expression", "[tab][name]")
{
    STATIC_CHECK(vtworkspace::tabNameSourceOf({ .name = "a", .title = "b" }) == TabNameSource::Name);
    STATIC_CHECK(vtworkspace::tabNameOf({}, "pane") == "pane");
}
