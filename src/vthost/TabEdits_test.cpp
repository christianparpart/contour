// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <vtcontrol/Api.hpp>
#include <vthost/TabEdits.hpp>
#include <vthost/control/ControlService.hpp>
#include <vthost/testing/ControlHarness.hpp>
#include <vtworkspace/Pane.hpp>
#include <vtworkspace/Tab.hpp>
#include <vtworkspace/TabName.hpp>

using vthost::applyTabEdit;
using vthost::TabEditEffect;
using vthost::TabEditError;
using vthost::proto::EditTab;
using vthost::proto::TabEditKind;

namespace
{

/// Counts every model event, so a case can tell whether an edit fired any.
struct CountingEvents final: vtworkspace::ModelEvents
{
    int count = 0;

    void tabAdded(vtworkspace::WorkspaceId, vtworkspace::TabId, int) override { ++count; }
    void tabClosed(vtworkspace::WorkspaceId, vtworkspace::TabId, int) override { ++count; }
    void tabMoved(vtworkspace::WorkspaceId, vtworkspace::TabId, int, int) override { ++count; }
    void activeTabChanged(vtworkspace::WorkspaceId, vtworkspace::TabId, int) override { ++count; }
    void paneSplit(vtworkspace::TabId, vtworkspace::PaneId, vtworkspace::PaneId) override { ++count; }
    void paneClosed(vtworkspace::TabId, vtworkspace::PaneId, vtworkspace::PaneId) override { ++count; }
    void activePaneChanged(vtworkspace::TabId, vtworkspace::PaneId) override { ++count; }
    void paneRatioChanged(vtworkspace::TabId, vtworkspace::PaneId, double) override { ++count; }
    void paneZoomChanged(vtworkspace::TabId, std::optional<vtworkspace::PaneId>) override { ++count; }
    void tabTitleChanged(vtworkspace::TabId) override { ++count; }
    void tabColorChanged(vtworkspace::TabId) override { ++count; }
};

/// A workspace of two tabs: the first split in two (sessions 1 and 2, the second pane active), the second
/// holding session 3 and active.
struct TwoPaneTab
{
    CountingEvents events;
    uint64_t nextSession = 1;
    vtworkspace::SessionModel model { events, [this] { return vtworkspace::SessionId { nextSession++ }; } };
    vtworkspace::WorkspaceId workspace = model.createWorkspace()->id();
    vtworkspace::Tab* first = model.createTab(workspace);
    vtworkspace::Pane* second = model.splitActivePane(first->id(), vtworkspace::SplitState::Horizontal);
    vtworkspace::Tab* other = model.createTab(workspace);
    vthost::ModelTabNames names { model };

    TwoPaneTab() { model.setActivePane(first->id(), *paneOf(2)); }

    /// @return The id of the pane hosting @p session.
    [[nodiscard]] std::optional<vtworkspace::PaneId> paneOf(uint64_t session) const
    {
        auto const [owner, tab, leaf] = model.findSessionLeaf(vtworkspace::SessionId { session });
        return leaf != nullptr ? std::optional { leaf->id() } : std::nullopt;
    }
};

/// One kind of edit: what puts the model in the state the edit changes, the edit, and what it then holds.
struct EditRow
{
    std::string name;
    EditTab edit;
    std::function<void(TwoPaneTab&)> prepare;
    std::function<bool(TwoPaneTab&)> holds;
};

} // namespace

