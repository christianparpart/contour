// SPDX-License-Identifier: Apache-2.0
#include <vthost/client/DaemonWord.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
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

    /// Where a prefix looks the name after it up.
    enum class PrefixLookup : uint8_t
    {
        LocalDaemons = 0, ///< LocalDaemons::contains.
        SavedHosts,       ///< SavedHosts::contains.
    };

    /// The prefixes that say which kind of name a word is, whatever else of that name exists.
    struct PrefixRow
    {
        std::string_view prefix; ///< E.g. `name:`.
        DaemonWordKind kind;     ///< What the name after it names.
        PrefixLookup lookup;     ///< Where a name of that kind is looked up.
    };

    constexpr auto PrefixRows = std::array {
        PrefixRow {
            .prefix = "name:"sv, .kind = DaemonWordKind::LocalName, .lookup = PrefixLookup::LocalDaemons },
        PrefixRow {
            .prefix = "host:"sv, .kind = DaemonWordKind::SavedHost, .lookup = PrefixLookup::SavedHosts },
    };

    [[nodiscard]] bool contains(std::function<bool(std::string_view)> const& lookup, std::string_view name)
    {
        return lookup && lookup(name);
    }

    /// @return Whether @p row takes @p word: it starts with the prefix, and what follows is no port (1-65535,
    ///         as parseHostPort() reads one) or names a daemon of the prefix's kind. So `host:2222` reaches a
    ///         machine called `host`, unless a saved host is called `2222`; `name:7` a local daemon called
    ///         `7` where one runs.
    [[nodiscard]] bool takes(PrefixRow const& row,
                             std::string_view word,
                             LocalDaemons const& localDaemons,
                             SavedHosts const& savedHosts)
    {
        if (!word.starts_with(row.prefix))
            return false;
        if (!parseHostPort(word))
            return true;
        auto const name = word.substr(row.prefix.size());
        switch (row.lookup)
        {
            case PrefixLookup::LocalDaemons: return contains(localDaemons.contains, name);
            case PrefixLookup::SavedHosts: return contains(savedHosts.contains, name);
        }
        return true;
    }
} // namespace

std::expected<DaemonTarget, DaemonWordError> resolveDaemonWord(std::string_view word,
                                                               LocalDaemons const& localDaemons,
                                                               SavedHosts const& savedHosts)
{
    if (auto const prefix = std::ranges::find_if(
            PrefixRows, [&](PrefixRow const& row) { return takes(row, word, localDaemons, savedHosts); });
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
