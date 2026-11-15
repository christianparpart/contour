// SPDX-License-Identifier: Apache-2.0
#include <vtpty/test/NonAsciiSample.hpp>

#include <core/testing/Environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <vthost/HostedShell.hpp>
#include <vthost/SocketPath.hpp>

using vthost::DaemonName;
using vthost::DaemonNameOrigin;
using vthost::muxSocketPath;
using vthost::MuxSocketPathInputs;

namespace
{
/// Stands in for whatever identifies the user on the host running the suite, so the fallback
/// branch's expectations are exact rather than "contains something".
constexpr auto TestUser = "1000";

/// A sandboxed application's own id, as /.flatpak-info reports it. Off a sandbox the field is
/// simply left out, which is what makes the designated-initializer form worth the struct.
constexpr auto SandboxedAs = "org.contourterminal.Contour";
} // namespace

TEST_CASE("an explicit --socket path wins over everything", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs { .explicitPath = "/tmp/explicit.sock",
                                              .contourMuxEnv = "/env/mux",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .user = TestUser })
          == "/tmp/explicit.sock");
}

TEST_CASE("the socket path is read as UTF-8, as the command line and the environment are",
          "[vthost][socketpath]")
{
    constexpr auto Awkward = vtpty::testing::NonAsciiName;
    auto const awkward = std::string { vtpty::testing::NonAsciiNameUtf8 };

    auto const explicitPath = "/tmp/" + awkward;
    CHECK(muxSocketPath(MuxSocketPathInputs { .explicitPath = explicitPath, .user = TestUser })
          == std::filesystem::path { u8"/tmp/" } / Awkward);

    // GCC's link-time inlining reports a -Wstringop-overflow for the natural form of this (a string
    // temporary joined with `+`) that cannot happen, so it is built in pieces.
    auto labelledDirectory = std::u8string { u8"contour-" };
    labelledDirectory.append(Awkward);
    CHECK(muxSocketPath(MuxSocketPathInputs { .name = awkward, .user = awkward })
          == std::filesystem::temp_directory_path() / labelledDirectory / Awkward);
}

TEST_CASE("$CONTOUR_MUX overrides the derived path", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs {
              .contourMuxEnv = "/env/mux.sock", .xdgRuntimeDir = "/run/user/1000", .user = TestUser })
          == "/env/mux.sock");
}

TEST_CASE("the XDG runtime dir hosts the per-label socket", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs { .xdgRuntimeDir = "/run/user/1000", .user = TestUser })
          == "/run/user/1000/contour/default");
    CHECK(muxSocketPath(
              MuxSocketPathInputs { .name = "work", .xdgRuntimeDir = "/run/user/1000", .user = TestUser })
          == "/run/user/1000/contour/work");
}

TEST_CASE("a sandboxed derivation lands where both sides of the sandbox can see it", "[vthost][socketpath]")
{
    // Inside a Flatpak, XDG_RUNTIME_DIR is a directory created in the sandbox's OWN mount namespace,
    // so a socket bound directly under it is unreachable from the host at any path -- and a shell
    // that escaped the sandbox is exactly what runs `contour client`. $XDG_RUNTIME_DIR/app/<id> is
    // the one place flatpak shares between the two, at the same absolute path in both.
    // @see issue #2075.
    CHECK(muxSocketPath(MuxSocketPathInputs {
              .xdgRuntimeDir = "/run/user/1000", .sandboxAppId = SandboxedAs, .user = TestUser })
          == "/run/user/1000/app/org.contourterminal.Contour/contour/default");
    CHECK(muxSocketPath(MuxSocketPathInputs { .name = "work",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .sandboxAppId = SandboxedAs,
                                              .user = TestUser })
          == "/run/user/1000/app/org.contourterminal.Contour/contour/work");
}

TEST_CASE("the sandbox rung does not outrank an explicit path or $CONTOUR_MUX", "[vthost][socketpath]")
{
    // Both remain the escape hatch for the case the rung cannot fix: a daemon on the HOST is not
    // sandboxed, so it derives the plain path, and only naming the socket makes the two agree.
    CHECK(muxSocketPath(MuxSocketPathInputs { .explicitPath = "/tmp/explicit.sock",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .sandboxAppId = SandboxedAs,
                                              .user = TestUser })
          == "/tmp/explicit.sock");
    CHECK(muxSocketPath(MuxSocketPathInputs { .contourMuxEnv = "/env/mux.sock",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .sandboxAppId = SandboxedAs,
                                              .user = TestUser })
          == "/env/mux.sock");
}

