// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// What a client's EditTab does to the daemon's model: one row per TabEditKind, so a ninth kind is a row
/// and an enumerator. Pure over the model and where the tabs' names are held, so every row is testable
/// without a socket.

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include <vthost/ObjectRegistry.hpp>
#include <vthost/proto/Pdu.hpp>
#include <vtworkspace/SessionModel.hpp>

namespace vthost
{

/// Why an EditTab changed nothing.
enum class TabEditError : uint8_t
{
    UnknownSession = 1, ///< No pane hosts the session it names: it closed, or never existed.
    UnknownKind = 2,    ///< The kind is not one this host knows, as from a newer client.
};

/// @param error The refusal.
/// @return A lower-case phrase for a log line.
[[nodiscard]] constexpr std::string_view describe(TabEditError error) noexcept
{
    switch (error)
    {
        case TabEditError::UnknownSession: return "unknown session";
        case TabEditError::UnknownKind: return "unknown kind of tab edit";
    }
    return "unknown error";
}

/// Whether an applied edit moved the model.
enum class TabEditEffect : uint8_t
{
    Unchanged = 0, ///< The model held the value already; nothing was mutated, no event fired.
    Changed = 1,   ///< The model took the value, and its observers were told.
};

/// Where a tab's name is held, which SetName and ClearName change: the daemon's object registry, which
/// addresses a tab by it, or a window's own model.
class TabNames
{
  public:
    virtual ~TabNames() = default;

    /// @param tab The tab.
    /// @return Its name; empty for none.
    [[nodiscard]] virtual std::string nameOf(vtworkspace::TabId tab) const = 0;

    /// Names @p tab; an empty name clears it. Tells whoever watches the name, only when it changed.
    /// @param tab The tab.
    /// @param name The name.
    virtual void setName(vtworkspace::TabId tab, std::string name) = 0;
};

/// The names a model holds itself (vtworkspace::Tab::name()), as a window's model does.
class ModelTabNames final: public TabNames
{
  public:
    /// @param model The model, which outlives this.
    explicit ModelTabNames(vtworkspace::SessionModel& model) noexcept: _model { &model } {}

    [[nodiscard]] std::string nameOf(vtworkspace::TabId tab) const override;
    void setName(vtworkspace::TabId tab, std::string name) override;

  private:
    vtworkspace::SessionModel* _model;
};

/// The names a daemon's object registry holds, where a tab is addressed by its name.
class RegistryTabNames final: public TabNames
{
  public:
    /// @param objects The registry, which outlives this.
    explicit RegistryTabNames(ObjectRegistry& objects) noexcept: _objects { &objects } {}

    [[nodiscard]] std::string nameOf(vtworkspace::TabId tab) const override;
    void setName(vtworkspace::TabId tab, std::string name) override;

  private:
    ObjectRegistry* _objects;
};

/// Applies @p edit to @p model, and a name to @p names. Each kind compares before it mutates, so a client
/// re-asserting state it shares with the model costs no event, and so no layout push to any client.
/// @param model The daemon's model.
/// @param names Where the tabs' names are held.
/// @param edit The client's edit.
/// @return Whether the model moved, or why the edit was refused.
[[nodiscard]] std::expected<TabEditEffect, TabEditError> applyTabEdit(vtworkspace::SessionModel& model,
                                                                      TabNames& names,
                                                                      proto::EditTab const& edit);

} // namespace vthost
