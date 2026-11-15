// SPDX-License-Identifier: Apache-2.0
#include <vthost/TabEdits.hpp>

#include <vtbackend/core/Color.hpp>

#include <algorithm>
#include <array>
#include <string>

#include <vtworkspace/ObjectName.hpp>
#include <vtworkspace/Pane.hpp>
#include <vtworkspace/Tab.hpp>

namespace vthost
{

namespace
{
    using vtworkspace::SessionModel;
    using vtworkspace::TabColorSource;

    /// The pane an edit names, and the tab and workspace that hold it.
    struct EditTarget
    {
        vtworkspace::Workspace& workspace;
        vtworkspace::Tab& tab;
        vtworkspace::Pane& leaf;
    };

    /// The model an edit changes, and where it changes a tab's name.
    struct EditScope
    {
        SessionModel& model;
        TabNames& names;
    };

    /// What one kind of edit does: compares, and mutates only when the model differs.
    using TabEditFn = TabEditEffect (*)(EditScope const& scope,
                                        EditTarget const& target,
                                        proto::EditTab const& edit);

    struct TabEditRow
    {
        proto::TabEditKind kind;
        TabEditFn apply;
    };

    [[nodiscard]] TabEditEffect activatePane(EditScope const& scope,
                                             EditTarget const& target,
                                             proto::EditTab const&)
    {
        if (target.tab.activePane() == &target.leaf)
            return TabEditEffect::Unchanged;
        scope.model.setActivePane(target.tab.id(), target.leaf.id());
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect zoom(EditScope const& scope,
                                     EditTarget const& target,
                                     proto::EditTab const& edit)
    {
        if (target.tab.zoomedLeafId() == target.leaf.id())
            return TabEditEffect::Unchanged;
        // Zoom follows focus: activating the pane of a tab zoomed on another moves the zoom to it.
        std::ignore = activatePane(scope, target, edit);
        if (!target.tab.isZoomed())
            scope.model.toggleActivePaneZoom(target.tab.id());
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect unzoom(EditScope const& scope,
                                       EditTarget const& target,
                                       proto::EditTab const&)
    {
        if (!target.tab.isZoomed())
            return TabEditEffect::Unchanged;
        scope.model.toggleActivePaneZoom(target.tab.id());
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect clearName(EditScope const& scope,
                                          EditTarget const& target,
                                          proto::EditTab const&)
    {
        if (scope.names.nameOf(target.tab.id()).empty())
            return TabEditEffect::Unchanged;
        scope.names.setName(target.tab.id(), {});
        return TabEditEffect::Changed;
    }

    /// An old window's "set title" (the same kind, the same field) arrives here too, as a rename.
    [[nodiscard]] TabEditEffect setName(EditScope const& scope,
                                        EditTarget const& target,
                                        proto::EditTab const& edit)
    {
        // Compared as it would be held: a blank name clears it, never blanks the tab.
        auto name = vtworkspace::normalizedName(edit.name);
        if (name.empty())
            return clearName(scope, target, edit);
        if (scope.names.nameOf(target.tab.id()) == name)
            return TabEditEffect::Unchanged;
        scope.names.setName(target.tab.id(), std::move(name));
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect setColor(EditScope const& scope,
                                         EditTarget const& target,
                                         proto::EditTab const& edit)
    {
        auto const color = vtbackend::RGBColor { edit.color };
        if (target.tab.color(TabColorSource::User) == color)
            return TabEditEffect::Unchanged;
        scope.model.setTabColor(target.tab.id(), TabColorSource::User, color);
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect clearColor(EditScope const& scope,
                                           EditTarget const& target,
                                           proto::EditTab const&)
    {
        if (!target.tab.color(TabColorSource::User))
            return TabEditEffect::Unchanged;
        scope.model.resetTabColor(target.tab.id(), TabColorSource::User);
        return TabEditEffect::Changed;
    }

    [[nodiscard]] TabEditEffect activateTab(EditScope const& scope,
                                            EditTarget const& target,
                                            proto::EditTab const&)
    {
        if (target.workspace.activeTab() == &target.tab)
            return TabEditEffect::Unchanged;
        scope.model.activateTab(target.workspace.id(), target.tab.id());
        return TabEditEffect::Changed;
    }

    constexpr auto TabEditTable = std::array {
        TabEditRow { .kind = proto::TabEditKind::ActivatePane, .apply = &activatePane },
        TabEditRow { .kind = proto::TabEditKind::Zoom, .apply = &zoom },
        TabEditRow { .kind = proto::TabEditKind::Unzoom, .apply = &unzoom },
        TabEditRow { .kind = proto::TabEditKind::SetName, .apply = &setName },
        TabEditRow { .kind = proto::TabEditKind::ClearName, .apply = &clearName },
        TabEditRow { .kind = proto::TabEditKind::SetColor, .apply = &setColor },
        TabEditRow { .kind = proto::TabEditKind::ClearColor, .apply = &clearColor },
        TabEditRow { .kind = proto::TabEditKind::ActivateTab, .apply = &activateTab },
    };
} // namespace

std::string ModelTabNames::nameOf(vtworkspace::TabId tab) const
{
    auto const* found = _model->findTab(tab);
    return found != nullptr ? found->name() : std::string {};
}

void ModelTabNames::setName(vtworkspace::TabId tab, std::string name)
{
    _model->setTabName(tab, std::move(name));
}

std::string RegistryTabNames::nameOf(vtworkspace::TabId tab) const
{
    auto const uuid = _objects->uuidOf(tab);
    return uuid ? std::string { _objects->nameOf(
                      vtcontrol::ObjectRef { .kind = vtcontrol::ObjectKind::Tab, .uuid = *uuid }) }
                : std::string {};
}

void RegistryTabNames::setName(vtworkspace::TabId tab, std::string name)
{
    if (auto const uuid = _objects->uuidOf(tab))
        _objects->setName(vtcontrol::ObjectRef { .kind = vtcontrol::ObjectKind::Tab, .uuid = *uuid },
                          std::move(name));
}

std::expected<TabEditEffect, TabEditError> applyTabEdit(SessionModel& model,
                                                        TabNames& names,
                                                        proto::EditTab const& edit)
{
    auto const [workspace, tab, leaf] = model.findSessionLeaf(vtworkspace::SessionId { edit.session });
    if (workspace == nullptr || tab == nullptr || leaf == nullptr)
        return std::unexpected { TabEditError::UnknownSession };
    auto const row = std::ranges::find(TabEditTable, edit.kind, &TabEditRow::kind);
    if (row == TabEditTable.end())
        return std::unexpected { TabEditError::UnknownKind };
    return row->apply(EditScope { .model = model, .names = names },
                      EditTarget { .workspace = *workspace, .tab = *tab, .leaf = *leaf },
                      edit);
}

} // namespace vthost
