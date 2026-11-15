// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// compat(0.9.0): the control API's old spelling -- `label` keys, `object.setLabel`, `workspace.rename`,
/// `workspace.labelChanged` -- and the one table that maps it to the new one (`name`, `object.rename`,
/// `object.renamed`) and back. The daemon applies it to a client, and a client to a daemon, that did not
/// negotiate the native protocol's `names` capability, so old and new binaries keep talking during an
/// upgrade. The JSON is rewritten as a generic value, so the strict typed decoders on either side stay
/// strict. Removed before 0.9.0, with every site that carries the marker.

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace vtcontrol
{

/// compat(0.9.0): which spelling of the control API a peer speaks.
enum class Spelling : uint8_t
{
    Legacy =
        0, ///< `label`, `object.setLabel`, `workspace.labelChanged`: a peer without the `names` capability.
    Names = 1, ///< `name`, `object.rename`, `object.renamed`.
};

/// A JSON document that could not be rewritten between the spellings.
struct JsonError
{
    std::string message; ///< Why, for the user: glaze's error text, or what the document is not.
};

/// One renamed key of a JSON object.
struct KeyRename
{
    std::string_view legacyKey; ///< The key in the old spelling, e.g. `label`.
    std::string_view key;       ///< The key in the new spelling, e.g. `name`.
    /// The object kind a legacy value names implicitly (`workspace.rename`'s `workspace`): read from the old
    /// spelling it is prefixed `<kind>:` unless it names a kind already, and written to it the prefix is
    /// stripped. Empty for a value that is the same in both.
    std::string_view impliedKind {};
};

/// Whether a row also rewrites the new spelling into the old one.
enum class SpellingDirection : uint8_t
{
    Both = 0,       ///< Each way: the row is how a request of this method reaches an old daemon.
    FromLegacyOnly, ///< Only read from an old client: another row maps its method to the old spelling.
};

/// One method whose request is spelled differently, and how.
struct SpellingRow
{
    std::string_view legacyMethod;                         ///< The method in the old spelling.
    std::string_view method;                               ///< The method in the new spelling.
    std::span<KeyRename const> keys;                       ///< The request's renamed keys.
    SpellingDirection direction = SpellingDirection::Both; ///< Whether it maps back to the old spelling.
    std::string_view eventListKey {}; ///< A key whose array holds event names, which are renamed too.
};

/// Rewrites a request from the legacy spelling: method name and params keys.
/// @param method The method as the peer named it.
/// @param paramsJson Its params; empty reads as `{}`.
/// @return The method and params in the new spelling; unchanged when the method is not spelled differently.
[[nodiscard]] std::expected<std::pair<std::string, std::string>, JsonError> requestFromLegacy(
    std::string_view method, std::string_view paramsJson);

/// Rewrites a request to the legacy spelling, for a peer without Capability::Names.
/// @param method The method in the new spelling.
/// @param paramsJson Its params; empty reads as `{}`.
/// @return The method and params in the old spelling; unchanged when the method is not spelled differently.
[[nodiscard]] std::expected<std::pair<std::string, std::string>, JsonError> requestToLegacy(
    std::string_view method, std::string_view paramsJson);

/// Rewrites a reply to the legacy spelling: `name` is `label` in every object row (a workspace, tab or pane,
/// at any depth).
/// @param method The method answered, in the new spelling.
/// @param json The reply.
/// @return The reply in the old spelling; unchanged for a method whose reply lists no objects.
[[nodiscard]] std::expected<std::string, JsonError> replyToLegacy(std::string_view method,
                                                                  std::string_view json);

/// Rewrites a reply from the legacy spelling: the inverse of replyToLegacy().
/// @param method The method answered, in the new spelling.
/// @param json The reply.
/// @return The reply in the new spelling.
[[nodiscard]] std::expected<std::string, JsonError> replyFromLegacy(std::string_view method,
                                                                    std::string_view json);

/// The event a legacy subscriber knows, if any: object.renamed of a workspace -> workspace.labelChanged.
/// @param event The event's name in the new spelling.
/// @param payloadJson Its payload.
/// @return The event's name and payload in the old spelling, unchanged for an event spelled alike; nullopt
///         for one a legacy subscriber has no name for (the rename of a tab or a pane), which is not sent.
[[nodiscard]] std::optional<std::pair<std::string, std::string>> eventToLegacy(std::string_view event,
                                                                               std::string_view payloadJson);

/// The event a legacy daemon sent, in the new spelling: workspace.labelChanged -> object.renamed.
/// @param event The event's name in the old spelling.
/// @param payloadJson Its payload.
/// @param origin The event's origin of the kind the event is about (`workspace.labelChanged`'s workspace).
/// @return The event's name and payload in the new spelling, unchanged for an event spelled alike.
[[nodiscard]] std::expected<std::pair<std::string, std::string>, JsonError> eventFromLegacy(
    std::string_view event, std::string_view payloadJson, std::string_view origin);

/// eventToLegacy() of a whole event, as `vtcontrol::encodeEvent` writes it.
/// @param envelope The event's JSON.
/// @return It in the old spelling, or nullopt for an event a legacy subscriber is not sent.
[[nodiscard]] std::optional<std::string> encodedEventToLegacy(std::string_view envelope);

/// eventFromLegacy() of a whole event, as `vtcontrol::encodeEvent` writes it.
/// @param envelope The event's JSON in the old spelling.
/// @return It in the new spelling, or why it could not be read.
[[nodiscard]] std::expected<std::string, JsonError> encodedEventFromLegacy(std::string_view envelope);

} // namespace vtcontrol
