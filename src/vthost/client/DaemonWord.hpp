// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// What a `--daemon` word names: a socket path, `HOST:PORT`, a URL, or a bare word, which is a local daemon's
/// name or a saved host's. The decision alone, with what it looks up injected, so that every spelling is
/// table-testable without a daemon or a configuration.

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace vthost::client
{

/// How a `--daemon` word resolves. Rows in order; first match wins.
enum class DaemonWordKind : uint8_t
{
    SocketPath = 0, ///< A control socket's path: one starting with `/` or `.`, or holding `/`, `\` or `:`.
    HostPort,  ///< A TCP daemon's `HOST:PORT` (or `[v6]:PORT`); `host:2222` and `name:22` too, by their port.
    SshUrl,    ///< A URL such as `ssh://box`: anything holding `://`.
    LocalName, ///< A local daemon by its name: `name:X`, or a bare word naming a local daemon.
    SavedHost, ///< A saved host by its name: `host:X`, or any other bare word.
};

/// What a `--daemon` word names.
struct DaemonTarget
{
    DaemonWordKind kind = DaemonWordKind::SocketPath; ///< How the word reads.
    /// The path, `HOST:PORT`, URL or name the word names; a `name:` or `host:` prefix is not part of it.
    std::string text {};
};

/// Why a `--daemon` word names no daemon.
enum class DaemonWordError : uint8_t
{
    Ambiguous = 0, ///< A bare word that names both a local daemon and a saved host.
    EmptyName,     ///< `name:` or `host:` with nothing after it.
};

/// The local daemons a bare word may name; injected, so that resolving one reads no socket directory.
struct LocalDaemons
{
    /// Whether a local daemon of a name is there to talk to; empty for none.
    std::function<bool(std::string_view name)> contains {};
};

/// The saved hosts a bare word may name; injected, so that resolving one reads no configuration.
struct SavedHosts
{
    /// Whether a saved host of a name is configured; empty for none.
    std::function<bool(std::string_view name)> contains {};
};

/// Reads a `--daemon` word. A bare word is a local daemon's name, else a saved host's; one naming both is
/// refused, as is a word with an empty name. A bare word naming neither is a saved host, whose resolver then
/// says that it knows none of that name.
/// @param word What `--daemon` (or `$CONTOUR_DAEMON`) says.
/// @param localDaemons The local daemons a bare word may name.
/// @param savedHosts The saved hosts a bare word may name.
/// @return What the word names, or why it names nothing.
[[nodiscard]] std::expected<DaemonTarget, DaemonWordError> resolveDaemonWord(std::string_view word,
                                                                             LocalDaemons const& localDaemons,
                                                                             SavedHosts const& savedHosts);

/// @param error Why @p word names no daemon.
/// @param word The `--daemon` word.
/// @return A sentence for the user; for an ambiguous word it names the spellings that pick each.
[[nodiscard]] std::string describe(DaemonWordError error, std::string_view word);

} // namespace vthost::client
