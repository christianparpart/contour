// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// What `contour client` attaches with, whichever daemon it reaches -- the local one, a saved host, a TLS
/// endpoint. Pure, so that each rule is tested without a daemon.

#include <contour/cli/ContourApp.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <vthost/client/DaemonWord.hpp>
#include <vthost/proto/Pdu.hpp>

namespace contour::cli
{

/// The `contour client` options that are silently meaningless beside `--host`, whose entry says where and how
/// to connect; given together, the client refuses. `--workspace` is not among them: it names what to show
/// there, as it does locally.
inline constexpr auto HostExclusions = std::array {
    OptionPresence { .name = "connect-tcp", .givenAs = GivenAs::Text },
    OptionPresence { .name = "socket", .givenAs = GivenAs::Text },
    OptionPresence { .name = "tmux", .givenAs = GivenAs::Flag },
    OptionPresence { .name = "token", .givenAs = GivenAs::Text },
    OptionPresence { .name = "token-file", .givenAs = GivenAs::Text },
    OptionPresence { .name = "tls-ca", .givenAs = GivenAs::Text },
};

/// The `contour client` options that say where the daemon is, as `--daemon` does; given beside it, the client
/// refuses.
inline constexpr auto DaemonExclusions = std::array {
    OptionPresence { .name = "connect-tcp", .givenAs = GivenAs::Text },
    OptionPresence { .name = "host", .givenAs = GivenAs::Text },
    OptionPresence { .name = "socket", .givenAs = GivenAs::Text },
};

/// Where `contour client --daemon` says the daemon is, spelled as the option that says only that.
struct ClientDaemon
{
    std::string socket {};     ///< `--socket`: a control socket's path; empty for none.
    std::string name {};       ///< A local daemon's name; empty for none.
    std::string connectTcp {}; ///< `--connect-tcp`: a TCP daemon's `HOST:PORT`; empty for none.
    std::string host {};       ///< `--host`: a saved host's name or an ssh:// URL; empty for none.
};

/// @param target What the `--daemon` word names (vthost::client::resolveDaemonWord()).
/// @return The one field of it that says so.
[[nodiscard]] inline ClientDaemon clientDaemonOf(vthost::client::DaemonTarget const& target)
{
    using vthost::client::DaemonWordKind;
    switch (target.kind)
    {
        case DaemonWordKind::SocketPath: return ClientDaemon { .socket = target.text };
        case DaemonWordKind::LocalName: return ClientDaemon { .name = target.text };
        case DaemonWordKind::HostPort: return ClientDaemon { .connectTcp = target.text };
        case DaemonWordKind::SshUrl:
        case DaemonWordKind::SavedHost: return ClientDaemon { .host = target.text };
    }
    return ClientDaemon {};
}

/// What a launch's first connection selects, on any daemon: exactly the workspace `--workspace` names, else
/// whichever the daemon picks (AttachIntentKind::Default).
/// @param workspace `--workspace`: the daemon workspace to show; none for a plain launch.
/// @return The attach intent.
[[nodiscard]] inline vthost::proto::AttachIntent launchIntent(std::optional<uint64_t> workspace)
{
    if (workspace)
        return vthost::proto::AttachIntent { .kind = vthost::proto::AttachIntentKind::Select,
                                             .workspace = *workspace };
    return vthost::proto::AttachIntent { .kind = vthost::proto::AttachIntentKind::Default, .workspace = 0 };
}

} // namespace contour::cli
