// SPDX-License-Identifier: Apache-2.0
// compat(0.9.0): the old spelling of the control API, which goes before 0.9.0.
#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <vtcontrol/Catalog.hpp>
#include <vtcontrol/Events.hpp>
#include <vtcontrol/LegacySpelling.hpp>

namespace
{
using Request = std::pair<std::string, std::string>;

/// `workspace.list`'s example reply as the catalog held it before the rename, with `label` keys.
constexpr auto LegacyListExample = std::string_view {
    R"({"workspaces":[{"id":"workspace:018f3c4e-9a1b-7c2d-8e3f-0123456789ab","label":"","position":1,"tabs":[{"id":"tab:018f3c4e-9a1b-7c2d-8e3f-0123456789ac","label":"build","title":"zsh","position":1,"isActive":true,"panes":[{"id":"pane:018f3c4e-9a1b-7c2d-8e3f-0123456789ad","label":"","title":"zsh","cwd":"/home/me","isActive":true,"size":{"columns":80,"lines":24},"programStatus":[]}]}]}]})"
};

constexpr auto WorkspaceId = std::string_view { "workspace:018f3c4e-9a1b-7c2d-8e3f-0123456789ab" };
constexpr auto TabId = std::string_view { "tab:018f3c4e-9a1b-7c2d-8e3f-0123456789ac" };

[[nodiscard]] std::string_view listExample()
{
    return std::get<vtcontrol::Method<vtcontrol::ListRequest, vtcontrol::ListReply>>(vtcontrol::Methods)
        .responseExample;
}
} // namespace

TEST_CASE("each row of the legacy spelling maps the old request to the new one", "[vtcontrol][compat-0.9.0]")
{
    SECTION("object.setLabel is object.rename")
    {
        CHECK(vtcontrol::requestFromLegacy("object.setLabel", R"({"object":"pane:logs","label":"tail"})")
              == Request { "object.rename", R"({"object":"pane:logs","name":"tail"})" });
    }
    SECTION("workspace.rename is object.rename of workspace:<spec>")
    {
        CHECK(vtcontrol::requestFromLegacy("workspace.rename", R"({"workspace":"build","label":"release"})")
              == Request { "object.rename", R"({"object":"workspace:build","name":"release"})" });
    }
    SECTION("workspace.rename keeps a full id as it is")
    {
        auto const params = std::format(R"({{"workspace":"{}","label":"x"}})", WorkspaceId);
        CHECK(vtcontrol::requestFromLegacy("workspace.rename", params)
              == Request { "object.rename", std::format(R"({{"object":"{}","name":"x"}})", WorkspaceId) });
    }
    SECTION("tab.create, pane.split and workspace.create name what they open")
    {
        CHECK(vtcontrol::requestFromLegacy("tab.create", R"({"cwd":"/tmp","label":"build","focus":"keep"})")
              == Request { "tab.create", R"({"cwd":"/tmp","name":"build","focus":"keep"})" });
        CHECK(vtcontrol::requestFromLegacy("pane.split", R"({"pane":"logs","label":"tail"})")
              == Request { "pane.split", R"({"pane":"logs","name":"tail"})" });
        CHECK(vtcontrol::requestFromLegacy("workspace.create", R"({"cwd":"/tmp","label":""})")
              == Request { "workspace.create", R"({"cwd":"/tmp","name":""})" });
    }
    SECTION("workspace.switch picks a local daemon by toName")
    {
        CHECK(vtcontrol::requestFromLegacy("workspace.switch", R"({"toLabel":"admin","toWorkspace":"build"})")
              == Request { "workspace.switch", R"({"toName":"admin","toWorkspace":"build"})" });
    }
    SECTION("events.subscribe names the rename event")
    {
        CHECK(vtcontrol::requestFromLegacy("events.subscribe",
                                           R"({"kinds":["pane.bell","workspace.labelChanged"],"pane":""})")
              == Request { "events.subscribe", R"({"kinds":["pane.bell","object.renamed"],"pane":""})" });
    }
    SECTION("a method spelled alike passes through unparsed")
    {
        CHECK(vtcontrol::requestFromLegacy("pane.capture", "not json")
              == Request { "pane.capture", "not json" });
    }
    SECTION("a malformed request of a renamed method is an error")
    {
        CHECK_FALSE(vtcontrol::requestFromLegacy("object.setLabel", R"({"object":)").has_value());
        CHECK_FALSE(vtcontrol::requestFromLegacy("object.setLabel", R"([1])").has_value());
    }
}

TEST_CASE("a request reaches an old daemon in its spelling", "[vtcontrol][compat-0.9.0]")
{
    SECTION("object.rename is object.setLabel, for a workspace too")
    {
        CHECK(vtcontrol::requestToLegacy("object.rename", R"({"object":"pane:logs","name":"tail"})")
              == Request { "object.setLabel", R"({"object":"pane:logs","label":"tail"})" });
        CHECK(vtcontrol::requestToLegacy("object.rename", R"({"object":"workspace:build","name":"release"})")
              == Request { "object.setLabel", R"({"object":"workspace:build","label":"release"})" });
    }
    SECTION("workspace.switch's toName is toLabel")
    {
        CHECK(vtcontrol::requestToLegacy("workspace.switch", R"({"toName":"admin"})")
              == Request { "workspace.switch", R"({"toLabel":"admin"})" });
    }
    SECTION("events.subscribe's object.renamed is workspace.labelChanged")
    {
        CHECK(vtcontrol::requestToLegacy("events.subscribe", R"({"kinds":["object.renamed"]})")
              == Request { "events.subscribe", R"({"kinds":["workspace.labelChanged"]})" });
    }
}

