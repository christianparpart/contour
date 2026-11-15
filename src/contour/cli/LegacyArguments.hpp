// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// compat(0.9.0): the command line's old spellings -- `contour label`, `contour workspace rename` -- and the
/// one table that rewrites them to the new ones before anything parses the line. The vendored command-line
/// parser has no hidden options, so the old spellings cannot simply stay registered beside the new ones.
/// Removed before 0.9.0, with every site that carries the marker.

#include <core/cli/CLI.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace contour::cli
{

/// compat(0.9.0): where a row's old tokens are looked for.
enum class LegacyPlace : uint8_t
{
    Verb = 0, ///< Right after the program name and the row's verb path: the verb itself.
    /// Where the command-line parser reads an option after the verb path, alone or as `--option=value`: not
    /// past a `--`, nor in a command's verbatim input, such as `contour new CMD ARGS...`.
    Option,
};

/// compat(0.9.0): one deprecated spelling of the command line, and what it is written as now.
struct LegacyArgumentRow
{
    LegacyPlace place = LegacyPlace::Verb; ///< Where the old tokens are looked for.
    /// The verb words the row applies under, right after the program name; empty for every verb.
    std::span<std::string_view const> verbPath {};
    /// The tokens of the old spelling. An option row has exactly one, the option.
    std::span<std::string_view const> oldTokens {};
    /// The tokens they are replaced by. An option row has exactly one, the option.
    std::span<std::string_view const> newTokens {};
    /// Put in front of the row's operand, e.g. `workspace:`; empty to leave it as written. An operand that
    /// already starts with it is left as written too.
    std::string_view operandPrefix {};
    /// A verb row's operand: the argument this far from the end (1 is the last), as the verb's operands
    /// trail its options. An option row's operand is the option's value instead.
    std::size_t operandFromEnd = 0;
    /// An operand that already starts with one of these, e.g. `tab:`, is left as written.
    std::span<std::string_view const> keptPrefixes {};
    std::string_view note {}; ///< What the user is told, naming the replacement.
};

/// The command line with its old spellings rewritten.
struct RewrittenArguments
{
    std::vector<std::string> arguments; ///< The rewritten arguments, the program name first.
    std::vector<std::string> notes;     ///< One deprecation note per row that applied.
};

/// compat(0.9.0): rewrites deprecated spellings; each applied row adds one note naming the replacement.
/// @param argv The command line as main() is given it, the program name first.
/// @param grammar The syntax the line is parsed against, which says where it reads options: an option row
///        rewrites nothing else, such as the same word in the command `contour new` runs.
/// @return The arguments to parse, and what to tell the user about them.
[[nodiscard]] RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv,
                                                        core::cli::Command const& grammar);

/// compat(0.9.0): rewrites the spellings @p rows name; rows apply in order, each to what the ones before it
/// left, so the first row that matches an argument wins.
/// @param argv The command line, the program name first.
/// @param grammar The syntax the line is parsed against. An option row's old option, which it lacks, is
///        read as one that takes a value.
/// @param rows The spellings to rewrite.
/// @return The arguments to parse, and one note per row that applied.
[[nodiscard]] RewrittenArguments rewriteLegacyArguments(std::span<char const* const> argv,
                                                        core::cli::Command const& grammar,
                                                        std::span<LegacyArgumentRow const> rows);

} // namespace contour::cli
