// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vtcontrol/testing/SequentialUuidSource.hpp>
#include <vthost/ObjectRegistry.hpp>

namespace
{
using vtcontrol::ObjectKind;
using vtcontrol::ObjectRef;
using vtworkspace::SessionId;
using vtworkspace::TabId;
using vtworkspace::WorkspaceId;

[[nodiscard]] vthost::ObjectRegistry makeRegistry()
{
    return vthost::ObjectRegistry { std::make_shared<vtcontrol::testing::SequentialUuidSource>() };
}
} // namespace

TEST_CASE("ObjectRegistry mints distinct, increasing uuids", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const a = registry.mint();
    auto const b = registry.mint();
    CHECK(a < b);
}

TEST_CASE("ObjectRegistry maps ids to uuids and back, per kind", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const workspace = registry.mint();
    auto const tab = registry.mint();
    auto const pane = registry.mint();
    registry.bind(WorkspaceId { 1 }, workspace);
    registry.bind(TabId { 1 }, tab);
    registry.bind(SessionId { 1 }, pane);

    // The same small integer in three kinds names three objects.
    CHECK(registry.uuidOf(WorkspaceId { 1 }) == workspace);
    CHECK(registry.uuidOf(TabId { 1 }) == tab);
    CHECK(registry.uuidOf(SessionId { 1 }) == pane);
    CHECK_FALSE(registry.uuidOf(SessionId { 2 }));

    auto const found = registry.find(ObjectRef { ObjectKind::Pane, pane });
    REQUIRE(found);
    CHECK(std::get<SessionId>(*found) == SessionId { 1 });
    CHECK(std::get<TabId>(*registry.find(ObjectRef { ObjectKind::Tab, tab })) == TabId { 1 });
    CHECK(std::get<WorkspaceId>(*registry.find(ObjectRef { ObjectKind::Workspace, workspace }))
          == WorkspaceId { 1 });

    // A uuid of one kind is not found under another.
    CHECK_FALSE(registry.find(ObjectRef { ObjectKind::Tab, pane }));
}

TEST_CASE("ObjectRegistry forgets an object with its label", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const pane = registry.mint();
    auto const ref = ObjectRef { ObjectKind::Pane, pane };
    registry.bind(SessionId { 7 }, pane);
    registry.setName(ref, "logs");
    REQUIRE(registry.nameOf(ref) == "logs");

    registry.forget(SessionId { 7 });

    CHECK_FALSE(registry.uuidOf(SessionId { 7 }));
    CHECK_FALSE(registry.find(ref));
    CHECK(registry.nameOf(ref).empty());
    registry.forget(SessionId { 7 }); // a second time is a no-op
}

TEST_CASE("ObjectRegistry labels: set, replace, clear, and ignore the unknown", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const tab = registry.mint();
    auto const ref = ObjectRef { ObjectKind::Tab, tab };
    registry.bind(TabId { 3 }, tab);

    CHECK(registry.nameOf(ref).empty());
    registry.setName(ref, "build");
    CHECK(registry.nameOf(ref) == "build");
    registry.setName(ref, "test");
    CHECK(registry.nameOf(ref) == "test");
    registry.setName(ref, "");
    CHECK(registry.nameOf(ref).empty());

    auto const stranger = ObjectRef { ObjectKind::Tab, registry.mint() };
    registry.setName(stranger, "ghost");
    CHECK(registry.nameOf(stranger).empty());
}

TEST_CASE("ObjectRegistry rebinding replaces", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const first = registry.mint();
    auto const second = registry.mint();
    registry.bind(SessionId { 1 }, first);
    registry.bind(SessionId { 1 }, second);
    CHECK(registry.uuidOf(SessionId { 1 }) == second);
    CHECK_FALSE(registry.find(ObjectRef { ObjectKind::Pane, first }));
    CHECK(registry.find(ObjectRef { ObjectKind::Pane, second }));
}

TEST_CASE("ObjectRegistry tells its label observers of each change, until they unsubscribe", "[vthost][ids]")
{
    auto registry = makeRegistry();
    auto const workspace = registry.mint();
    auto const ref = ObjectRef { ObjectKind::Workspace, workspace };
    registry.bind(WorkspaceId { 2 }, workspace);
    auto told = std::vector<std::pair<ObjectRef, std::string>> {};
    auto subscription = std::optional { registry.onNameChanged(
        [&told](ObjectRef const& object, std::string_view name) { told.emplace_back(object, name); }) };

    registry.setName(ref, "ops");
    registry.setName(ref, "ops"); // unchanged: nothing to tell
    registry.setName(ObjectRef { ObjectKind::Workspace, registry.mint() }, "unbound"); // ignored
    registry.setName(ref, "");
    registry.setName(ref, ""); // cleared already

    REQUIRE(told.size() == 2);
    CHECK(told[0] == std::pair { ref, std::string { "ops" } });
    CHECK(told[1] == std::pair { ref, std::string {} });

    subscription.reset();
    registry.setName(ref, "after");
    CHECK(told.size() == 2);
}

TEST_CASE("ObjectRegistry: a label subscription outlives a moved registry, and the registry itself",
          "[vthost][ids]")
{
    auto told = 0;
    auto original = makeRegistry();
    auto subscription = original.onNameChanged([&told](ObjectRef const&, std::string_view) { ++told; });
    auto moved = std::move(original);
    auto const tab = moved.mint();
    moved.bind(TabId { 1 }, tab);

    moved.setName(ObjectRef { ObjectKind::Tab, tab }, "logs");
    CHECK(told == 1);

    {
        auto const gone = std::move(moved);
    }
    // Unsubscribing from a registry that is gone is a no-op.
    auto const dropped = std::move(subscription);
}
