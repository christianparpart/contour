// SPDX-License-Identifier: Apache-2.0
#include <contour/cli/ClientAttach.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

using contour::cli::launchIntent;
using vthost::proto::AttachIntentKind;

TEST_CASE("launchIntent: a launch lets the daemon pick, --workspace selects exactly one", "[cli][client]")
{
    CHECK(launchIntent(std::nullopt).kind == AttachIntentKind::Default);
    CHECK(launchIntent(std::nullopt).workspace == 0);
    CHECK(launchIntent(uint64_t { 7 }).kind == AttachIntentKind::Select);
    CHECK(launchIntent(uint64_t { 7 }).workspace == 7);
}

TEST_CASE("HostExclusions: --workspace may be given beside --host", "[cli][client]")
{
    CHECK(std::ranges::none_of(contour::cli::HostExclusions,
                               [](auto const& option) { return option.name == "workspace"; }));
    // Where and how to connect stays the host entry's to say.
    for (auto const name: { "connect-tcp", "socket", "tmux", "token", "token-file", "tls-ca" })
        CHECK(std::ranges::any_of(contour::cli::HostExclusions,
                                  [name](auto const& option) { return option.name == name; }));
}

TEST_CASE(
    "clientDaemonOf: --daemon says what --socket, --connect-tcp or --host said, or names a local daemon",
    "[cli][client]")
{
    using contour::cli::clientDaemonOf;
    using vthost::client::DaemonTarget;
    using vthost::client::DaemonWordKind;
    CHECK(clientDaemonOf(DaemonTarget { .kind = DaemonWordKind::SocketPath, .text = "/run/x" }).socket
          == "/run/x");
    CHECK(clientDaemonOf(DaemonTarget { .kind = DaemonWordKind::LocalName, .text = "work" }).name == "work");
    CHECK(clientDaemonOf(DaemonTarget { .kind = DaemonWordKind::HostPort, .text = "box:2222" }).connectTcp
          == "box:2222");
    CHECK(clientDaemonOf(DaemonTarget { .kind = DaemonWordKind::SshUrl, .text = "ssh://box" }).host
          == "ssh://box");
    CHECK(clientDaemonOf(DaemonTarget { .kind = DaemonWordKind::SavedHost, .text = "box" }).host == "box");
    // Each option that already says where the daemon is is refused beside --daemon.
    for (auto const* option: { "socket", "connect-tcp", "host" })
        CHECK(std::ranges::any_of(contour::cli::DaemonExclusions,
                                  [option](auto const& excluded) { return excluded.name == option; }));
}