TEST_CASE("without a runtime dir the sandbox rung has nothing to qualify", "[vthost][socketpath]")
{
    // The app-scoped directory only exists BENEATH a runtime dir, so with none there is nothing to
    // qualify and the temp fallback stands as it does off the sandbox.
    auto const path = muxSocketPath(
        MuxSocketPathInputs { .name = "fallback", .sandboxAppId = SandboxedAs, .user = TestUser });

    CHECK(path.parent_path().filename() == std::string { "contour-" } + TestUser);
    CHECK(path.filename() == "fallback");
}

TEST_CASE("a relative runtime dir counts as unset", "[vthost][socketpath]")
{
    // The XDG base directory specification has a relative value ignored: it would put the socket
    // beneath whatever directory the daemon or the client happened to start in, so the two would not
    // meet.
    auto const path = muxSocketPath(
        MuxSocketPathInputs { .name = "fallback", .xdgRuntimeDir = "run/user", .user = TestUser });

    CHECK(path.parent_path().filename() == std::string { "contour-" } + TestUser);
    CHECK(path.filename() == "fallback");
}

TEST_CASE("an empty env value counts as unset", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs {
              .contourMuxEnv = "", .xdgRuntimeDir = "/run/user/1000", .user = TestUser })
          == "/run/user/1000/contour/default");
}

TEST_CASE("without a runtime dir the path falls back to a per-user temp dir", "[vthost][socketpath]")
{
    auto const path = muxSocketPath(MuxSocketPathInputs { .name = "fallback", .user = TestUser });

    CHECK(path.parent_path().filename() == std::string { "contour-" } + TestUser);
    CHECK(path.filename() == "fallback"); // the name is the leaf, whatever the OS separator
}

TEST_CASE("the production overload reads its inputs from the injected environment", "[vthost][socketpath]")
{
    // What the `env` parameter is for: no variable of the test process is consulted, and none is
    // mutated to arrange this.
    auto const environment = core::testing::FakeEnvironment { { { "XDG_RUNTIME_DIR", "/run/user/4242" } } };

    CHECK(muxSocketPath(vthost::daemonNameFromOption("work"), "", environment)
          == "/run/user/4242/contour/work");

    SECTION("and $CONTOUR_MUX still outranks it")
    {
        auto const overridden = core::testing::FakeEnvironment { {
            { "CONTOUR_MUX", "/env/mux.sock" },
            { "XDG_RUNTIME_DIR", "/run/user/4242" },
        } };

        CHECK(muxSocketPath(DaemonName { .name = "work" }, "", overridden) == "/env/mux.sock");
    }
}

TEST_CASE("a label's own socket is what neither --socket nor $CONTOUR_MUX names", "[vthost][socketpath]")
{
    // What a daemon at a socket of its own is told apart from: where its name would serve otherwise.
    auto const overridden = core::testing::FakeEnvironment { {
        { "CONTOUR_MUX", "/env/mux.sock" },
        { "XDG_RUNTIME_DIR", "/run/user/4242" },
    } };
    CHECK(vthost::namedSocketPath("work", overridden) == "/run/user/4242/contour/work");
    CHECK(vthost::namedSocketPath(vthost::DefaultDaemonName, overridden) == "/run/user/4242/contour/default");
    CHECK(muxSocketPath(DaemonName { .name = "work" }, "", overridden) == "/env/mux.sock");
}

TEST_CASE("the production overload takes the sandbox from where it is injected too", "[vthost][socketpath]")
{
    // The sandbox is a parameter for the same reason the environment is: without one, this case
    // could only be reached by running the suite inside an actual Flatpak.
    auto const environment = core::testing::FakeEnvironment { { { "XDG_RUNTIME_DIR", "/run/user/4242" } } };
    auto const sandboxed = vtpty::SandboxInfo { .state = vtpty::SandboxState::Flatpak,
                                                .network = vtpty::NetworkAccess::Denied,
                                                .applicationId = SandboxedAs };

    CHECK(muxSocketPath(vthost::daemonNameFromOption("work"), "", environment, sandboxed)
          == "/run/user/4242/app/org.contourterminal.Contour/contour/work");
}

