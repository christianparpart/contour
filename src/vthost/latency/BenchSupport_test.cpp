// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <vthost/latency/BenchSupport.hpp>
#include <vthost/latency/EchoProtocol.hpp>

using namespace vthost::latency;
using namespace std::chrono_literals;

namespace
{
ProbeResult settledAs(ProbePhase phase, int awaitedColumn = -1)
{
    auto result = ProbeResult {};
    result.phase = phase;
    result.awaitedColumn = awaitedColumn;
    return result;
}
} // namespace

TEST_CASE("judgeProbe names the column a timed-out probe waited for", "[vthost][latency]")
{
    CHECK(judgeProbe(settledAs(ProbePhase::Finished)).has_value());

    auto const echo = judgeProbe(settledAs(ProbePhase::EchoTimedOut, 7));
    REQUIRE_FALSE(echo.has_value());
    CHECK(echo.error() == "probe timed out waiting for cursor column 7");

    auto const prompt = judgeProbe(settledAs(ProbePhase::PromptTimedOut));
    REQUIRE_FALSE(prompt.has_value());
    CHECK(prompt.error() == "probe timed out waiting for cursor column 2 (the prompt)");

    auto const focus = judgeProbe(settledAs(ProbePhase::FocusTimedOut, 3));
    REQUIRE_FALSE(focus.has_value());
    CHECK(focus.error() == "probe timed out waiting for the keyboard focus to type towards cursor column 3");

    CHECK_FALSE(judgeProbe(settledAs(ProbePhase::AwaitingEcho)).has_value());
}

TEST_CASE("only a probe that has said all it will is settled", "[vthost][latency]")
{
    CHECK(isSettled(ProbePhase::Finished));
    CHECK(isSettled(ProbePhase::PromptTimedOut));
    CHECK(isSettled(ProbePhase::EchoTimedOut));
    CHECK(isSettled(ProbePhase::FocusTimedOut));
    CHECK_FALSE(isSettled(ProbePhase::AwaitingPrompt));
    CHECK_FALSE(isSettled(ProbePhase::Pausing));
    CHECK_FALSE(isSettled(ProbePhase::AwaitingEcho));
}

TEST_CASE("medianOf takes the middle figure", "[vthost][latency]")
{
    CHECK(medianOf({}) == 0.0);
    CHECK(medianOf({ 5.0 }) == 5.0);
    CHECK(medianOf({ 9.0, 1.0, 5.0 }) == 5.0);
}

TEST_CASE("publish prints the table and the gate line, and writes the same to the record",
          "[vthost][latency]")
{
    auto report = BenchReport {};
    report.series = { MetricSeries {
        .metric = "keystroke", .mode = "daemon", .samples = { 100us, 300us, 200us } } };
    report.scalars = { ScalarSeries {
        .metric = "flood-mibps", .mode = "daemon", .values = { 10.0, 30.0, 20.0 } } };
    report.gate = GateResult { .startup = Verdict::Inconclusive, .keystroke = Verdict::Inconclusive };
    report.gateNote = "(informational)";

    auto const record = std::filesystem::temp_directory_path() / "bench-support-test-record.tsv";
    auto out = std::ostringstream {};
    REQUIRE(publish(report, record, out).has_value());

    auto const text = out.str();
    CHECK(text.starts_with("metric          mode         count     p50 us"));
    CHECK(text.contains("keystroke       daemon           3      200.0"));
    CHECK(text.contains("flood-mibps     daemon           3       20.0"));
    CHECK(text.contains("INCONCLUSIVE (informational)\n"));

    auto written = std::ostringstream {};
    written << std::ifstream(record, std::ios::binary).rdbuf();
    CHECK(written.str().starts_with("keystroke\tdaemon\t3\t200.0\t"));
    CHECK(written.str().contains("flood-mibps\tdaemon\t3\t20.0\n"));
    CHECK(written.str().contains("(informational)"));
    std::filesystem::remove(record);
}

TEST_CASE("publish says when the record cannot be written", "[vthost][latency]")
{
    auto out = std::ostringstream {};
    auto const result =
        publish(BenchReport {}, std::filesystem::path { "/nonexistent-directory/record.tsv" }, out);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("a child's failure names how it ended and what it printed", "[vthost][latency]")
{
    using vtpty::NormalExit;
    using vtpty::SignalExit;
    CHECK(describeChildFailure("no report", NormalExit { 3 }, "") == "no report (it exited with status 3)");
    CHECK(describeChildFailure("no report", SignalExit { 11 }, "boom")
          == "no report (it was killed by signal 11); its output ended with:\nboom");
    CHECK(describeExit(std::nullopt) == "it did not end");
}

#ifndef _WIN32
TEST_CASE("a benchmark child's last output and exit status are kept", "[vthost][latency]")
{
    auto process = BenchProcess::start("/bin/sh", { "-c", "echo first-words; echo last-words >&2; exit 7" });
    REQUIRE(process.has_value());
    auto const status = (*process)->waitFor(10s);
    REQUIRE(status.has_value());
    auto const* normal = std::get_if<vtpty::NormalExit>(&*status);
    REQUIRE(normal != nullptr);
    CHECK(normal->exitCode == 7);
    // The drain thread reads asynchronously; give it a moment to see the last line.
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while ((*process)->outputTail().find("last-words") == std::string::npos
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    CHECK((*process)->outputTail().find("last-words") != std::string::npos);
}

TEST_CASE("a benchmark child that fills its terminal is not left blocked on it", "[vthost][latency]")
{
    // A daemon logs to the terminal it runs on, and a pseudo terminal holds a kilobyte or so on macOS: with
    // nothing reading, the daemon stopped in the middle of a log line, before it served anything.
    constexpr auto Megabyte = std::size_t { 1024 } * 1024;
    auto const script = std::format("head -c {} /dev/zero", Megabyte);
    auto process = BenchProcess::start("/bin/sh", { "-c", script });
    REQUIRE(process.has_value());
    CHECK((*process)->waitFor(10s).has_value());
}
#endif
