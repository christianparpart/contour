// SPDX-License-Identifier: Apache-2.0
// compat(0.9.0): the whole file goes with the old spelling.
#include <vtcontrol/LegacySpelling.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>

#include <vtcontrol/EnumTable.hpp>
#include <vtcontrol/Events.hpp>
#include <vtcontrol/Json.hpp>
#include <vtcontrol/ObjectRef.hpp>

namespace vtcontrol
{

namespace
{
    // {{{ The table

    /// compat(0.9.0): `label` is `name`.
    constexpr auto LabelIsName = std::array { KeyRename { .legacyKey = "label", .key = "name" } };

    /// compat(0.9.0): `workspace.rename` names its workspace by a bare spec, which `object.rename` takes as
    /// `workspace:<spec>`.
    constexpr auto RenameWorkspaceKeys = std::array {
        KeyRename { .legacyKey = "workspace", .key = "object", .impliedKind = "workspace" },
        KeyRename { .legacyKey = "label", .key = "name" },
    };

    /// compat(0.9.0): `workspace.switch` picks a local daemon by its name.
    constexpr auto SwitchKeys = std::array { KeyRename { .legacyKey = "toLabel", .key = "toName" } };

    /// compat(0.9.0): every request spelled differently. A method in neither column is spelled alike.
    constexpr auto SpellingTable = std::array {
        SpellingRow { .legacyMethod = "object.setLabel", .method = "object.rename", .keys = LabelIsName },
        // Read only: object.setLabel is how a rename reaches an old daemon, a workspace's included.
        SpellingRow { .legacyMethod = "workspace.rename",
                      .method = "object.rename",
                      .keys = RenameWorkspaceKeys,
                      .direction = SpellingDirection::FromLegacyOnly },
        SpellingRow { .legacyMethod = "tab.create", .method = "tab.create", .keys = LabelIsName },
        SpellingRow { .legacyMethod = "pane.split", .method = "pane.split", .keys = LabelIsName },
        SpellingRow { .legacyMethod = "workspace.create", .method = "workspace.create", .keys = LabelIsName },
        SpellingRow { .legacyMethod = "workspace.switch", .method = "workspace.switch", .keys = SwitchKeys },
        SpellingRow { .legacyMethod = "events.subscribe",
                      .method = "events.subscribe",
                      .keys = {},
                      .eventListKey = "kinds" },
    };

    /// compat(0.9.0): the methods whose replies list workspaces, tabs or panes, each spelling its name as
    /// ObjectRowKeys say.
    constexpr auto ObjectRowReplies = std::array { std::string_view { "workspace.list" } };

    /// compat(0.9.0): the keys of an object row (an object whose `id` is `<kind>:<uuid>`), at any depth.
    constexpr auto ObjectRowKeys = LabelIsName;

    /// compat(0.9.0): one event spelled differently.
    struct EventSpellingRow
    {
        std::string_view legacyEvent;    ///< Its name in the old spelling.
        std::string_view event;          ///< Its name in the new spelling.
        std::span<KeyRename const> keys; ///< Its payload's renamed keys.
        /// The kind of object the old event is about; the new one names it in its `object` key, the old one
        /// in its origin only, and is sent for no other kind.
        ObjectKind kind = ObjectKind::Workspace;
    };

    /// compat(0.9.0): every event spelled differently.
    constexpr auto EventSpellingTable = std::array {
        EventSpellingRow { .legacyEvent = "workspace.labelChanged",
                           .event = "object.renamed",
                           .keys = LabelIsName,
                           .kind = ObjectKind::Workspace },
    };

    /// The key of an event payload naming the object it is about.
    constexpr auto ObjectKey = std::string_view { "object" };

    // }}}
    // {{{ JSON

    using Json = glz::generic_u64;

    /// Which way a rewrite goes.
    enum class Towards : uint8_t
    {
        Names = 0, ///< From the old spelling to the new one.
        Legacy,    ///< From the new spelling to the old one.
    };

    [[nodiscard]] std::expected<Json, JsonError> parse(std::string_view text)
    {
        auto value = Json {};
        auto const document = text.empty() ? std::string_view { "{}" } : text;
        if (auto const error = glz::read_json(value, document))
            return std::unexpected(JsonError { .message = glz::format_error(error, document) });
        if (!value.is_object())
            return std::unexpected(JsonError { .message = "the document is not a JSON object" });
        return value;
    }

    [[nodiscard]] std::string write(Json const& value)
    {
        auto text = std::string {};
        // A generic value always writes.
        (void) glz::write_json(value, text);
        return text;
    }

    /// @return The key @p rename spells as on the side a rewrite @p towards reads from.
    [[nodiscard]] constexpr std::string_view fromKey(KeyRename const& rename, Towards towards) noexcept
    {
        return towards == Towards::Names ? rename.legacyKey : rename.key;
    }

