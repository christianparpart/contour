// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vtworkspace
{

/// What names a tab. Every place that names one (the tab strip, the sidebar, the switcher, the status line)
/// asks tabNameSourceOf() which of these wins, so they cannot come to disagree.
enum class TabNameSource : uint8_t
{
    Panes, ///< Its panes, as the place naming the tab names a tab after them (a title, a template).
    /// compat(0.9.0): its own title, as what states the tab gives one: an older daemon states its runtime
    /// title.
    Title,
    Name, ///< Its user-given name, set from the GUI and the control API alike.
};

/// What a tab could be named by besides its panes; an empty one names nothing.
struct TabNameCandidates
{
    std::string_view name {};  ///< Its user-given name.
    std::string_view title {}; ///< Its own title, where what states the tab gives it one.
};

/// One source that names a tab over its panes, and where its text is.
struct TabNameRule
{
    TabNameSource source;                       ///< The source.
    std::string_view TabNameCandidates::* text; ///< Its text among the candidates.
};

/// The sources that name a tab over its panes, in precedence order: the first that names something wins, and
/// where none does, its panes name it. A new source is a row here.
constexpr auto TabNamePrecedence = std::array {
    TabNameRule { .source = TabNameSource::Name, .text = &TabNameCandidates::name },
    TabNameRule { .source = TabNameSource::Title, .text = &TabNameCandidates::title }, // compat(0.9.0)
};

/// @param candidates What the tab could be named by besides its panes.
/// @return The source that names the tab: a name over a title, either over its panes.
[[nodiscard]] constexpr TabNameSource tabNameSourceOf(TabNameCandidates const& candidates) noexcept
{
    for (auto const& rule: TabNamePrecedence)
        if (!(candidates.*rule.text).empty())
            return rule.source;
    return TabNameSource::Panes;
}

/// @param candidates What the tab could be named by besides its panes.
/// @param panes What names the tab after its panes, where nothing else does.
/// @return The tab's name, from the source tabNameSourceOf() picks.
[[nodiscard]] constexpr std::string_view tabNameOf(TabNameCandidates const& candidates,
                                                   std::string_view panes) noexcept
{
    for (auto const& rule: TabNamePrecedence)
        if (!(candidates.*rule.text).empty())
            return candidates.*rule.text;
    return panes;
}

/// compat(0.9.0): before 0.9.0 a tab renamed from the GUI kept that rename as a runtime title beside its
/// name. A handover image or a snapshot written then still carries it; restored, it becomes the name the tab
/// lacks.
/// @param name The name the tab was captured with; empty for none.
/// @param runtimeTitle The runtime title it was captured with, if any.
/// @return The name the tab is restored with: @p name, else @p runtimeTitle, else none.
[[nodiscard]] constexpr std::string_view restoredTabName(
    std::string_view name, std::optional<std::string> const& runtimeTitle) noexcept
{
    if (!name.empty() || !runtimeTitle)
        return name;
    return *runtimeTitle;
}

} // namespace vtworkspace
