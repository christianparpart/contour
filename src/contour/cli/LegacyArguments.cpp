// SPDX-License-Identifier: Apache-2.0
#include <contour/cli/LegacyArguments.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace contour::cli
{

namespace
{
    using namespace std::string_view_literals;

    // compat(0.9.0): the table. A verb row's tokens are looked for at the verb, so a positional value or
    // a command after `--` that reads the same is never rewritten. Option rows go after the verb rows.
    constexpr auto LabelVerb = std::array { "label"sv };
    constexpr auto WorkspaceRenameVerb = std::array { "workspace"sv, "rename"sv };
    constexpr auto RenameVerb = std::array { "rename"sv };
    constexpr auto ObjectKinds = std::array { "workspace:"sv, "tab:"sv, "pane:"sv };
    constexpr auto DaemonVerb = std::array { "daemon"sv };
    constexpr auto DaemonServiceVerb = std::array { "daemon-service"sv };
    constexpr auto SwitchVerb = std::array { "switch"sv };
    constexpr auto LabelOption = std::array { "--label"sv };
    constexpr auto NameOption = std::array { "--name"sv };
    constexpr auto DaemonOption = std::array { "--daemon"sv };
    constexpr auto ToLabelOption = std::array { "--to-label"sv };
    constexpr auto ToNameOption = std::array { "--to-name"sv };
    constexpr auto DaemonWordPrefixes = std::array { "name:"sv, "host:"sv };

    constexpr auto Rows = std::array {
        LegacyArgumentRow {
            .place = LegacyPlace::Verb,
            .oldTokens = LabelVerb,
            .newTokens = RenameVerb,
            .note = "`contour label` is deprecated and goes away in 0.9.0; use `contour rename OBJECT NAME`.",
        },
        LegacyArgumentRow {
            .place = LegacyPlace::Verb,
            .oldTokens = WorkspaceRenameVerb,
            .newTokens = RenameVerb,
            .operandPrefix = "workspace:",
            .operandFromEnd = 2, // WORKSPACE NAME
            .keptPrefixes = ObjectKinds,
            .note = "`contour workspace rename WORKSPACE NAME` is deprecated and goes away in 0.9.0; use "
                    "`contour rename workspace:WORKSPACE NAME`.",
        },
        // `contour daemon` and its sub-verbs, and every `daemon-service` verb, name the daemon they run or
        // manage; before the generic row, which would make it a selector.
        LegacyArgumentRow {
            .place = LegacyPlace::Option,
            .verbPath = DaemonVerb,
            .oldTokens = LabelOption,
            .newTokens = NameOption,
            .note = "`--label` on `contour daemon` is deprecated and goes away in 0.9.0; use `--name NAME`.",
        },
        LegacyArgumentRow {
            .place = LegacyPlace::Option,
            .verbPath = DaemonServiceVerb,
            .oldTokens = LabelOption,
            .newTokens = NameOption,
            .note = "`--label` on `contour daemon-service` is deprecated and goes away in 0.9.0; use "
                    "`--name NAME`.",
        },
        LegacyArgumentRow {
            .place = LegacyPlace::Option,
            .verbPath = SwitchVerb,
            .oldTokens = ToLabelOption,
            .newTokens = ToNameOption,
            .note = "`--to-label` is deprecated and goes away in 0.9.0; use `--to-name NAME`.",
        },
        // Everywhere else `--label` picked a local daemon to talk to: the control verbs, `contour client`.
        LegacyArgumentRow {
            .place = LegacyPlace::Option,
            .oldTokens = LabelOption,
            .newTokens = DaemonOption,
            .operandPrefix = "name:",
            .keptPrefixes = DaemonWordPrefixes,
            .note = "`--label NAME` is deprecated and goes away in 0.9.0; use `--daemon name:NAME`, or "
                    "`--daemon NAME`.",
        },
    };

    using Arguments = std::vector<std::string>;

    /// @return Whether @p arguments hold @p tokens at @p at.
    [[nodiscard]] bool holdsAt(Arguments const& arguments,
                               std::size_t at,
                               std::span<std::string_view const> tokens)
    {
        return at + tokens.size() <= arguments.size()
               && std::ranges::equal(arguments | std::views::drop(at) | std::views::take(tokens.size()),
                                     tokens);
    }

    /// @return @p operand with the row's prefix, unless it carries that or a kept one already.
    [[nodiscard]] std::string prefixed(LegacyArgumentRow const& row, std::string_view operand)
    {
        auto const startsWith = [operand](std::string_view prefix) {
            return operand.starts_with(prefix);
        };
        if (row.operandPrefix.empty() || startsWith(row.operandPrefix)
            || std::ranges::any_of(row.keptPrefixes, startsWith))
            return std::string { operand };
        return std::format("{}{}", row.operandPrefix, operand);
    }

    /// @return Whether the argument at @p at reads as an operand: after a `--`, or not an option.
    [[nodiscard]] bool isOperand(Arguments const& arguments, std::size_t from, std::size_t at)
    {
        auto const options = arguments | std::views::drop(from) | std::views::take(at - from);
        return std::ranges::contains(options, "--"sv) || !arguments[at].starts_with('-');
    }

    /// Rewrites the verb at @p start.
    /// @return Whether the row applied.
    [[nodiscard]] bool rewriteVerb(LegacyArgumentRow const& row, Arguments& arguments, std::size_t start)
    {
        if (!holdsAt(arguments, start, row.oldTokens))
            return false;
        auto const at = arguments.begin() + static_cast<std::ptrdiff_t>(start);
        arguments.erase(at, at + static_cast<std::ptrdiff_t>(row.oldTokens.size()));
        arguments.insert(arguments.begin() + static_cast<std::ptrdiff_t>(start),
                         row.newTokens.begin(),
                         row.newTokens.end());

        // A line short of the operand is left for the verb to refuse.
        auto const operandsFrom = start + row.newTokens.size();
        if (row.operandFromEnd == 0 || arguments.size() < operandsFrom + row.operandFromEnd)
            return true;
        auto const operand = arguments.size() - row.operandFromEnd;
        if (isOperand(arguments, operandsFrom, operand))
            arguments[operand] = prefixed(row, arguments[operand]);
        return true;
    }

    /// @return Whether @p token spells @p option with a value after an `=`, as `--option=value`.
    [[nodiscard]] bool spellsWithValue(std::string_view token, std::string_view option)
    {
        return token.size() > option.size() && token.starts_with(option) && token[option.size()] == '=';
    }

    /// @return The option of @p command that @p token names as the parser reads it: `--NAME`, `--NAME=VALUE`,
    ///         `-N`, or the natural `NAME`; null for none.
    [[nodiscard]] core::cli::Option const* optionNamed(core::cli::Command const& command,
                                                       std::string_view token)
    {
        auto name = token;
        if (name.starts_with("--"))
            name = name.substr(2).substr(0, name.substr(2).find('='));
        else if (name.starts_with('-'))
            name = name.substr(1);
        auto const found = std::ranges::find_if(command.options, [name](core::cli::Option const& option) {
            return name == option.name.longName
                   || (name.size() == 1 && name.front() == option.name.shortName);
        });
        return found != command.options.end() ? &*found : nullptr;
    }

    /// Where the command-line parser reads options: the positions of the tokens it reads as an option's name.
    struct OptionPositions
    {
        std::vector<std::size_t> names; ///< Each option's name, alone or as `--option=value`, in order.
    };

    /// Reads @p arguments as core::cli::parse() does against @p grammar: options after each verb, the next
    /// verb, and a command's verbatim input from the first token that is neither (or right after an option
    /// that starts it, VerbatimStart::AfterValue). An old option of @p rows, which the grammar lacks, is read
    /// as an option that takes a value. Where the parser would refuse the line, the reading stops.
    /// @return Where the options are.
    [[nodiscard]] OptionPositions optionPositions(core::cli::Command const& grammar,
                                                  Arguments const& arguments,
                                                  std::span<LegacyArgumentRow const> rows)
    {
        auto const isOldOption = [rows](std::string_view token) {
            return std::ranges::any_of(rows, [token](LegacyArgumentRow const& row) {
                return row.place == LegacyPlace::Option
                       && (token == row.oldTokens.front() || spellsWithValue(token, row.oldTokens.front()));
            });
        };
        auto positions = OptionPositions {};
        auto const* command = &grammar;
        auto at = std::size_t { 1 }; // the program name is no option
        while (at < arguments.size())
        {
            auto const& token = arguments[at];
            auto const* option = optionNamed(*command, token);
            if (option != nullptr || (token.starts_with('-') && isOldOption(token)))
            {
                positions.names.push_back(at++);
                auto const takesValue = !token.starts_with("--") || !token.contains('=');
                if (option == nullptr)
                    at += takesValue ? 1 : 0;
                else if (takesValue && std::holds_alternative<bool>(option->v))
                    // A boolean's value is optional: only `true`, `yes`, `false` and `no` are read as one.
                    at +=
                        at < arguments.size()
                                && std::ranges::contains(std::array { "true"sv, "yes"sv, "false"sv, "no"sv },
                                                         std::string_view { arguments[at] })
                            ? 1
                            : 0;
                else if (takesValue)
                    ++at;
                if (option != nullptr && option->verbatimStart == core::cli::VerbatimStart::AfterValue
                    && command->verbatim.has_value())
                    break;
                continue;
            }
            auto const verb = std::string_view { token }.starts_with("--")
                                  ? std::string_view { token }.substr(2)
                                  : std::string_view { token };
            auto const& children = command->children;
            if (auto const child = std::ranges::find(children, verb, &core::cli::Command::name);
                child != children.end())
            {
                command = &*child;
                ++at;
                continue;
            }
            auto const implicit =
                std::ranges::find(children, core::cli::CommandSelect::Implicit, &core::cli::Command::select);
            if (implicit == children.end())
                break; // verbatim input, a `--`, or a token the parser refuses
            command = &*implicit;
        }
        return positions;
    }

    /// Rewrites every occurrence of the row's option the parser reads as one after @p start.
    /// @return Whether the row applied.
    [[nodiscard]] bool rewriteOption(LegacyArgumentRow const& row,
                                     Arguments& arguments,
                                     std::size_t start,
                                     OptionPositions const& positions)
    {
        auto const oldOption = row.oldTokens.front();
        auto const newOption = std::string { row.newTokens.front() };
        auto applied = false;
        for (auto const at: positions.names | std::views::filter([start](std::size_t position) {
                                return position >= start;
                            }))
        {
            auto& argument = arguments[at];
            if (argument == oldOption)
            {
                argument = newOption;
                applied = true;
                if (at + 1 < arguments.size())
                    arguments[at + 1] = prefixed(row, arguments[at + 1]); // its value
            }
            else if (spellsWithValue(argument, oldOption))
            {
                argument =
                    std::format("{}={}",
                                newOption,
                                prefixed(row, std::string_view { argument }.substr(oldOption.size() + 1)));
                applied = true;
            }
        }
        return applied;
    }
} // namespace

RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv,
                                          core::cli::Command const& grammar)
{
    return rewriteLegacyArguments(argv, grammar, Rows);
}

RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv,
                                          core::cli::Command const& grammar,
                                          std::span<LegacyArgumentRow const> rows)
{
    auto result = RewrittenArguments {};
    for (auto const* argument: argv)
        result.arguments.emplace_back(argument);
    for (auto const& row: rows)
    {
        // The program name, then the row's verb path.
        if (result.arguments.empty() || !holdsAt(result.arguments, 1, row.verbPath))
            continue;
        auto const start = 1 + row.verbPath.size();
        auto const applied =
            row.place == LegacyPlace::Verb
                ? rewriteVerb(row, result.arguments, start)
                : rewriteOption(
                      row, result.arguments, start, optionPositions(grammar, result.arguments, rows));
        if (applied)
            result.notes.emplace_back(row.note);
    }
    return result;
}

} // namespace contour::cli