    /// @return The key @p rename spells as on the side a rewrite @p towards writes to.
    [[nodiscard]] constexpr std::string_view toKey(KeyRename const& rename, Towards towards) noexcept
    {
        return towards == Towards::Names ? rename.key : rename.legacyKey;
    }

    /// @return Whether @p text starts with the name of an object kind and a colon.
    [[nodiscard]] bool namesKind(std::string_view text) noexcept
    {
        auto const colon = text.find(':');
        return colon != std::string_view::npos && enumFromName<ObjectKind>(text.substr(0, colon)).has_value();
    }

    /// Adds or strips the kind @p rename implies of a string @p value.
    void spellValue(KeyRename const& rename, Towards towards, Json& value)
    {
        if (rename.impliedKind.empty() || !value.is_string())
            return;
        auto& text = value.get_string();
        auto const prefix = std::format("{}:", rename.impliedKind);
        if (towards == Towards::Names && !namesKind(text))
            text.insert(0, prefix);
        else if (towards == Towards::Legacy && text.starts_with(prefix) && !parseObjectRef(text))
            text.erase(0, prefix.size());
    }

    /// Renames the keys of @p object, in place and in order, as @p keys say for a rewrite @p towards.
    void renameKeys(Json& object, std::span<KeyRename const> keys, Towards towards)
    {
        auto renamed = Json::object_t {};
        for (auto& [key, value]: object.get_object())
        {
            auto const rename =
                std::ranges::find(keys, std::string_view { key }, [towards](KeyRename const& row) {
                    return fromKey(row, towards);
                });
            if (rename == keys.end())
            {
                renamed[key] = std::move(value);
                continue;
            }
            spellValue(*rename, towards, value);
            renamed[std::string { toKey(*rename, towards) }] = std::move(value);
        }
        object.get_object() = std::move(renamed);
    }

    /// @return The row of the event named @p name in the spelling a rewrite @p towards reads, if any.
    [[nodiscard]] EventSpellingRow const* eventRowOf(std::string_view name, Towards towards) noexcept
    {
        auto const row = std::ranges::find_if(EventSpellingTable, [&](EventSpellingRow const& candidate) {
            return (towards == Towards::Names ? candidate.legacyEvent : candidate.event) == name;
        });
        return row == EventSpellingTable.end() ? nullptr : &*row;
    }

    /// @return The event's name in the spelling a rewrite @p towards writes, or @p name itself.
    [[nodiscard]] std::string_view eventNameTowards(std::string_view name, Towards towards) noexcept
    {
        auto const* row = eventRowOf(name, towards);
        if (row == nullptr)
            return name;
        return towards == Towards::Names ? row->event : row->legacyEvent;
    }

    /// Renames the event names in the array @p row's eventListKey holds in @p object.
    void renameEventList(Json& object, SpellingRow const& row, Towards towards)
    {
        if (row.eventListKey.empty())
            return;
        auto& members = object.get_object();
        auto const list = members.find(row.eventListKey);
        if (list == members.end() || !list->second.is_array())
            return;
        for (auto& name: list->second.get_array())
            if (name.is_string())
                name.get_string() = std::string { eventNameTowards(name.get_string(), towards) };
    }

    /// Renames ObjectRowKeys in every object row of @p value, at any depth.
    void spellObjectRows(Json& value, Towards towards)
    {
        if (value.is_array())
        {
            for (auto& element: value.get_array())
                spellObjectRows(element, towards);
            return;
        }
        if (!value.is_object())
            return;
        for (auto& member: value.get_object() | std::views::values)
            spellObjectRows(member, towards);
        auto const& members = value.get_object();
        auto const id = members.find(std::string_view { "id" });
        if (id != members.end() && id->second.is_string() && namesKind(id->second.get_string()))
            renameKeys(value, ObjectRowKeys, towards);
    }

    [[nodiscard]] std::expected<std::pair<std::string, std::string>, JsonError> rewriteRequest(
        SpellingRow const& row, std::string_view paramsJson, Towards towards)
    {
        return parse(paramsJson).transform([&](Json params) {
            renameKeys(params, row.keys, towards);
            renameEventList(params, row, towards);
            auto const method = towards == Towards::Names ? row.method : row.legacyMethod;
            return std::pair { std::string { method }, write(params) };
        });
    }

    [[nodiscard]] std::expected<std::string, JsonError> rewriteReply(std::string_view method,
                                                                     std::string_view json,
                                                                     Towards towards)
    {
        if (!std::ranges::contains(ObjectRowReplies, method))
            return std::string { json };
        return parse(json).transform([towards](Json reply) {
            spellObjectRows(reply, towards);
            return write(reply);
        });
    }

