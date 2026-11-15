// SPDX-License-Identifier: Apache-2.0
#include <contour/cli/LegacyArguments.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
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

    /// Rewrites every occurrence of the row's option after @p start, up to a `--`.
    /// @return Whether the row applied.
    [[nodiscard]] bool rewriteOption(LegacyArgumentRow const& row, Arguments& arguments, std::size_t start)
    {
        auto const oldOption = row.oldTokens.front();
        auto const newOption = std::string { row.newTokens.front() };
        auto applied = false;
        for (auto at = start; at < arguments.size() && arguments[at] != "--"; ++at)
        {
            auto& argument = arguments[at];
            if (argument == oldOption)
            {
                argument = newOption;
                applied = true;
                if (!row.operandPrefix.empty() && at + 1 < arguments.size())
                {
                    ++at; // the value, which is no option
                    arguments[at] = prefixed(row, arguments[at]);
                }
            }
            else if (argument.starts_with(oldOption) && argument.size() > oldOption.size()
                     && argument[oldOption.size()] == '=')
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

RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv)
{
    return rewriteLegacyArguments(argv, Rows);
}

RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv,
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
        auto const applied = row.place == LegacyPlace::Verb ? rewriteVerb(row, result.arguments, start)
                                                            : rewriteOption(row, result.arguments, start);
        if (applied)
            result.notes.emplace_back(row.note);
    }
    return result;
}

} // namespace contour::cli
