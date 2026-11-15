// SPDX-License-Identifier: Apache-2.0
#pragma once
/// @file
/// compat(0.9.0): the shapes a client of the old spelling decodes, as they were before the rename, so a test
/// can decode a reply as strictly as an old window does. In a header, in a named namespace: glaze reflects a
/// type through a variable template that needs the type to have linkage.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <vtcontrol/Api.hpp>

namespace vtcontrol::testing::legacyspelling
{

/// A pane, as `workspace.list` spelled it before the rename.
struct LegacyPaneInfo
{
    std::string id {};                                          ///< Its reference.
    std::string label {};                                       ///< Its name, under the old key.
    std::string title {};                                       ///< Its title.
    std::string cwd {};                                         ///< Its working directory.
    bool isActive = false;                                      ///< Whether it is its tab's active pane.
    vtcontrol::Size size {};                                    ///< Its size.
    std::vector<vtcontrol::ProgramStatusInfo> programStatus {}; ///< Its program status.
    std::optional<vtcontrol::BlockInfo> lastFinished {};        ///< Its last finished command block.
};

/// A tab, as `workspace.list` spelled it before the rename.
struct LegacyTabInfo
{
    std::string id {};                    ///< Its reference.
    std::string label {};                 ///< Its name, under the old key.
    std::string title {};                 ///< Its title.
    uint32_t position = 0;                ///< Its position.
    bool isActive = false;                ///< Whether it is its workspace's active tab.
    std::vector<LegacyPaneInfo> panes {}; ///< Its panes.
};

/// A workspace, as `workspace.list` spelled it before the rename.
struct LegacyWorkspaceInfo
{
    std::string id {};                                 ///< Its reference.
    std::string label {};                              ///< Its name, under the old key.
    uint32_t position = 0;                             ///< Its position.
    std::string native {};                             ///< Its native id.
    std::string visibility {};                         ///< Its visibility.
    uint32_t clients = 0;                              ///< How many clients show it.
    std::vector<LegacyTabInfo> tabs {};                ///< Its tabs.
    std::optional<vtcontrol::WorkspaceGitInfo> git {}; ///< Its git state.
};

/// A `workspace.list` reply, as spelled before the rename.
struct LegacyListReply
{
    std::vector<LegacyWorkspaceInfo> workspaces {}; ///< The workspaces.
};

} // namespace vtcontrol::testing::legacyspelling