TEST_CASE("a daemon-hosted shell is told where its own daemon is", "[vthost][socketpath]")
{
    // It cannot derive it: with escape_sandbox the shell runs on the HOST while the daemon that
    // spawned it does not, and the two sides derive different paths. @see issue #2075.
    auto const environment = vthost::hostedShellEnvironment("/run/user/1000/contour/default");

    REQUIRE(environment.contains("CONTOUR_MUX"));
    CHECK(environment.at("CONTOUR_MUX") == "/run/user/1000/contour/default");
    // The same path under the name the control API's clients read.
    REQUIRE(environment.contains("CONTOUR_DAEMON"));
    CHECK(environment.at("CONTOUR_DAEMON") == "/run/user/1000/contour/default");

    // In UTF-8, as the spawn reads an environment value.
    auto const awkward =
        vthost::hostedShellEnvironment(std::filesystem::path { u8"/tmp/" } / vtpty::testing::NonAsciiName);
    CHECK(awkward.at("CONTOUR_MUX") == std::format("/tmp/{}", vtpty::testing::NonAsciiNameUtf8));
    CHECK(awkward.at("CONTOUR_DAEMON") == awkward.at("CONTOUR_MUX"));
}

TEST_CASE("HostedShellVariables names every variable a hosted shell is handed", "[vthost][socketpath]")
{
    auto const uuid = *vtcontrol::parseUuid("018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    auto handed = vthost::hostedShellEnvironment("/run/c.sock");
    handed.merge(vthost::paneEnvironment(uuid, uuid, uuid));
    CHECK(handed.size() == vthost::HostedShellVariables.size());
    for (auto const& [name, value]: handed)
        CHECK(std::ranges::contains(vthost::HostedShellVariables, std::string_view { name }));
}

TEST_CASE("a hosted shell's pane variables are full references", "[vthost][socketpath]")
{
    auto const uuid = *vtcontrol::parseUuid("018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    auto const environment = vthost::paneEnvironment(uuid, uuid, uuid);
    CHECK(environment.at("CONTOUR_PANE") == "pane:018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    CHECK(environment.at("CONTOUR_TAB") == "tab:018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    CHECK(environment.at("CONTOUR_WORKSPACE") == "workspace:018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    CHECK(environment.size() == 3);

    // The daemon's own variables win over a profile's, the pane's ids among a profile's variables.
    auto base = vtpty::Process::ExecInfo {};
    base.program = "/bin/zsh";
    auto profile = vtpty::Process::ExecInfo {};
    profile.env = { { "CONTOUR_PANE", "bogus" }, { "A", "1" } };
    auto const shell = vthost::hostedShellFor(base, profile, vthost::ShellOverride::Replaces, environment);
    CHECK(shell.env.at("CONTOUR_PANE") == "pane:018f3c4e-9a1b-7c2d-8e3f-0123456789ab");
    CHECK(shell.env.at("A") == "1");
}

TEST_CASE("an explicit label beats $CONTOUR_MUX, the default label yields to it", "[vthost][socketpath]")
{
    constexpr auto Mux = "/run/user/1000/contour/default";

    CHECK(muxSocketPath(MuxSocketPathInputs { .name = "dragtest",
                                              .nameOrigin = DaemonNameOrigin::Explicit,
                                              .contourMuxEnv = Mux,
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .user = TestUser })
          == "/run/user/1000/contour/dragtest");

    // Nobody named a label: the daemon hosting the calling shell is the one meant.
    CHECK(muxSocketPath(MuxSocketPathInputs { .name = "dragtest",
                                              .nameOrigin = DaemonNameOrigin::Default,
                                              .contourMuxEnv = "/env/mux.sock",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .user = TestUser })
          == "/env/mux.sock");
}

TEST_CASE("an explicitly given `default` label beats $CONTOUR_MUX too", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs { .name = vthost::DefaultDaemonName,
                                              .nameOrigin = DaemonNameOrigin::Explicit,
                                              .contourMuxEnv = "/env/mux.sock",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .user = TestUser })
          == "/run/user/1000/contour/default");
}

TEST_CASE("--socket beats both an explicit label and $CONTOUR_MUX", "[vthost][socketpath]")
{
    CHECK(muxSocketPath(MuxSocketPathInputs { .name = "dragtest",
                                              .nameOrigin = DaemonNameOrigin::Explicit,
                                              .explicitPath = "/tmp/explicit.sock",
                                              .contourMuxEnv = "/env/mux.sock",
                                              .xdgRuntimeDir = "/run/user/1000",
                                              .user = TestUser })
          == "/tmp/explicit.sock");
}

TEST_CASE(
    "an explicit label is derived through the sandbox rung, and the unlabelled shell still finds its daemon",
    "[vthost][socketpath]")
{
    // The Flatpak rationale for $CONTOUR_MUX: a sandboxed shell with no --label reaches its own daemon.
    auto const environment = core::testing::FakeEnvironment { {
        { "CONTOUR_MUX", "/run/user/4242/app/org.contourterminal.Contour/contour/default" },
        { "XDG_RUNTIME_DIR", "/run/user/4242" },
    } };
    auto const sandboxed = vtpty::SandboxInfo { .state = vtpty::SandboxState::Flatpak,
                                                .network = vtpty::NetworkAccess::Denied,
                                                .applicationId = SandboxedAs };

    CHECK(muxSocketPath(DaemonName {}, "", environment, sandboxed)
          == "/run/user/4242/app/org.contourterminal.Contour/contour/default");
    CHECK(muxSocketPath(vthost::daemonNameFromOption("work"), "", environment, sandboxed)
          == "/run/user/4242/app/org.contourterminal.Contour/contour/work");
}

TEST_CASE("daemonNameFromOption reads an empty option as not given", "[vthost][socketpath]")
{
    CHECK(vthost::daemonNameFromOption("").origin == DaemonNameOrigin::Default);
    CHECK(vthost::daemonNameFromOption("").name == vthost::DefaultDaemonName);
    CHECK(vthost::daemonNameFromOption("default").origin == DaemonNameOrigin::Explicit);
    CHECK(vthost::daemonNameFromOption("work").name == "work");
}

TEST_CASE("validateDaemonName accepts what can name a socket discovery lists", "[vthost][socketpath]")
{
    using vthost::DaemonNameError;
    struct Row
    {
        std::string name;
        std::optional<DaemonNameError> refused;
    };
    auto const rows = std::to_array<Row>({
        { .name = "work2", .refused = std::nullopt },
        { .name = "admin_box-1.dev", .refused = std::nullopt },
        { .name = std::string(vthost::MaxDaemonNameLength, 'a'), .refused = std::nullopt },
        { .name = "", .refused = DaemonNameError::Empty },
        { .name = std::string(vthost::MaxDaemonNameLength + 1, 'a'), .refused = DaemonNameError::TooLong },
        { .name = ".", .refused = DaemonNameError::LeadingDot },
        { .name = "..", .refused = DaemonNameError::LeadingDot },
        { .name = ".hidden", .refused = DaemonNameError::LeadingDot },
        { .name = "a/b", .refused = DaemonNameError::InvalidCharacter },
        { .name = "a\\b", .refused = DaemonNameError::InvalidCharacter },
        { .name = "with space", .refused = DaemonNameError::InvalidCharacter },
        { .name = "tab\there", .refused = DaemonNameError::InvalidCharacter },
        { .name = "caf\xc3\xa9", .refused = DaemonNameError::InvalidCharacter },
        { .name = "work-native", .refused = DaemonNameError::SiblingSuffix },
        { .name = "work-tmux", .refused = DaemonNameError::SiblingSuffix },
        { .name = "work.log", .refused = DaemonNameError::SiblingSuffix },
    });
    for (auto const& row: rows)
    {
        INFO("label \"" << row.name << "\"");
        auto const checked = vthost::validateDaemonName(row.name);
        if (!row.refused)
            CHECK(checked.has_value());
        else
        {
            REQUIRE_FALSE(checked.has_value());
            CHECK(checked.error() == *row.refused);
            CHECK_FALSE(vthost::describe(checked.error()).empty());
        }
    }
    CHECK(vthost::describe(DaemonNameError::TooLong).contains(std::to_string(vthost::MaxDaemonNameLength)));
}