    /// @return The origin of @p envelope that names an object of @p kind.
    [[nodiscard]] std::string_view originOf(EventEnvelope const& envelope, ObjectKind kind) noexcept
    {
        switch (kind)
        {
            case ObjectKind::Workspace: return envelope.workspace;
            case ObjectKind::Tab: return envelope.tab;
            case ObjectKind::Pane: return envelope.pane;
        }
        return {};
    }

    // }}}
} // namespace

std::expected<std::pair<std::string, std::string>, JsonError> requestFromLegacy(std::string_view method,
                                                                                std::string_view paramsJson)
{
    auto const row = std::ranges::find(SpellingTable, method, &SpellingRow::legacyMethod);
    if (row == SpellingTable.end())
        return std::pair { std::string { method }, std::string { paramsJson } };
    return rewriteRequest(*row, paramsJson, Towards::Names);
}

std::expected<std::pair<std::string, std::string>, JsonError> requestToLegacy(std::string_view method,
                                                                              std::string_view paramsJson)
{
    auto const row = std::ranges::find_if(SpellingTable, [method](SpellingRow const& candidate) {
        return candidate.method == method && candidate.direction == SpellingDirection::Both;
    });
    if (row == SpellingTable.end())
        return std::pair { std::string { method }, std::string { paramsJson } };
    return rewriteRequest(*row, paramsJson, Towards::Legacy);
}

std::expected<std::string, JsonError> replyToLegacy(std::string_view method, std::string_view json)
{
    return rewriteReply(method, json, Towards::Legacy);
}

std::expected<std::string, JsonError> replyFromLegacy(std::string_view method, std::string_view json)
{
    return rewriteReply(method, json, Towards::Names);
}

std::optional<std::pair<std::string, std::string>> eventToLegacy(std::string_view event,
                                                                 std::string_view payloadJson)
{
    auto const* row = eventRowOf(event, Towards::Legacy);
    if (row == nullptr)
        return std::pair { std::string { event }, std::string { payloadJson } };
    auto payload = parse(payloadJson);
    if (!payload)
        return std::nullopt;
    auto& members = payload->get_object();
    auto const object = members.find(ObjectKey);
    if (object == members.end() || !object->second.is_string())
        return std::nullopt;
    auto const ref = parseObjectRef(object->second.get_string());
    if (!ref || ref->kind != row->kind)
        return std::nullopt;
    members.erase(object);
    renameKeys(*payload, row->keys, Towards::Legacy);
    return std::pair { std::string { row->legacyEvent }, write(*payload) };
}

std::expected<std::pair<std::string, std::string>, JsonError> eventFromLegacy(std::string_view event,
                                                                              std::string_view payloadJson,
                                                                              std::string_view origin)
{
    auto const* row = eventRowOf(event, Towards::Names);
    if (row == nullptr)
        return std::pair { std::string { event }, std::string { payloadJson } };
    return parse(payloadJson).transform([&](Json payload) {
        renameKeys(payload, row->keys, Towards::Names);
        // The object first, as the new event spells it.
        auto withObject = Json {};
        withObject[ObjectKey] = std::string { origin };
        for (auto& [key, value]: payload.get_object())
            withObject.get_object()[key] = std::move(value);
        return std::pair { std::string { row->event }, write(withObject) };
    });
}

std::optional<std::string> encodedEventToLegacy(std::string_view envelope)
{
    auto decoded = decodeJson<EventEnvelope>(envelope);
    // Not one this side wrote; passed on as it is.
    if (!decoded)
        return std::string { envelope };
    if (eventRowOf(decoded->type, Towards::Legacy) == nullptr)
        return std::string { envelope };
    return eventToLegacy(decoded->type, decoded->payload.text)
        .transform([&](std::pair<std::string, std::string> legacy) {
            decoded->type = std::move(legacy.first);
            decoded->payload.text = std::move(legacy.second);
            return encodeJson(*decoded);
        });
}

std::expected<std::string, JsonError> encodedEventFromLegacy(std::string_view envelope)
{
    auto decoded = decodeJson<EventEnvelope>(envelope);
    if (!decoded)
        return std::unexpected(JsonError { .message = decoded.error().message });
    auto const* row = eventRowOf(decoded->type, Towards::Names);
    if (row == nullptr)
        return std::string { envelope };
    return eventFromLegacy(decoded->type, decoded->payload.text, originOf(*decoded, row->kind))
        .transform([&](std::pair<std::string, std::string> named) {
            decoded->type = std::move(named.first);
            decoded->payload.text = std::move(named.second);
            return encodeJson(*decoded);
        });
}

} // namespace vtcontrol
