// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <string_view>

#include <vthost/client/DaemonWord.hpp>

using vthost::client::DaemonTarget;
using vthost::client::DaemonWordError;
using vthost::client::DaemonWordKind;
using vthost::client::LocalDaemons;
using vthost::client::resolveDaemonWord;
using vthost::client::SavedHosts;

namespace
{
/// @return A lookup that knows exactly @p names.
[[nodiscard]] std::function<bool(std::string_view)> knowing(std::set<std::string, std::less<>> names)
{
    return [names = std::move(names)](std::string_view name) {
        return names.contains(name);
    };
}

[[nodiscard]] DaemonTarget resolved(std::string_view word,
                                    LocalDaemons const& local = {},
                                    SavedHosts const& saved = {})
{
    auto const target = resolveDaemonWord(word, local, saved);
    REQUIRE(target.has_value());
    return *target;
}
} // namespace

TEST_CASE("resolveDaemonWord reads paths, addresses and URLs by their spelling",
          "[vthost][client][daemon-word]")
{
    // A local daemon and a saved host of every word's name exist: a spelling wins over a name.
    auto const local = LocalDaemons { .contains = [](std::string_view) { return true; } };
    auto const saved = SavedHosts { .contains = [](std::string_view) { return true; } };
    for (auto const word: { "/run/x", "./sock", R"(C:\x\sock)", "/run/user/1000/x:1" })
    {
        INFO(word);
        auto const target = resolved(word, local, saved);
        CHECK(target.kind == DaemonWordKind::SocketPath);
        CHECK(target.text == word);
    }
    CHECK(resolved("box:2222", local, saved).kind == DaemonWordKind::HostPort);
    CHECK(resolved("ssh://box", local, saved).kind == DaemonWordKind::SshUrl);
    CHECK(resolved("ssh://box", local, saved).text == "ssh://box");
}

TEST_CASE("resolveDaemonWord looks a bare word up as a local daemon, then a saved host",
          "[vthost][client][daemon-word]")
{
    auto const work = knowing({ "work" });
    auto const none = knowing({});

    SECTION("only the local daemon")
    {
        auto const target = resolved("work", { .contains = work }, { .contains = none });
        CHECK(target.kind == DaemonWordKind::LocalName);
        CHECK(target.text == "work");
    }
    SECTION("only the saved host")
    {
        auto const target = resolved("work", { .contains = none }, { .contains = work });
        CHECK(target.kind == DaemonWordKind::SavedHost);
        CHECK(target.text == "work");
    }
    SECTION("both: ambiguous, naming the spellings that pick one")
    {
        auto const target = resolveDaemonWord("work", { .contains = work }, { .contains = work });
        REQUIRE_FALSE(target.has_value());
        CHECK(target.error() == DaemonWordError::Ambiguous);
        auto const message = vthost::client::describe(target.error(), "work");
        CHECK(message.contains("--daemon name:work"));
        CHECK(message.contains("--daemon host:work"));
    }
    SECTION("name: and host: say which, even when both exist")
    {
        auto const local = resolved("name:work", { .contains = work }, { .contains = work });
        CHECK(local.kind == DaemonWordKind::LocalName);
        CHECK(local.text == "work");
        auto const saved = resolved("host:work", { .contains = work }, { .contains = work });
        CHECK(saved.kind == DaemonWordKind::SavedHost);
        CHECK(saved.text == "work");
    }
    SECTION("neither: a saved host, which its resolver then says it does not know")
    {
        auto const target = resolved("nowhere", { .contains = none }, { .contains = none });
        CHECK(target.kind == DaemonWordKind::SavedHost);
        CHECK(resolved("nowhere").kind == DaemonWordKind::SavedHost); // no lookups at all
    }
    SECTION("a prefix with no name after it names nothing")
    {
        CHECK(resolveDaemonWord("name:", {}, {}) == std::unexpected { DaemonWordError::EmptyName });
        CHECK(resolveDaemonWord("host:", {}, {}) == std::unexpected { DaemonWordError::EmptyName });
    }
}

TEST_CASE(
    "resolveDaemonWord reads a number after name: or host: as a port only when no such daemon is called so",
    "[vthost][client][daemon-word]")
{
    // A machine may be called `host` or `name`, and a daemon or a saved host may be called `22`: the prefix
    // wins where its own lookup knows the number, and HOST:PORT otherwise.
    auto const none = knowing({});
    auto const twentyTwo = knowing({ "22" });
    struct Row
    {
        std::string_view word;
        LocalDaemons local;
        SavedHosts saved;
        DaemonWordKind kind;
        std::string_view text;
    };
    auto const rows = {
        Row { "name:22", { .contains = twentyTwo }, { .contains = none }, DaemonWordKind::LocalName, "22" },
        Row { "host:22", { .contains = none }, { .contains = twentyTwo }, DaemonWordKind::SavedHost, "22" },
        Row { "name:22", { .contains = none }, { .contains = none }, DaemonWordKind::HostPort, "name:22" },
        Row {
            "host:2222", { .contains = none }, { .contains = none }, DaemonWordKind::HostPort, "host:2222" },
        // Only the prefix's own kind counts.
        Row {
            "host:22", { .contains = twentyTwo }, { .contains = none }, DaemonWordKind::HostPort, "host:22" },
        Row {
            "name:22", { .contains = none }, { .contains = twentyTwo }, DaemonWordKind::HostPort, "name:22" },
        // Not a port (1-65535): a name.
        Row { "host:0", { .contains = none }, { .contains = none }, DaemonWordKind::SavedHost, "0" },
        Row { "host:99999", { .contains = none }, { .contains = none }, DaemonWordKind::SavedHost, "99999" },
        Row { "name:0", { .contains = none }, { .contains = none }, DaemonWordKind::LocalName, "0" },
        Row { "host:box", { .contains = none }, { .contains = none }, DaemonWordKind::SavedHost, "box" },
        Row { "name:work", { .contains = none }, { .contains = none }, DaemonWordKind::LocalName, "work" },
    };
    for (auto const& row: rows)
    {
        INFO(row.word);
        auto const target = resolved(row.word, row.local, row.saved);
        CHECK(target.kind == row.kind);
        CHECK(target.text == row.text);
    }
}

TEST_CASE("the spellings an ambiguous number is told to use pick each kind", "[vthost][client][daemon-word]")
{
    auto const seven = knowing({ "7" });
    auto const ambiguous = resolveDaemonWord("7", { .contains = seven }, { .contains = seven });
    REQUIRE_FALSE(ambiguous.has_value());
    auto const message = vthost::client::describe(ambiguous.error(), "7");
    REQUIRE(message.contains("--daemon name:7"));
    REQUIRE(message.contains("--daemon host:7"));
    CHECK(resolved("name:7", { .contains = seven }, { .contains = seven }).kind == DaemonWordKind::LocalName);
    CHECK(resolved("host:7", { .contains = seven }, { .contains = seven }).kind == DaemonWordKind::SavedHost);
}
