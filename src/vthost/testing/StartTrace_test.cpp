// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <vthost/testing/StartTrace.hpp>

using namespace std::chrono_literals;
using vthost::testing::StartTrace;
using vthost::testing::StartTraceMode;

namespace
{
constexpr auto Log =
    std::string_view { "session 1 spawned at 80x24 (1 live)\n"
                       "session 2 spawned at 80x24 (2 live)\n"
                       "session 3: PTY factory failed; no session created\n"
                       "session 4: device failed to start: the keeper is out of descriptors\n"
                       "session 5 spawned at 80x24 (3 live)\n" };
} // namespace

TEST_CASE("StartTrace.readStart counts spawned and failed sessions", "[keeper][trace]")
{
    auto const reading = vthost::testing::readStart(Log);
    CHECK(reading.spawned == 3);
    CHECK(reading.failed == 2);
    CHECK(vthost::testing::readStart("").spawned == 0);
}

TEST_CASE("StartTrace.firstSpawnFailure names the first failure, or none", "[keeper][trace]")
{
    CHECK(vthost::testing::firstSpawnFailure(Log) == "session 3: PTY factory failed; no session created");
    CHECK_FALSE(vthost::testing::firstSpawnFailure("session 1 spawned at 80x24 (1 live)\n").has_value());
}

TEST_CASE("StartTrace reports once per interval, with the tail of the log", "[keeper][trace]")
{
    auto const origin = std::chrono::steady_clock::time_point {};
    auto lines = std::vector<std::string> {};
    auto reads = 0;
    auto trace = StartTrace { StartTraceMode::On, 10s, origin, [&](std::string const& line) {
                                 lines.push_back(line);
                             } };
    auto const log = [&] {
        ++reads;
        return std::string { Log };
    };

    trace.observe(origin + 9s, log);
    CHECK(lines.empty());
    CHECK(reads == 0); // the log is not read before a report is due

    trace.observe(origin + 10s, log);
    REQUIRE(lines.size() == 1 + StartTrace::TailLines);
    CHECK(lines.front()
          == "[trace]   start: the daemon does not serve yet (+10000 ms); 3 sessions spawned, 2 failed");
    CHECK(lines.back() == "[trace]     | session 5 spawned at 80x24 (3 live)");

    trace.observe(origin + 15s, log);
    CHECK(lines.size() == 1 + StartTrace::TailLines);
    trace.observe(origin + 20s, log);
    CHECK(lines.size() == 2 * (1 + StartTrace::TailLines));
}

TEST_CASE("StartTrace says nothing when off", "[keeper][trace]")
{
    auto const origin = std::chrono::steady_clock::time_point {};
    auto said = false;
    auto trace = StartTrace { StartTraceMode::Off, 1s, origin, [&](std::string const&) { said = true; } };
    trace.observe(origin + 1h, [] { return std::string { Log }; });
    CHECK_FALSE(said);
}
