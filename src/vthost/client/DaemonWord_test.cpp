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