TEST_CASE("each tab edit changes the model once and is idempotent", "[vthost][tab-edits]")
{
    auto const nothing = [](TwoPaneTab&) {
    };
    auto const rows = std::vector<EditRow> {
        { .name = "ActivatePane",
          .edit = { .session = 1, .kind = TabEditKind::ActivatePane },
          .prepare = nothing,
          .holds = [](TwoPaneTab& t) { return t.first->activePane()->id() == t.paneOf(1); } },
        { .name = "Zoom",
          .edit = { .session = 1, .kind = TabEditKind::Zoom },
          .prepare = nothing,
          .holds = [](TwoPaneTab& t) { return t.first->zoomedLeafId() == t.paneOf(1); } },
        { .name = "Unzoom",
          .edit = { .session = 1, .kind = TabEditKind::Unzoom },
          .prepare = [](TwoPaneTab& t) { t.model.toggleActivePaneZoom(t.first->id()); },
          .holds = [](TwoPaneTab& t) { return !t.first->isZoomed(); } },
        { .name = "SetName",
          .edit = { .session = 1, .kind = TabEditKind::SetName, .name = "build" },
          .prepare = nothing,
          .holds = [](TwoPaneTab& t) { return t.first->name() == "build"; } },
        { .name = "ClearName",
          .edit = { .session = 1, .kind = TabEditKind::ClearName },
          .prepare = [](TwoPaneTab& t) { t.model.setTabName(t.first->id(), "old"); },
          .holds = [](TwoPaneTab& t) { return t.first->name().empty(); } },
        { .name = "SetColor",
          .edit = { .session = 1, .kind = TabEditKind::SetColor, .color = 0x112233 },
          .prepare = nothing,
          .holds =
              [](TwoPaneTab& t) {
                  return t.first->color(vtworkspace::TabColorSource::User)
                         == vtbackend::RGBColor { 0x112233U };
              } },
        { .name = "ClearColor",
          .edit = { .session = 1, .kind = TabEditKind::ClearColor },
          .prepare =
              [](TwoPaneTab& t) {
                  t.model.setTabColor(
                      t.first->id(), vtworkspace::TabColorSource::User, vtbackend::RGBColor { 0x445566U });
              },
          .holds = [](TwoPaneTab& t) { return !t.first->color(vtworkspace::TabColorSource::User); } },
        { .name = "ActivateTab",
          .edit = { .session = 1, .kind = TabEditKind::ActivateTab },
          .prepare = nothing,
          .holds = [](TwoPaneTab& t) { return t.model.workspace(t.workspace)->activeTab() == t.first; } },
    };
    CHECK(rows.size() == 8); // every TabEditKind

    for (auto const& row: rows)
    {
        INFO(row.name);
        auto t = TwoPaneTab {};
        row.prepare(t);
        REQUIRE_FALSE(row.holds(t));
        t.events.count = 0;

        CHECK(applyTabEdit(t.model, t.names, row.edit) == TabEditEffect::Changed);
        CHECK(row.holds(t));
        CHECK(t.events.count > 0);

        auto const eventsAfterFirst = t.events.count;
        CHECK(applyTabEdit(t.model, t.names, row.edit) == TabEditEffect::Unchanged);
        CHECK(row.holds(t));
        CHECK(t.events.count == eventsAfterFirst);
    }
}

TEST_CASE("an edit naming an unknown session or kind is refused", "[vthost][tab-edits]")
{
    auto t = TwoPaneTab {};
    CHECK(applyTabEdit(t.model, t.names, { .session = 999, .kind = TabEditKind::SetName }).error()
          == TabEditError::UnknownSession);
    CHECK(applyTabEdit(t.model, t.names, { .session = 1, .kind = static_cast<TabEditKind>(0xEE) }).error()
          == TabEditError::UnknownKind);
    CHECK(t.first->name().empty());
}

TEST_CASE("a zoom on another pane of a zoomed tab moves the zoom", "[vthost][tab-edits]")
{
    auto t = TwoPaneTab {};
    t.model.toggleActivePaneZoom(t.first->id()); // zoomed on session 2
    CHECK(applyTabEdit(t.model, t.names, { .session = 1, .kind = TabEditKind::Zoom })
          == TabEditEffect::Changed);
    CHECK(t.first->zoomedLeafId() == t.paneOf(1));
}

