// SPDX-License-Identifier: Apache-2.0
#include <vthost/ObjectRegistry.hpp>

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <vtworkspace/ObjectName.hpp>

namespace vthost
{

using vtcontrol::ObjectKind;
using vtcontrol::ObjectRef;
using vtcontrol::Uuid;

ObjectRegistry::ObjectRegistry(std::shared_ptr<vtcontrol::UuidSource> uuids):
    _uuids { std::move(uuids) }, _nameObservers { std::make_shared<NameObservers>() }
{
}

Uuid ObjectRegistry::mint()
{
    return _uuids->next();
}

ObjectRegistry::Table const& ObjectRegistry::tableOf(ObjectKind kind) const noexcept
{
    switch (kind)
    {
        case ObjectKind::Workspace: return _workspaces;
        case ObjectKind::Tab: return _tabs;
        case ObjectKind::Pane: break;
    }
    return _panes;
}

void ObjectRegistry::bindIn(Table& table, ObjectKind kind, uint64_t id, Uuid uuid)
{
    forgetIn(table, kind, id);
    // A UUID names one object: if it was bound to another id, that id loses it.
    if (auto const other = table.byUuid.find(uuid); other != table.byUuid.end())
        table.byId.erase(other->second);
    table.byId.insert_or_assign(id, uuid);
    table.byUuid.insert_or_assign(uuid, id);
}

void ObjectRegistry::forgetIn(Table& table, ObjectKind kind, uint64_t id)
{
    auto const known = table.byId.find(id);
    if (known == table.byId.end())
        return;
    _names.erase(ObjectRef { .kind = kind, .uuid = known->second });
    table.byUuid.erase(known->second);
    table.byId.erase(known);
}

std::optional<Uuid> ObjectRegistry::uuidIn(Table const& table, uint64_t id)
{
    if (auto const known = table.byId.find(id); known != table.byId.end())
        return known->second;
    return std::nullopt;
}

void ObjectRegistry::bind(vtworkspace::WorkspaceId id, Uuid uuid)
{
    bindIn(_workspaces, ObjectKind::Workspace, id.value, uuid);
}

void ObjectRegistry::bind(vtworkspace::TabId id, Uuid uuid)
{
    bindIn(_tabs, ObjectKind::Tab, id.value, uuid);
}

void ObjectRegistry::bind(vtworkspace::SessionId id, Uuid uuid)
{
    bindIn(_panes, ObjectKind::Pane, id.value, uuid);
}

void ObjectRegistry::forget(vtworkspace::WorkspaceId id)
{
    forgetIn(_workspaces, ObjectKind::Workspace, id.value);
}

void ObjectRegistry::forget(vtworkspace::TabId id)
{
    forgetIn(_tabs, ObjectKind::Tab, id.value);
}

void ObjectRegistry::forget(vtworkspace::SessionId id)
{
    forgetIn(_panes, ObjectKind::Pane, id.value);
}

std::optional<Uuid> ObjectRegistry::uuidOf(vtworkspace::WorkspaceId id) const
{
    return uuidIn(_workspaces, id.value);
}

std::optional<Uuid> ObjectRegistry::uuidOf(vtworkspace::TabId id) const
{
    return uuidIn(_tabs, id.value);
}

std::optional<Uuid> ObjectRegistry::uuidOf(vtworkspace::SessionId id) const
{
    return uuidIn(_panes, id.value);
}

std::optional<ObjectRegistry::Handle> ObjectRegistry::find(ObjectRef const& ref) const
{
    auto const& table = tableOf(ref.kind);
    auto const known = table.byUuid.find(ref.uuid);
    if (known == table.byUuid.end())
        return std::nullopt;
    switch (ref.kind)
    {
        case ObjectKind::Workspace: return Handle { vtworkspace::WorkspaceId { known->second } };
        case ObjectKind::Tab: return Handle { vtworkspace::TabId { known->second } };
        case ObjectKind::Pane: break;
    }
    return Handle { vtworkspace::SessionId { known->second } };
}

/// The name observers of a registry, by subscription id in subscription order.
struct ObjectRegistry::NameObservers
{
    std::vector<std::pair<uint64_t, NameObserver>> entries;
    uint64_t nextId = 1;
};

ObjectRegistry::Subscription::Subscription(std::weak_ptr<NameObservers> observers, uint64_t id) noexcept:
    _observers(std::move(observers)), _id(id)
{
}

ObjectRegistry::Subscription::Subscription(Subscription&& other) noexcept:
    _observers(std::move(other._observers)), _id(std::exchange(other._id, 0))
{
}

ObjectRegistry::Subscription& ObjectRegistry::Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other)
    {
        unsubscribe();
        _observers = std::move(other._observers);
        _id = std::exchange(other._id, 0);
    }
    return *this;
}

