// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Derivation of the daemon's control-socket path, following tmux's shape:
/// a per-user runtime directory holding one socket file per name.

#include <vtpty/SandboxInfo.hpp>

#include <core/Environment.hpp>
#include <core/Utf8Path.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#ifndef _WIN32
    #include <unistd.h>
#endif

namespace vthost
{

/// The name a daemon serves, and a client dials, when given none: the one source of it.
inline constexpr auto DefaultDaemonName = std::string_view { "default" };

/// Whether a daemon name was given by the user or is merely the default.
///
/// Decides who wins between the name and `$CONTOUR_MUX`: a name the user spelled out is an explicit
/// choice and beats the environment (as tmux's `-L` beats `$TMUX`), whereas the default name is
/// what nobody chose and yields to the socket of the daemon that hosts the calling shell.
enum class DaemonNameOrigin : uint8_t
{
    Default = 0, ///< Nobody gave a name; DefaultDaemonName applies, and `$CONTOUR_MUX` outranks it.
    Explicit,    ///< The user passed a name, whatever its value (`default` included).
};

/// A daemon name together with how it came about.
struct DaemonName
{
    /// The daemon's name; DefaultDaemonName for the unnamed one.
    std::string name = std::string { DefaultDaemonName };

    /// Whether the user named it.
    DaemonNameOrigin origin = DaemonNameOrigin::Default;
};

/// Reads a `--label` option whose declared default is empty, so that "not given" is distinguishable
/// from "given as `default`" (the option parser reports defaults and given values alike).
/// @param option The option's value; empty when the user did not pass it.
/// @return The name it names, Explicit when @p option is non-empty, else the default name.
[[nodiscard]] inline DaemonName daemonNameFromOption(std::string_view option)
{
    if (option.empty())
        return DaemonName {};
    return DaemonName { .name = std::string { option }, .origin = DaemonNameOrigin::Explicit };
}

/// The inputs a control-socket path is derived from.
///
/// A struct rather than six positional parameters: `sandboxAppId` and `user` are both
/// std::string_view and adjacent, so nothing but argument order tells them apart — the same
/// silently-exchangeable-parameters problem AGENT.md names for bools, and one the compiler cannot
/// diagnose. Designated initializers put the name back at the call site.
///
/// Every field carries a default, so a call site names only what it is actually varying: this tree
/// builds with -Wmissing-designated-field-initializers under -Werror, which would otherwise make a
/// partial designated initializer a build error and cost the form its whole point.
struct MuxSocketPathInputs
{
    /// The daemon's name (tmux's `-L`); DefaultDaemonName for the unnamed one.
    std::string_view name = DefaultDaemonName;

    /// Whether @c name was given by the user; an Explicit name outranks `$CONTOUR_MUX`.
    DaemonNameOrigin nameOrigin = DaemonNameOrigin::Default;

    /// A user-supplied path override (the `--socket PATH` flag), or empty.
    std::string_view explicitPath = {};

    /// The value of `$CONTOUR_MUX`, if set.
    std::optional<std::string_view> contourMuxEnv = {};

    /// The value of `$XDG_RUNTIME_DIR`, if set.
    std::optional<std::string_view> xdgRuntimeDir = {};

    /// The sandbox's own name for this application, or empty when not sandboxed.
    std::string_view sandboxAppId = {};