TEST_CASE("a listing reaches an old client with label keys, byte for byte", "[vtcontrol][compat-0.9.0]")
{
    CHECK(vtcontrol::replyToLegacy("workspace.list", listExample()) == std::string { LegacyListExample });
    CHECK(vtcontrol::replyFromLegacy("workspace.list", LegacyListExample) == std::string { listExample() });
    // A reply that lists no objects is not touched, even where it carries a `name`.
    CHECK(vtcontrol::replyToLegacy("api.describe", R"({"name":"pane.split"})")
          == std::string { R"({"name":"pane.split"})" });
}

TEST_CASE("object.renamed reaches a legacy subscriber for a workspace only", "[vtcontrol][compat-0.9.0]")
{
    CHECK(vtcontrol::eventToLegacy("object.renamed",
                                   std::format(R"({{"object":"{}","name":"x"}})", WorkspaceId))
          == Request { "workspace.labelChanged", R"({"label":"x"})" });
    CHECK(vtcontrol::eventToLegacy("object.renamed", std::format(R"({{"object":"{}","name":"x"}})", TabId))
          == std::nullopt);
    CHECK(vtcontrol::eventToLegacy("pane.bell", "{}") == Request { "pane.bell", "{}" });
    CHECK(vtcontrol::eventFromLegacy("workspace.labelChanged", R"({"label":"x"})", WorkspaceId)
          == Request { "object.renamed", std::format(R"({{"object":"{}","name":"x"}})", WorkspaceId) });
}

TEST_CASE("a whole event crosses the spellings with its origin", "[vtcontrol][compat-0.9.0]")
{
    auto const named = vtcontrol::encodeEvent(vtcontrol::Event {
        .origin = { .workspace = std::string { WorkspaceId }, .tab = {}, .pane = {} },
        .payload = vtcontrol::ObjectRenamedEvent { .object = std::string { WorkspaceId }, .name = "x" } });
    auto const legacy = vtcontrol::encodedEventToLegacy(named);
    REQUIRE(legacy.has_value());
    CHECK(
        *legacy
        == std::format(
            R"({{"type":"workspace.labelChanged","workspace":"{}","tab":"","pane":"","payload":{{"label":"x"}}}})",
            WorkspaceId));
    CHECK(vtcontrol::encodedEventFromLegacy(*legacy) == named);

    auto const tabRenamed = vtcontrol::encodeEvent(vtcontrol::Event {
        .origin = { .workspace = std::string { WorkspaceId }, .tab = std::string { TabId }, .pane = {} },
        .payload = vtcontrol::ObjectRenamedEvent { .object = std::string { TabId }, .name = "x" } });
    CHECK(vtcontrol::encodedEventToLegacy(tabRenamed) == std::nullopt);

    auto const bell =
        vtcontrol::encodeEvent(vtcontrol::Event { .origin = {}, .payload = vtcontrol::BellEvent {} });
    CHECK(vtcontrol::encodedEventToLegacy(bell) == bell);
}

TEST_CASE("each way and back again is identity", "[vtcontrol][compat-0.9.0]")
{
    auto const fromThenTo = [](std::string_view method, std::string_view params) {
        auto const named = vtcontrol::requestFromLegacy(method, params);
        REQUIRE(named.has_value());
        return vtcontrol::requestToLegacy(named->first, named->second);
    };
    auto const toThenFrom = [](std::string_view method, std::string_view params) {
        auto const legacy = vtcontrol::requestToLegacy(method, params);
        REQUIRE(legacy.has_value());
        return vtcontrol::requestFromLegacy(legacy->first, legacy->second);
    };

    SECTION("object.setLabel")
    {
        auto const params = std::string { R"({"object":"pane:logs","label":"tail"})" };
        CHECK(fromThenTo("object.setLabel", params) == Request { "object.setLabel", params });
    }
    SECTION("object.rename")
    {
        auto const params = std::string { R"({"object":"workspace:build","name":"release"})" };
        CHECK(toThenFrom("object.rename", params) == Request { "object.rename", params });
    }
    SECTION("the rows spelled alike but for their keys")
    {
        for (auto const& [method, params]: {
                 std::pair { "tab.create", R"({"cwd":"/tmp","name":"build","focus":"keep"})" },
                 std::pair { "pane.split", R"({"pane":"logs","name":"tail"})" },
                 std::pair { "workspace.create", R"({"cwd":"/tmp","name":""})" },
                 std::pair { "workspace.switch", R"({"toName":"admin","toHost":"","toWorkspace":"build"})" },
                 std::pair { "events.subscribe", R"({"kinds":["object.renamed","pane.bell"],"pane":""})" },
             })
        {
            CHECK(toThenFrom(method, params) == Request { method, params });
            auto const legacy = vtcontrol::requestToLegacy(method, params);
            REQUIRE(legacy.has_value());
            CHECK(fromThenTo(legacy->first, legacy->second) == *legacy);
        }
    }
    SECTION("the listing")
    {
        auto const legacy = vtcontrol::replyToLegacy("workspace.list", listExample());
        REQUIRE(legacy.has_value());
        CHECK(vtcontrol::replyFromLegacy("workspace.list", *legacy) == std::string { listExample() });
    }
}
