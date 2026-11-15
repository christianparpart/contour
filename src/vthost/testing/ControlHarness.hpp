// SPDX-License-Identifier: Apache-2.0
//
// Test-only: a SessionHost over MockPty sessions with no pump threads, and a control service over it.
#pragma once

#include <vtbackend/screen/Settings.hpp>
#include <vtbackend/screen/Terminal.hpp>

#include <vtpty/MockPty.hpp>
#include <vtpty/Process.hpp>

#include <core/Environment.hpp>
#include <core/net/EventLoop.hpp>
#include <core/net/testing/ScriptedBackend.hpp>
#include <core/platform/Clock.hpp>
#include <core/platform/testing/InMemoryFileSystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <vtcontrol/Api.hpp>
#include <vtcontrol/ObjectRef.hpp>
#include <vtcontrol/testing/FakeControlApi.hpp>
#include <vtcontrol/testing/SequentialUuidSource.hpp>
#include <vthost/SessionHost.hpp>
#include <vthost/WorkspaceGit.hpp>
#include <vthost/control/ControlService.hpp>
#include <vtworkspace/Pane.hpp>
#include <vtworkspace/Tab.hpp>

namespace vthost::testing
{

/// A host over MockPty sessions with no pump threads, and a control service over it.
struct ControlHarness
{
    /// The service's reply cap when a test names none.
    static constexpr auto DefaultMaxReplyBytes = std::size_t { 1 } * 1024 * 1024;

    /// @param maxReplyBytes The service's reply cap.
    explicit ControlHarness(std::size_t maxReplyBytes = DefaultMaxReplyBytes):
        ControlHarness { std::nullopt, maxReplyBytes }
    {
    }

    /// @param git Where the host looks its workspaces' git state up.
    explicit ControlHarness(vthost::WorkspaceGitServices git): ControlHarness { git, DefaultMaxReplyBytes } {}

    /// @param git Where the host looks its workspaces' git state up; none looks nothing up.
    /// @param maxReplyBytes The service's reply cap.
    ControlHarness(std::optional<vthost::WorkspaceGitServices> git, std::size_t maxReplyBytes):
        workspaceGit { std::move(git) },
        service { host,
                  vthost::control::ControlServiceConfig { .version = "9.8.7",
                                                          .socketPath = "/run/test.sock",
                                                          .clock = wall,
                                                          .maxReplyBytes = maxReplyBytes } }
    {
    }

    core::net::testing::ScriptedBackend source;
    core::net::EventLoop loop { source };
    core::platform::ManualWallClock wall { std::chrono::system_clock::time_point {
        std::chrono::seconds { 1'800'000'000 } } };
    /// The directories the host finds on disk, so an inherited one need not exist on the machine.
    core::platform::testing::InMemoryFileSystem files;
    /// The override every spawn was given, in spawn order.
    std::vector<vtpty::Process::ExecInfo> seen;
    /// Where the host looks its workspaces' git state up; none looks nothing up.
    std::optional<vthost::WorkspaceGitServices> workspaceGit {};
    vthost::SessionHost host {
        loop,
        [this](vtbackend::PageSize size, std::optional<vtpty::Process::ExecInfo> const& spawn) {
            seen.push_back(spawn.value_or(vtpty::Process::ExecInfo {}));
            return std::make_unique<vtpty::MockPty>(size);
        },
        vtbackend::Settings {},
        core::defaultEnvironment(),
        vthost::SessionHostConfig { .pumping = vthost::SessionPumping::Manual,
                                    .fileSystem = files,
                                    .workspaceGit = workspaceGit,
                                    .uuids = std::make_shared<vtcontrol::testing::SequentialUuidSource>() }
    };
    control::ControlService service;
    vtcontrol::testing::FakeCallContext context;

    /// Opens a tab in the host's workspace.
    /// @return The session of its pane.
    vtworkspace::SessionId openTab()
    {
        auto* tab = host.createTab();
        REQUIRE(tab != nullptr);
        return tab->rootPane()->session();
    }

    /// @return The terminal of @p session.
    vtbackend::Terminal& terminal(vtworkspace::SessionId session)
    {
        auto* found = host.terminal(session);
        REQUIRE(found != nullptr);
        return *found;
    }

    /// @return The API id of @p session's pane.
    [[nodiscard]] std::string paneId(vtworkspace::SessionId session) const
    {
        return vtcontrol::toString(vtcontrol::ObjectRef { .kind = vtcontrol::ObjectKind::Pane,
                                                          .uuid = *host.objects().uuidOf(session) });
    }

    /// Runs the loop until nothing is left to do: what a session's parser posted reaches the observers.
    void drain() { std::ignore = loop.runUntilIdle(); }
};

/// @param host The host.
/// @param tab One of its tabs.
/// @return The name @p host's object registry holds for @p tab; empty for none.
[[nodiscard]] inline std::string registryNameOf(vthost::SessionHost const& host, vtworkspace::Tab const& tab)
{
    auto const uuid = host.objects().uuidOf(tab.id());
    REQUIRE(uuid.has_value());
    return std::string { host.objects().nameOf({ .kind = vtcontrol::ObjectKind::Tab, .uuid = *uuid }) };
}

/// Renames, through a control service over @p host, the object @p address names (`tab:build`, say).
/// @param host The host; its loop must be driven by the calling thread.
/// @param address The object's address, as a control client spells it.
/// @param name Its new name.
/// @return Whether the service resolved @p address and took the name.
[[nodiscard]] inline bool renameByAddress(vthost::SessionHost& host,
                                          std::string const& address,
                                          std::string const& name)
{
    auto wall = core::platform::ManualWallClock { std::chrono::system_clock::time_point {} };
    auto service = control::ControlService {
        host,
        control::ControlServiceConfig { .version = "test", .socketPath = "/run/test.sock", .clock = wall }
    };
    auto context = vtcontrol::testing::FakeCallContext {};
    auto reply = std::optional<std::expected<vtcontrol::EmptyReply, vtcontrol::ControlError>> {};
    service.handle(context,
                   vtcontrol::SetLabelRequest { .object = address, .label = name },
                   [&](std::expected<vtcontrol::EmptyReply, vtcontrol::ControlError> answer) {
                       reply = std::move(answer);
                   });
    return reply.has_value() && reply->has_value();
}

} // namespace vthost::testing