ObjectRegistry::Subscription::~Subscription()
{
    unsubscribe();
}

void ObjectRegistry::Subscription::unsubscribe() noexcept
{
    if (auto const observers = _observers.lock())
        std::erase_if(observers->entries, [this](auto const& entry) { return entry.first == _id; });
    _observers.reset();
}

ObjectRegistry::Subscription ObjectRegistry::onNameChanged(NameObserver observer)
{
    auto const id = _nameObservers->nextId++;
    _nameObservers->entries.emplace_back(id, std::move(observer));
    return Subscription { _nameObservers, id };
}

void ObjectRegistry::setName(ObjectRef const& ref, std::string name)
{
    // Every name is held as it is normalised once here, whoever sets it: no control character, and a blank
    // one cleared.
    name = vtworkspace::normalizedName(name);
    if (!find(ref) || nameOf(ref) == name)
        return;
    if (name.empty())
        _names.erase(ref);
    else
        _names.insert_or_assign(ref, std::move(name));
    // Who is told was settled before the first is: an observer may unsubscribe, or subscribe, as it is told.
    // Each is called through a copy, so one that unsubscribes itself is not destroyed while it runs.
    auto const ids = std::ranges::to<std::vector>(_nameObservers->entries | std::views::keys);
    auto const now = std::string { nameOf(ref) };
    for (auto const id: ids)
    {
        auto const& entries = _nameObservers->entries;
        auto const found =
            std::ranges::find_if(entries, [id](auto const& entry) { return entry.first == id; });
        if (found == entries.end())
            continue;
        auto const observer = found->second;
        observer(ref, now);
    }
}

std::string_view ObjectRegistry::nameOf(ObjectRef const& ref) const
{
    if (auto const known = _names.find(ref); known != _names.end())
        return known->second;
    return {};
}

namespace
{
    /// @return Every object @p byId binds, by id ascending.
    [[nodiscard]] std::vector<BoundObject> boundIn(std::unordered_map<uint64_t, Uuid> const& byId)
    {
        auto bound =
            std::ranges::to<std::vector>(byId | std::views::transform([](auto const& entry) {
                                             return BoundObject { .id = entry.first, .uuid = entry.second };
                                         }));
        std::ranges::sort(bound, {}, &BoundObject::id);
        return bound;
    }
} // namespace

ObjectRegistrySnapshot ObjectRegistry::snapshot() const
{
    return ObjectRegistrySnapshot {
        .workspaces = boundIn(_workspaces.byId),
        .tabs = boundIn(_tabs.byId),
        .panes = boundIn(_panes.byId),
        .names =
            std::ranges::to<std::vector>(_names | std::views::transform([](auto const& entry) {
                                             return NamedObject { .ref = entry.first, .name = entry.second };
                                         })),
    };
}

void ObjectRegistry::restore(ObjectRegistrySnapshot const& snapshot)
{
    _workspaces = {};
    _tabs = {};
    _panes = {};
    _names.clear();
    for (auto const& workspace: snapshot.workspaces)
        bind(vtworkspace::WorkspaceId { workspace.id }, workspace.uuid);
    for (auto const& tab: snapshot.tabs)
        bind(vtworkspace::TabId { tab.id }, tab.uuid);
    for (auto const& pane: snapshot.panes)
        bind(vtworkspace::SessionId { pane.id }, pane.uuid);
    // After the binds: a name of an object not bound is ignored.
    for (auto const& named: snapshot.names)
        setName(named.ref, named.name);
}

} // namespace vthost
