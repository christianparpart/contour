// SPDX-License-Identifier: Apache-2.0
//
// compat(0.9.0): the command line's old spellings, rewritten before any parse.

#include <contour/cli/ContourApp.hpp>
#include <contour/cli/LegacyArguments.hpp>

#include <core/Environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

using contour::cli::LegacyArgumentRow;
using contour::cli::LegacyPlace;
using contour::cli::rewriteLegacyArguments;

namespace
{
using Strings = std::vector<std::string>;
namespace CLI = core::cli;

/// @return The syntax main() rewrites against: the headless app's, and `contour client`, which only the GUI
///         app has, as it reads its options.
[[nodiscard]] CLI::Command const& grammar()
{
    static auto const syntax = [] {
        auto command = contour::cli::ContourApp { core::defaultEnvironment() }.parameterDefinition();
        command.children.push_back(
            CLI::Command { .name = "client",
                           .options = { CLI::Option { "daemon", CLI::Value { std::string {} } },
                                        CLI::Option { "tmux", CLI::Value { false } } },
                           .verbatim = CLI::Verbatim { "TARGET", "Where the sessions live." } });
        return command;
    }();
    return syntax;
}

/// @return The rewrite of @p args, as main() would see them.
template <typename... Args>
[[nodiscard]] contour::cli::RewrittenArguments rewrite(Args... args)
{
    auto const argv = std::array<char const*, sizeof...(Args)> { args... };
    return rewriteLegacyArguments(argv, grammar());
}

/// A syntax of its own, for the rows of the test that says how option rows work.
[[nodiscard]] CLI::Command const& testGrammar()
{
    static auto const syntax = CLI::Command {
        .name = "contour",
        .children = {
            CLI::Command { .name = "daemon", .options = { CLI::Option { "new", CLI::Value { std::string {} } } } },
            CLI::Command { .name = "ls",
                           .options = { CLI::Option { "pick", CLI::Value { std::string {} } },
                                        CLI::Option { "json", CLI::Value { false } } } },
            CLI::Command { .name = "new",
                           .options = { CLI::Option { "pick", CLI::Value { std::string {} } },
                                        CLI::Option { "json", CLI::Value { false } },
                                        CLI::Option { .name = "exec",
                                                      .v = CLI::Value { std::string {} },
                                                      .verbatimStart = CLI::VerbatimStart::AfterValue } },
                           .verbatim = CLI::Verbatim { "CMD", "The program to run." } },
        },
    };
    return syntax;
}

template <typename... Args>
[[nodiscard]] contour::cli::RewrittenArguments rewriteWith(std::span<LegacyArgumentRow const> rows,
                                                           Args... args)
{
    auto const argv = std::array<char const*, sizeof...(Args)> { args... };
    return rewriteLegacyArguments(argv, testGrammar(), rows);
}
} // namespace

TEST_CASE("the label verb is rewritten to rename", "[contour][cli][compat-0.9.0]")
{
    auto const result = rewrite("contour", "label", "pane:logs", "tail");
    CHECK(result.arguments == Strings { "contour", "rename", "pane:logs", "tail" });
    REQUIRE(result.notes.size() == 1);
    CHECK(result.notes.front().contains("contour rename"));
}

TEST_CASE("workspace rename is rewritten to rename workspace:", "[contour][cli][compat-0.9.0]")
{
    CHECK(rewrite("contour", "workspace", "rename", "build", "x").arguments
          == Strings { "contour", "rename", "workspace:build", "x" });
    CHECK(rewrite("contour", "workspace", "rename", "1", "").arguments
          == Strings { "contour", "rename", "workspace:1", "" });

    SECTION("a workspace with a kind prefix keeps it")
    {
        CHECK(rewrite("contour", "workspace", "rename", "workspace:build", "x").arguments
              == Strings { "contour", "rename", "workspace:build", "x" });
    }
    SECTION("options before the operands stay where they are")
    {
        CHECK(rewrite("contour", "workspace", "rename", "--json", "build", "x").arguments
              == Strings { "contour", "rename", "--json", "workspace:build", "x" });
        CHECK(rewrite("contour", "workspace", "rename", "--", "build", "-x").arguments
              == Strings { "contour", "rename", "--", "workspace:build", "-x" });
    }
    SECTION("a line short of an operand is left for rename to refuse")
    {
        CHECK(rewrite("contour", "workspace", "rename", "build").arguments
              == Strings { "contour", "rename", "build" });
    }
}

TEST_CASE("a line with no legacy spelling comes back unchanged", "[contour][cli][compat-0.9.0]")
{
    auto const result = rewrite("contour", "rename", "pane:logs", "tail");
    CHECK(result.arguments == Strings { "contour", "rename", "pane:logs", "tail" });
    CHECK(result.notes.empty());
    CHECK(rewrite("contour").arguments == Strings { "contour" });
    CHECK(rewriteLegacyArguments(std::span<char const* const> {}, grammar()).arguments.empty());
}

