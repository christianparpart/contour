// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `bench-latency e2e`: the real `contour` GUI, run in-process (`contour terminal`) and against a daemon
/// (`contour client`) with `--latency-probe`, and the budgets of both judged on what the GUIs report.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace vthost::latency
{

/// Whether a failed budget fails the run.
enum class GateMode : uint8_t
{
    Informational = 0, ///< The verdicts are printed; only a run that failed to measure fails.
    Enforced,          ///< The exit status is 0 only if both budgets pass.
};

/// Whether launches with no daemon running are measured too.
enum class ColdLaunches : uint8_t
{
    Skip = 0,
    Measure,
};

/// Whether the GUI's swaps wait for the vertical blank.
enum class FramePacing : uint8_t
{
    Unpaced = 0,
    VSync,
};

/// How `bench-latency e2e` runs.
struct E2eOptions
{
    std::filesystem::path contour;               ///< The `contour` executable to launch.
    std::size_t runs = 10;                       ///< Launches per mode, alternating.
    std::size_t keys = 50;                       ///< Measured keys per launch.
    GateMode gate = GateMode::Informational;     ///< Whether the budgets decide the exit status.
    ColdLaunches cold = ColdLaunches::Skip;      ///< Whether to measure launches with no daemon running.
    FramePacing pacing = FramePacing::Unpaced;   ///< Whether the GUI's swaps wait for the vertical blank.
    std::optional<std::filesystem::path> record; ///< Where to write the TSV record.
};

/// Which GUI a launch runs.
enum class LaunchMode : uint8_t
{
    InProcess = 0, ///< `contour terminal`.
    Daemon,        ///< `contour client`.
};

/// What one GUI launch is started with.
struct GuiLaunch
{
    LaunchMode mode = LaunchMode::InProcess; ///< Which GUI.
    std::filesystem::path socket;     ///< The benchmark's daemon socket, which a client launch attaches to.
    std::filesystem::path config;     ///< The configuration file both GUIs read.
    std::filesystem::path reportFile; ///< Where the GUI writes its probe report.
    std::size_t keys = 0;             ///< Measured keys.
    FramePacing pacing = FramePacing::Unpaced; ///< Whether the GUI's swaps wait for the vertical blank.
};

/// @param launch What the GUI is started with.
/// @return The `contour` arguments (without the program) that start the GUI of @p launch.
[[nodiscard]] std::vector<std::string> guiLaunchArguments(GuiLaunch const& launch);

/// Runs the benchmark and prints its table and the gate line.
/// @param given How it runs; the executable's path is made absolute, as the children start elsewhere.
/// @param out Where the table goes.
/// @param err Where a failure is described.
/// @return EXIT_SUCCESS; EXIT_FAILURE when a run failed to measure, or, with GateMode::Enforced, when a
///         budget failed or could not be judged.
[[nodiscard]] int runE2eBench(E2eOptions const& given, std::ostream& out, std::ostream& err);

} // namespace vthost::latency
