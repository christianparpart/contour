// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// What a test fixture says, on standard error, while it waits for a daemon that has much to start. A suite
/// killed by its timeout shows the log of no case that was still running; a fixture that streams what the
/// daemon has said so far leaves the reason a start stalls behind, where the run's own log keeps it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vthost::testing
{

/// Whether StartTrace says anything.
enum class StartTraceMode : uint8_t
{
    Off = 0,
    On,
};

/// What the daemon's log says of the sessions it started.
struct StartReading
{
    std::size_t spawned = 0; ///< Sessions it reports spawned.
    std::size_t failed = 0;  ///< Sessions it reports it could not spawn.
};

/// The words with which a daemon's log reports a session it could not spawn (SessionHost::seedSession(), and
/// HostedSession::start()).
inline constexpr auto SpawnFailureMarkers =
    std::array<std::string_view, 2> { "PTY factory failed", "device failed to start" };

/// The words with which a daemon's log reports a session it spawned (SessionHost::seedSession()).
inline constexpr auto SpawnedMarker = std::string_view { " spawned at " };

/// @param log A daemon's log.
/// @return Its lines.
[[nodiscard]] inline std::vector<std::string_view> linesOf(std::string_view log)
{
    return std::ranges::to<std::vector<std::string_view>>(
        log | std::views::split('\n')
        | std::views::transform([](auto part) { return std::string_view { part }; })
        | std::views::filter([](std::string_view line) { return !line.empty(); }));
}

/// @param line One line of a daemon's log.
/// @return Whether it reports a session that could not be spawned.
[[nodiscard]] inline bool reportsSpawnFailure(std::string_view line)
{
    return std::ranges::any_of(SpawnFailureMarkers,
                               [line](std::string_view marker) { return line.contains(marker); });
}

/// @param log A daemon's log.
/// @return How many sessions it reports spawned, and how many it could not.
[[nodiscard]] inline StartReading readStart(std::string_view log)
{
    auto reading = StartReading {};
    for (auto const line: linesOf(log))
    {
        if (reportsSpawnFailure(line))
            ++reading.failed;
        else if (line.contains(SpawnedMarker))
            ++reading.spawned;
    }
    return reading;
}

/// @param log A daemon's log.
/// @return The first line that reports a session it could not spawn; none when it reports no such.
[[nodiscard]] inline std::optional<std::string> firstSpawnFailure(std::string_view log)
{
    for (auto const line: linesOf(log))
        if (reportsSpawnFailure(line))
            return std::string { line };
    return std::nullopt;
}

/// Reports, every interval, how far a daemon's start has come: the sessions it spawned, those it could not,
/// and the tail of its log. Time is passed in, and the report goes to a sink, so that both are the test's to
/// choose.
class StartTrace
{
  public:
    /// Where a report goes: one line, without its newline.
    using Sink = std::function<void(std::string const&)>;

    /// How many lines of the log's tail a report shows.
    static constexpr auto TailLines = std::size_t { 3 };

    /// @param mode Whether to report at all.
    /// @param interval How often.
    /// @param started When the wait began.
    /// @param sink Where the reports go.
    StartTrace(StartTraceMode mode,
               std::chrono::milliseconds interval,
               std::chrono::steady_clock::time_point started,
               Sink sink):
        _mode { mode },
        _interval { interval },
        _started { started },
        _next { started + interval },
        _sink { std::move(sink) }
    {
    }

    /// Reports, if an interval has passed since the last report.
    /// @param now The time.
    /// @param log What the daemon has said so far; read only when a report is due.
    void observe(std::chrono::steady_clock::time_point now, std::function<std::string()> const& log)
    {
        if (_mode == StartTraceMode::Off || now < _next)
            return;
        _next = now + _interval;
        auto const text = log();
        auto const reading = readStart(text);
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _started);
        _sink(std::format(
            "[trace]   start: the daemon does not serve yet (+{} ms); {} sessions spawned, {} failed",
            elapsed.count(),
            reading.spawned,
            reading.failed));
        auto const lines = linesOf(text);
        for (auto const line: lines | std::views::drop(lines.size() - std::min(lines.size(), TailLines)))
            _sink(std::format("[trace]     | {}", line));
    }

  private:
    StartTraceMode _mode;
    std::chrono::milliseconds _interval;
    std::chrono::steady_clock::time_point _started;
    std::chrono::steady_clock::time_point _next;
    Sink _sink;
};

} // namespace vthost::testing