TEST_CASE("only the verb position is rewritten", "[contour][cli][compat-0.9.0]")
{
    // A positional value, or a command after `--`, that happens to read "label" is not a verb.
    auto const named = rewrite("contour", "rename", "pane:x", "label");
    CHECK(named.arguments == Strings { "contour", "rename", "pane:x", "label" });
    CHECK(named.notes.empty());
    CHECK(rewrite("contour", "new", "--", "cmd", "label", "x").arguments
          == Strings { "contour", "new", "--", "cmd", "label", "x" });
    CHECK(rewrite("contour", "send", "workspace", "rename", "a", "b").arguments
          == Strings { "contour", "send", "workspace", "rename", "a", "b" });
}

TEST_CASE("option rows rewrite a flag under its verbs, up to --", "[contour][cli][compat-0.9.0]")
{
    static constexpr auto Daemon = std::array<std::string_view, 1> { "daemon" };
    static constexpr auto OldFlag = std::array<std::string_view, 1> { "--old" };
    static constexpr auto NewFlag = std::array<std::string_view, 1> { "--new" };
    static constexpr auto Selector = std::array<std::string_view, 1> { "--pick" };
    static constexpr auto Kept = std::array<std::string_view, 2> { "name:", "host:" };
    static constexpr auto Rows = std::array {
        LegacyArgumentRow { .place = LegacyPlace::Option,
                            .verbPath = Daemon,
                            .oldTokens = OldFlag,
                            .newTokens = NewFlag,
                            .note = "daemon --old" },
        LegacyArgumentRow { .place = LegacyPlace::Option,
                            .oldTokens = OldFlag,
                            .newTokens = Selector,
                            .operandPrefix = "name:",
                            .keptPrefixes = Kept,
                            .note = "--old" },
    };

    SECTION("the first row that matches wins")
    {
        auto const result = rewriteWith(Rows, "contour", "daemon", "--old", "work");
        CHECK(result.arguments == Strings { "contour", "daemon", "--new", "work" });
        CHECK(result.notes == Strings { "daemon --old" });
    }
    SECTION("the operand is the next argument, or what follows '='")
    {
        CHECK(rewriteWith(Rows, "contour", "ls", "--old", "work", "--json").arguments
              == Strings { "contour", "ls", "--pick", "name:work", "--json" });
        CHECK(rewriteWith(Rows, "contour", "ls", "--old=work").arguments
              == Strings { "contour", "ls", "--pick=name:work" });
        CHECK(rewriteWith(Rows, "contour", "ls", "--old", "host:box").arguments
              == Strings { "contour", "ls", "--pick", "host:box" });
    }
    SECTION("one note per row, however often it applies")
    {
        auto const result = rewriteWith(Rows, "contour", "ls", "--old", "a", "--old", "b");
        CHECK(result.arguments == Strings { "contour", "ls", "--pick", "name:a", "--pick", "name:b" });
        CHECK(result.notes == Strings { "--old" });
    }
    SECTION("nothing after -- is an option")
    {
        auto const result = rewriteWith(Rows, "contour", "new", "--", "cmd", "--old", "x");
        CHECK(result.arguments == Strings { "contour", "new", "--", "cmd", "--old", "x" });
        CHECK(result.notes.empty());
    }
}

TEST_CASE("option rows stop where the command's verbatim input starts", "[contour][cli][compat-0.9.0]")
{
    static constexpr auto OldFlag = std::array<std::string_view, 1> { "--old" };
    static constexpr auto Selector = std::array<std::string_view, 1> { "--pick" };
    static constexpr auto Rows = std::array {
        LegacyArgumentRow {
            .place = LegacyPlace::Option, .oldTokens = OldFlag, .newTokens = Selector, .note = "--old" },
    };

    SECTION("at the first token that is neither an option nor a value")
    {
        auto const result = rewriteWith(Rows, "contour", "new", "mycmd", "--old", "x");
        CHECK(result.arguments == Strings { "contour", "new", "mycmd", "--old", "x" });
        CHECK(result.notes.empty());
    }
    SECTION("after a boolean, whose value is optional")
    {
        CHECK(rewriteWith(Rows, "contour", "new", "--json", "mycmd", "--old", "x").arguments
              == Strings { "contour", "new", "--json", "mycmd", "--old", "x" });
        CHECK(rewriteWith(Rows, "contour", "new", "--json", "yes", "--old", "x").arguments
              == Strings { "contour", "new", "--json", "yes", "--pick", "x" });
    }
    SECTION("right after the value of an option that starts it")
    {
        CHECK(rewriteWith(Rows, "contour", "new", "--old", "a", "--exec", "sh", "--old", "x").arguments
              == Strings { "contour", "new", "--pick", "a", "--exec", "sh", "--old", "x" });
    }
    SECTION("a value is no option, even one that reads like the old one")
    {
        CHECK(rewriteWith(Rows, "contour", "ls", "--pick", "--old", "--old", "x").arguments
              == Strings { "contour", "ls", "--pick", "--old", "--pick", "x" });
    }
}