namespace
{

using vthost::testing::ControlHarness;

/// Calls one control method and returns what it answered with, which it must answer at once.
/// @tparam Reply The method's reply type.
template <typename Reply, typename Request>
[[nodiscard]] std::expected<Reply, vtcontrol::ControlError> callSync(ControlHarness& h,
                                                                     Request const& request)
{
    auto result = std::optional<std::expected<Reply, vtcontrol::ControlError>> {};
    h.service.handle(h.context, request, [&](std::expected<Reply, vtcontrol::ControlError> reply) {
        result = std::move(reply);
    });
    REQUIRE(result.has_value());
    return std::move(*result);
}

/// @return The tab at @p index of @p h's workspace, as the control API lists it.
[[nodiscard]] vtcontrol::TabInfo listedTab(ControlHarness& h, std::size_t index)
{
    auto const list = callSync<vtcontrol::ListReply>(h, vtcontrol::ListRequest {});
    REQUIRE(list.has_value());
    REQUIRE(list->workspaces.size() == 1);
    REQUIRE(list->workspaces[0].tabs.size() > index);
    return list->workspaces[0].tabs[index];
}

/// Applies @p edit to @p h's daemon as a client's EditTab is: a name in its object registry.
[[nodiscard]] std::expected<TabEditEffect, TabEditError> applyOnDaemon(ControlHarness& h, EditTab const& edit)
{
    auto names = vthost::RegistryTabNames { h.host.objects() };
    return applyTabEdit(h.host.model(), names, edit);
}

} // namespace

TEST_CASE("a SetName names the daemon's tab, which is then addressable by it", "[vthost][tab-edits][name]")
{
    auto h = ControlHarness {};
    std::ignore = h.openTab();
    auto const session = h.openTab();

    REQUIRE(applyOnDaemon(h, { .session = session.value, .kind = TabEditKind::SetName, .name = "build" })
            == TabEditEffect::Changed);
    CHECK(listedTab(h, 1).label == "build");
    // The model mirrors the registry, so what reads the model (a tmux client, a status line) sees it too.
    CHECK(h.host.model().workspace(h.host.workspaceId())->tabAt(1)->name() == "build");

    // `tab:build` resolves to it: renaming by that address reaches the same tab.
    REQUIRE(callSync<vtcontrol::EmptyReply>(
                h, vtcontrol::SetLabelRequest { .object = "tab:build", .label = "ci" })
                .has_value());
    CHECK(listedTab(h, 1).label == "ci");

    REQUIRE(applyOnDaemon(h, { .session = session.value, .kind = TabEditKind::ClearName })
            == TabEditEffect::Changed);
    CHECK(listedTab(h, 1).label.empty());
    CHECK(h.host.model().workspace(h.host.workspaceId())->tabAt(1)->name().empty());
}

TEST_CASE("a tab's name stands beside its program's title, which names it once the name is cleared",
          "[vthost][tab-edits][name]")
{
    auto h = ControlHarness {};
    auto const session = h.openTab();

    REQUIRE(applyOnDaemon(h, { .session = session.value, .kind = TabEditKind::SetName, .name = "build" })
            == TabEditEffect::Changed);
    h.terminal(session).writeToScreen("\033]2;vim\007");
    h.drain();
    // The API states both: the user's name, and the program's title, which the name does not hide.
    auto const named = listedTab(h, 0);
    CHECK(named.label == "build");
    CHECK(named.title == "vim");
    CHECK(vtworkspace::tabNameOf({ .name = named.label, .title = named.title }, {}) == "build");

    REQUIRE(applyOnDaemon(h, { .session = session.value, .kind = TabEditKind::ClearName })
            == TabEditEffect::Changed);
    auto const cleared = listedTab(h, 0);
    CHECK(cleared.label.empty());
    CHECK(cleared.title == "vim");
    CHECK(vtworkspace::tabNameOf({ .name = cleared.label, .title = cleared.title }, {}) == "vim");
}