    /// What identifies this user in the fallback directory's name: the numeric uid on POSIX,
    /// `$USERNAME` on Windows.
    std::string_view user = {};
};

/// The pure derivation core: resolves the control-socket path from explicitly passed inputs. Every
/// input is a field — the user identity of the fallback included — so this is fully deterministic
/// and a test drives it without touching the environment or the host it happens to run on.
///
/// Precedence mirrors tmux's socket discovery, adapted to XDG conventions:
///  1. @c explicitPath (the `--socket PATH` flag) verbatim, if non-empty;
///  2. a name the user gave (@c nameOrigin Explicit), derived by rungs 4 to 6: an explicit `-L` beats
///     `$TMUX` in tmux, and `--label` beats `$CONTOUR_MUX` here. Without that, a shell inside a
///     daemon-hosted pane could never address a second daemon;
///  3. @c contourMuxEnv verbatim, if set and non-empty (the `$CONTOUR_MUX` override, analogous to tmux's
///     `$TMUX` naming the active server's socket), when no name was given;
///  4. `<xdgRuntimeDir>/app/<sandboxAppId>/contour/<name>` when sandboxed;
///  5. `<xdgRuntimeDir>/contour/<name>`;
///  6. `<temp>/contour-<user>/<name>` as the fallback when no runtime dir exists, or the one set is
///     relative (tmux's `/tmp/tmux-<uid>/<name>` shape).
///
/// Rungs 4 and 5 are vtpty::sharedRuntimeDirectory(), which the pager's private files share.
///
/// Rung 4 exists because inside a Flatpak sandbox `XDG_RUNTIME_DIR` is `/run/user/<uid>` — a
/// directory created inside the sandbox's own mount namespace, NOT the host's. A socket bound at
/// rung 5 from inside the sandbox is therefore not merely at a different path than the host would
/// derive; it is on a private mount the host cannot reach at all, so `contour client` run from a
/// shell that escaped the sandbox finds nothing and can never find anything.
/// `$XDG_RUNTIME_DIR/app/<app-id>` is the one directory flatpak documents as both writable from
/// inside and shared with the host, and since `XDG_RUNTIME_DIR` is `/run/user/<uid>` on both sides,
/// it is the same absolute path in both worlds.
/// @see https://github.com/contour-terminal/contour/issues/2075
///
/// Note that it only makes the two SANDBOXED derivations agree. A `contour` installed on the host
/// is not sandboxed and still derives rung 5, which is why the sandboxed daemon also exports
/// `$CONTOUR_MUX` — rung 3 — into the shells it hosts. A sandboxed shell running `contour client` with
/// no `--label` therefore still finds its own daemon; only a name it spells out redirects it.
///
/// The parent directory is NOT created here; the listener's bind path hardens
/// and creates it (see core::net::ensureOwnedPrivateDirectory).
/// @param inputs What the path is derived from.
/// @return The resolved socket file path.
[[nodiscard]] inline std::filesystem::path muxSocketPath(MuxSocketPathInputs const& inputs)
{
    namespace fs = std::filesystem;

    auto const fromUtf8 = [](std::string_view utf8) {
        return core::pathFromUtf8(utf8).value_or(fs::path {});
    };

    if (!inputs.explicitPath.empty())
        return fromUtf8(inputs.explicitPath);

    if (inputs.nameOrigin == DaemonNameOrigin::Default && inputs.contourMuxEnv
        && !inputs.contourMuxEnv->empty())
        return fromUtf8(*inputs.contourMuxEnv);

    if (auto const runtime = vtpty::sharedRuntimeDirectory(inputs.xdgRuntimeDir, inputs.sandboxAppId))
        return *runtime / "contour" / fromUtf8(inputs.name);

    return fs::temp_directory_path() / fromUtf8("contour-" + std::string { inputs.user })
           / fromUtf8(inputs.name);
}

namespace detail
{
    /// muxSocketPath() from an environment, with `$CONTOUR_MUX` read by the caller.
    /// @param contourMux The value of `$CONTOUR_MUX` to honour; none to ignore it.
    [[nodiscard]] inline std::filesystem::path muxSocketPathOf(DaemonName const& daemon,
                                                               std::string_view explicitPath,
                                                               std::optional<std::string> const& contourMux,
                                                               core::Environment const& env,
                                                               vtpty::SandboxInfo const& sandbox);
} // namespace detail

/// Production entry point: derives the path from a process environment and the sandbox this process
/// runs in.
/// @param daemon The daemon's name and whether the user named it (@see daemonNameFromOption).
/// @param explicitPath A user-supplied path override, or empty.
/// @param env The environment to read `$CONTOUR_MUX`, `$XDG_RUNTIME_DIR` and (on
///            Windows) `$USERNAME` from.
/// @param sandbox Where this process runs. Injected for the same reason @p env is, and separate
///                from it because the sandbox describes itself in `/.flatpak-info` — a fact no
///                environment variable can be trusted to reflect.
/// @return The resolved socket file path.
[[nodiscard]] inline std::filesystem::path muxSocketPath(
    DaemonName const& daemon = {},
    std::string_view explicitPath = {},
    core::Environment const& env = core::defaultEnvironment(),
    vtpty::SandboxInfo const& sandbox = vtpty::currentSandbox())
{
    return detail::muxSocketPathOf(daemon, explicitPath, env.get("CONTOUR_MUX"), env, sandbox);
}

/// Where a daemon serving @p name listens when nothing names its socket: neither `--socket` nor
/// `$CONTOUR_MUX`. What tells a daemon at a socket of its own apart from the one its name would run.
/// @param name The daemon's name.
/// @param env The environment to read `$XDG_RUNTIME_DIR` and (on Windows) `$USERNAME` from.
/// @param sandbox Where this process runs.
/// @return The name's own socket path.
[[nodiscard]] inline std::filesystem::path namedSocketPath(
    std::string_view name,
    core::Environment const& env = core::defaultEnvironment(),
    vtpty::SandboxInfo const& sandbox = vtpty::currentSandbox())
{
    return detail::muxSocketPathOf(
        DaemonName { .name = std::string { name } }, {}, std::nullopt, env, sandbox);
}

/// The directory the sockets of the named daemons live in, which is where discovery looks for them.
/// @param env The environment to read `$XDG_RUNTIME_DIR` and (on Windows) `$USERNAME` from.
/// @param sandbox Where this process runs.
/// @return The directory holding the default name's socket, and so every name's.
[[nodiscard]] inline std::filesystem::path namedSocketDirectory(
    core::Environment const& env = core::defaultEnvironment(),
    vtpty::SandboxInfo const& sandbox = vtpty::currentSandbox())
{
    return namedSocketPath(DefaultDaemonName, env, sandbox).parent_path();
}

[[nodiscard]] inline std::filesystem::path detail::muxSocketPathOf(
    DaemonName const& daemon,
    std::string_view explicitPath,
    std::optional<std::string> const& contourMux,
    core::Environment const& env,
    vtpty::SandboxInfo const& sandbox)
{
    // Held in named locals, because the pure core above views them rather than owning them.
    auto const xdgRuntimeDir = env.get("XDG_RUNTIME_DIR");
    auto const asView = [](std::string const& value) {
        return std::string_view { value };
    };

#ifndef _WIN32
    auto const user = std::to_string(::getuid());
#else
    auto const user = env.get("USERNAME").value_or("user");
#endif

    return muxSocketPath(MuxSocketPathInputs { .name = daemon.name,
                                               .nameOrigin = daemon.origin,
                                               .explicitPath = explicitPath,
                                               .contourMuxEnv = contourMux.transform(asView),
                                               .xdgRuntimeDir = xdgRuntimeDir.transform(asView),
                                               .sandboxAppId = sandbox.applicationId,
                                               .user = user });
}

// The daemon's sibling endpoints derive from the control-socket path by
// suffix. These helpers are the ONLY place the convention lives; every
// consumer (GUI, POSIX daemon, Win32 daemon, attach flows) derives through
// them.

/// The suffix of the native cells+deltas endpoint beside a control socket.
inline constexpr auto NativeSocketSuffix = std::string_view { "-native" };

/// The suffix of the binary-tmux (imsg) endpoint beside a control socket.
inline constexpr auto TmuxSocketSuffix = std::string_view { "-tmux" };

/// The suffix of the log file beside a control socket.
inline constexpr auto DaemonLogSuffix = std::string_view { ".log" };

/// Every suffix a file beside a control socket carries: what tells such a sibling from a control socket.
inline constexpr auto SiblingSuffixes =
    std::to_array({ NativeSocketSuffix, TmuxSocketSuffix, DaemonLogSuffix });

/// The longest daemon name a user may give: the name is the socket's file name, and a Unix socket's whole
/// path must fit `sun_path` (108 bytes on Linux, 104 on macOS) with the runtime directory and a sibling
/// suffix beside it.
inline constexpr auto MaxDaemonNameLength = std::size_t { 64 };

/// Why a text cannot be the name a daemon is started under.
enum class DaemonNameError : uint8_t
{
    Empty = 0,        ///< Nothing was named.
    TooLong,          ///< Longer than MaxDaemonNameLength.
    LeadingDot,       ///< Starts with a dot: `.`, `..` or a hidden file, none of which discovery lists.
    InvalidCharacter, ///< Holds something other than ASCII letters, digits, `-`, `_` and `.`.
    SiblingSuffix,    ///< Ends in a sibling's suffix (`-native`, `-tmux`, `.log`), which discovery skips.
};

/// @param error Why a name was refused.
/// @return The reason, for a person: the inline error under the name field.
[[nodiscard]] inline std::string describe(DaemonNameError error)
{
    switch (error)
    {
        case DaemonNameError::Empty: return "Type a label for the daemon.";
        case DaemonNameError::TooLong:
            return std::format("A label has at most {} characters.", MaxDaemonNameLength);
        case DaemonNameError::LeadingDot: return "A label cannot start with a dot.";
        case DaemonNameError::InvalidCharacter:
            return R"(A label may hold only letters, digits, "-", "_" and ".".)";
        case DaemonNameError::SiblingSuffix: return R"(A label cannot end in "-native", "-tmux" or ".log".)";
    }
    return "This label cannot be used.";
}

/// Checks that @p name can name a daemon of its own: its control socket is a file of that name in the
/// runtime directory, which local discovery lists (sibling files and hidden ones skipped).
/// daemonNameFromOption() reads any `--label` the user passes; a name the GUI asks the user for is held to
/// these rules first.
/// @param name The name asked for.
/// @return Nothing when it can; why not otherwise.
[[nodiscard]] constexpr std::expected<void, DaemonNameError> validateDaemonName(std::string_view name)
{
    if (name.empty())
        return std::unexpected(DaemonNameError::Empty);
    if (name.size() > MaxDaemonNameLength)
        return std::unexpected(DaemonNameError::TooLong);
    if (name.front() == '.')
        return std::unexpected(DaemonNameError::LeadingDot);
    auto const isNameCharacter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
               || c == '_' || c == '.';
    };
    if (!std::ranges::all_of(name, isNameCharacter))
        return std::unexpected(DaemonNameError::InvalidCharacter);
    if (std::ranges::any_of(SiblingSuffixes,
                            [name](std::string_view suffix) { return name.ends_with(suffix); }))
        return std::unexpected(DaemonNameError::SiblingSuffix);
    return {};
}

/// @return The native cells+deltas endpoint beside @p controlSocket.
[[nodiscard]] inline std::filesystem::path nativeSocketPath(std::filesystem::path controlSocket)
{
    controlSocket += NativeSocketSuffix;
    return controlSocket;
}

/// @return The binary-tmux (imsg) endpoint beside @p controlSocket.
[[nodiscard]] inline std::filesystem::path tmuxSocketPath(std::filesystem::path controlSocket)
{
    controlSocket += TmuxSocketSuffix;
    return controlSocket;
}

/// The default log file for a daemon that has no console to write to.
///
/// A backgrounded or service-hosted daemon is detached from any terminal, so its standard
/// error goes nowhere and an unconfigured one becomes the instance nobody can diagnose. It
/// lands beside the socket for the same reason the socket is where it is: one place per
/// name, derived rather than remembered.
/// @param controlSocket The daemon's control-socket path.
/// @return The log file path beside @p controlSocket.
[[nodiscard]] inline std::filesystem::path daemonLogPath(std::filesystem::path controlSocket)
{
    controlSocket += DaemonLogSuffix;
    return controlSocket;
}

} // namespace vthost