TEST_CASE("--label picks a daemon by its name, or names one", "[contour][cli][compat-0.9.0]")
{
    SECTION("on a control verb it becomes --daemon name:")
    {
        auto const result = rewrite("contour", "ls", "--label", "work");
        CHECK(result.arguments == Strings { "contour", "ls", "--daemon", "name:work" });
        REQUIRE(result.notes.size() == 1);
        CHECK(result.notes.front().contains("--daemon name:NAME"));
        CHECK(rewrite("contour", "ls", "--label=work").arguments
              == Strings { "contour", "ls", "--daemon=name:work" });
    }
    SECTION("on contour client too")
    {
        CHECK(rewrite("contour", "client", "--label", "work").arguments
              == Strings { "contour", "client", "--daemon", "name:work" });
    }
    SECTION("on contour daemon, its sub-verbs and every daemon-service verb it becomes --name")
    {
        auto const daemon = rewrite("contour", "daemon", "--label", "work");
        CHECK(daemon.arguments == Strings { "contour", "daemon", "--name", "work" });
        REQUIRE(daemon.notes.size() == 1);
        CHECK(daemon.notes.front().contains("--name NAME"));
        CHECK(rewrite("contour", "daemon", "status", "--label", "work").arguments
              == Strings { "contour", "daemon", "status", "--name", "work" });
        CHECK(rewrite("contour", "daemon", "--label=work").arguments
              == Strings { "contour", "daemon", "--name=work" });
        for (auto const* verb: { "install", "uninstall", "start", "stop", "restart", "status" })
        {
            INFO(verb);
            CHECK(rewrite("contour", "daemon-service", verb, "--label", "work").arguments
                  == Strings { "contour", "daemon-service", verb, "--name", "work" });
        }
    }
    SECTION("the live-upgrade compat case: a restarted daemon carries --name, its handover argument kept")
    {
        CHECK(rewrite("/state/bin/Contour.AppImage", "daemon", "--handover=fd=7", "--label", "work").arguments
              == Strings { "/state/bin/Contour.AppImage", "daemon", "--handover=fd=7", "--name", "work" });
    }
    SECTION("only where the parser reads options, never in the command `new` runs")
    {
        auto const command = rewrite("contour", "new", "mycmd", "--label", "x");
        CHECK(command.arguments == Strings { "contour", "new", "mycmd", "--label", "x" });
        CHECK(command.notes.empty());
        CHECK(rewrite("contour", "new", "--label", "x", "mycmd", "--label", "y").arguments
              == Strings { "contour", "new", "--daemon", "name:x", "mycmd", "--label", "y" });
    }
    SECTION("a creating verb's --name still names what it makes")
    {
        CHECK(rewrite("contour", "new", "--label", "work", "--name", "logs").arguments
              == Strings { "contour", "new", "--daemon", "name:work", "--name", "logs" });
    }
}

TEST_CASE("switch --to-label is rewritten to --to-name", "[contour][cli][compat-0.9.0]")
{
    auto const result = rewrite("contour", "switch", "--to-label", "admin");
    CHECK(result.arguments == Strings { "contour", "switch", "--to-name", "admin" });
    REQUIRE(result.notes.size() == 1);
    CHECK(result.notes.front().contains("--to-name"));
}

TEST_CASE("the rewritten old spellings parse", "[contour][cli][compat-0.9.0]")
{
    // What each row writes is a line the app's own parser takes.
    for (auto const& line:
         { Strings { "contour", "ls", "--label", "work" },
           Strings { "contour", "daemon", "status", "--label", "work" },
           Strings { "contour", "daemon-service", "status", "--label", "work" },
           Strings { "contour", "switch", "--to-label", "admin" },
           Strings { "contour", "new", "--label", "w", "--name", "logs", "sh", "--label" } })
    {
        auto argv = std::vector<char const*> {};
        for (auto const& argument: line)
            argv.push_back(argument.c_str());
        auto const rewritten = rewriteLegacyArguments(argv, grammar());
        auto rewrittenArgv = std::vector<char const*> {};
        for (auto const& argument: rewritten.arguments)
            rewrittenArgv.push_back(argument.c_str());
        INFO(line.at(1));
        CHECK(
            CLI::parse(grammar(), static_cast<int>(rewrittenArgv.size()), rewrittenArgv.data()).has_value());
    }
}
