// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

#include <vtworkspace/ObjectName.hpp>

TEST_CASE("ObjectName: a name is set without control characters, and a blank one clears it", "[name]")
{
    struct Row
    {
        std::string_view given;
        std::string_view normalized;
    };
    using namespace std::string_view_literals;
    auto const rows = std::array {
        Row { .given = "build", .normalized = "build" },
        Row { .given = "my tab", .normalized = "my tab" },
        Row { .given = " padded ", .normalized = " padded " },
        Row { .given = "", .normalized = "" },
        Row { .given = "   ", .normalized = "" },
        Row { .given = "x\nkill-server #", .normalized = "xkill-server #" },
        Row { .given = "a\rb\tc", .normalized = "abc" },
        Row { .given = "bell\a", .normalized = "bell" },
        Row { .given = "esc\x1b[31m", .normalized = "esc[31m" },
        Row { .given = "del\x7f", .normalized = "del" },
        Row { .given = "nul\0x"sv, .normalized = "nulx" },
        Row { .given = "\t \n", .normalized = "" },
        Row { .given = "caf\xc3\xa9", .normalized = "caf\xc3\xa9" },
    };
    for (auto const& row: rows)
    {
        INFO("given '" << row.given << "'");
        CHECK(vtworkspace::normalizedName(row.given) == row.normalized);
    }
}
