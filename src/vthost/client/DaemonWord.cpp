// SPDX-License-Identifier: Apache-2.0
#include <vthost/client/DaemonWord.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <vthost/DaemonSpawn.hpp>

namespace vthost::client
{

namespace
{
    using namespace std::string_view_literals;

    /// One spelling of a `--daemon` word.
    struct WordRow
    {
        DaemonWordKind kind;               ///< What a word of this spelling names.
        bool (*matches)(std::string_view); ///< Whether a word is of this spelling.
    };

    /// The spellings a word is read by, in order: a path that starts like one is a path whatever follows (a
    /// colon in `/run/user/1000/x:1`); then a URL, a `HOST:PORT`, and any other path (`C:\x\sock`, `dir/x`).
    constexpr auto WordRows = std::array {
        WordRow { DaemonWordKind::SocketPath,
                  [](std::string_view word) { return word.starts_with('/') || word.starts_with('.'); } },
        WordRow { DaemonWordKind::SshUrl, [](std::string_view word) { return word.contains("://"); } },
        WordRow { DaemonWordKind::HostPort,
                  [](std::string_view word) { return parseHostPort(word).has_value(); } },
        WordRow {
            DaemonWordKind::SocketPath,
            [](std::string_view word) { return word.find_first_of("/\\:") != std::string_view::npos; } },
    };

    /// The prefixes that say which kind of name a word is, whatever else of that name exists.
    struct PrefixRow
    {
        std::string_view prefix; ///< E.g. `name:`.
        DaemonWordKind kind;     ///< What the name after it names.
    };

    constexpr auto PrefixRows = std::array {
        PrefixRow { .prefix = "name:"sv, .kind = DaemonWordKind::LocalName },
        PrefixRow { .prefix = "host:"sv, .kind = DaemonWordKind::SavedHost },
    };

    /// @return Whether @p rest, what follows a word's first colon, is a port: then the word is `HOST:PORT`
    ///         whatever its host is called, `host:2222` and `name:22` included.
    [[nodiscard]] bool isPort(std::string_view rest) noexcept
    {
        return !rest.empty() && std::ranges::all_of(rest, [](char ch) { return ch >= '0' && ch <= '9'; });
    }

    [[nodiscard]] bool contains(std::function<bool(std::string_view)> const& lookup, std::string_view name)
    {
        return lookup && lookup(name);
    }
} // namespace

std::expected<DaemonTarget, DaemonWordError> resolveDaemonWord(std::string_view word,
                                                               LocalDaemons const& localDaemons,
                                                               SavedHosts const& savedHosts)
{
    if (auto const prefix = std::ranges::find_if(PrefixRows,
                                                 [word](PrefixRow const& row) {
                                                     return word.starts_with(row.prefix)
                                                            && !isPort(word.substr(row.prefix.size()));
                                                 });
        prefix != PrefixRows.end())
    {
        auto const name = word.substr(prefix->prefix.size());
        if (name.empty())
            return std::unexpected { DaemonWordError::EmptyName };
        return DaemonTarget { .kind = prefix->kind, .text = std::string { name } };
    }
    if (auto const row =
            std::ranges::find_if(WordRows, [word](WordRow const& each) { return each.matches(word); });
        row != WordRows.end())
        return DaemonTarget { .kind = row->kind, .text = std::string { word } };

    auto const local = contains(localDaemons.contains, word);
    if (local && contains(savedHosts.contains, word))
        return std::unexpected { DaemonWordError::Ambiguous };
    return DaemonTarget { .kind = local ? DaemonWordKind::LocalName : DaemonWordKind::SavedHost,
                          .text = std::string { word } };
}

std::string describe(DaemonWordError error, std::string_view word)
{
    switch (error)
    {
        case DaemonWordError::Ambiguous:
            return std::format(
                "'{0}' names both a local daemon and a saved host; say which: --daemon name:{0} "
                "or --daemon host:{0}",
                word);
        case DaemonWordError::EmptyName: return std::format("'{}' names nothing after the colon", word);
    }
    return std::format("'{}' names no daemon", word);
}

} // namespace vthost::client
