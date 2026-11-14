# Agent Orchestrator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a daemon-scoped WebAssembly extension that lists the user's open PRs/MRs and unclaimed
issues, dispatches Claude Code agents into fresh git worktrees in their own tabs, and is driven from a
terminal tab it serves.

**Architecture:** All orchestrator logic lives in one sans-IO C++23 library, `orchestrator_core`, that
maps inputs to effects. It is built natively — where Catch2 tests it and a prototype drives it over
today's control socket (phase 0) — and cross-compiled to `wasm32-wasip1` as the guest (phase 1 on).
The tab is drawn with core-cpp's `core::tui`, which first needs an upstream work package (phase U).
The host grows a second extension world, `terminal-extensions:daemon@0.1.0`, whose services are
injected interfaces; its events are payload-less hints and all data is pulled.

**Tech Stack:** C++23 everywhere (guest and host), Catch2, glaze 9.0.0 for JSON, core-cpp `core::tui`,
wasi-sdk ≥ 34 / clang ≥ 20 targeting `wasm32-wasip1`, `wit-bindgen c` 0.63.0, WAMR fast interpreter,
CMake presets (`clang-asan`), `gh` ≥ 2.95, git ≥ 2.40, Claude Code ≥ 2.1.

**Spec:** `docs/superpowers/specs/2026-11-14-agent-orchestrator-design.md`. Section and item numbers
(§5.2, C12, D3, R2) refer to it.

## How this plan is layered

The work is five phases, each usable on its own, plus phase U — an upstream work package in core-cpp that
runs in parallel with phases 0 and 1 and gates phase 2. **Phase 0 is specified to step level** below and can be
executed as written. **Phases 1–4 are specified to task level**: files, interfaces, tests, and the gate
that closes the phase. Their step-level expansion is the first task of each phase, written against the
code as it stands then — the later phases depend on facts earlier ones produce (the R2 measurement, the
spike answers, the shape the daemon world's codec takes), and steps written today would be guesses.
Do not start a phase whose predecessor's gate has not been met.

## Global Constraints

- **Do not edit `vendor/core-cpp`.** A defect there is fixed upstream and re-vendored.
- **Layering.** `vtextension` links `vtbackend vtcontrol vtpty core::*` and nothing higher, so nothing
  in `vtpty` may name a `vtextension` type. `vthost` sits above `vtextension`, links no Qt, and **must
  build with `CONTOUR_EXTENSIONS=OFF` with the same layout**: `SessionHost` and its peers see only
  `vthost`-owned interfaces (the model is `IExtensionHosting` in `src/vthost/ExtensionHosting.hpp`), and
  only `DaemonExtensions`, compiled when extensions are on, includes `vtextension`.
  `contour_config`, `contour_command`, `contour_cli` link no Qt. WAMR is linked only from
  `vtextension_wamr` (the `check_wasm_runtime_containment` lint enforces it).
- **No glaze in `vtextension`'s public headers** (`vtextension_public_headers_test` enforces it).
- **Zero warnings.** `-Werror`, clang-tidy clean, no `NOLINT`, no `#pragma` mutes.
- **C++ rules from `AGENT.md`:** C++23; no C-style loops; `std::expected<T, E>` for fallible API with a
  per-subsystem error enum; `enum class : uint8_t` instead of `bool` in API surface (zero = the
  negative case); everything touching I/O, time or processes behind an injected interface — **clocks
  and timeouts included, so no test waits on a real 30 s timer**; complete constructors, no
  `init()`/setters for configuration; behaviour as table rows; Doxygen on new public API;
  `[[nodiscard]]`; `.hpp` with `#pragma once`.
- **Every code change lands with tests**, in the module's Catch2 `*_test`.
- **Guest code rules** — everything under `extensions/orchestrator/` that is compiled into the wasm
  module (the core, the view, the guest driver) obeys these in addition to `AGENT.md`, because a
  violation either cannot load or turns into a guest fault at run time:
  - **No exceptions:** no `throw`, `try`, `catch`. The module is built `-fno-exceptions`; WAMR here has
    no exception-handling support. Fallible functions return `std::expected`.
  - **Nothing that aborts on bad input:** no `.at(`, no `.value()` on an `optional`/`expected`
    (check, then use `*`), no `std::vformat` with a run-time format string, no `std::stoi` family
    (use `std::from_chars`).
  - **No `<iostream>`, `<fstream>` or `<filesystem>` of our own** (`<iostream>` imports `fd_read`,
    which the host refuses), no `<thread>`/`<mutex>`/`<future>`, no `dynamic_cast`/`typeid`
    (`-fno-rtti`), no `std::regex`, no `std::locale`.
  - **No clock, randomness or environment read directly:** they arrive as inputs.
  - The portability lint test (task 0.1) enforces the greppable half on every machine.
- **Never `co_await` inside a Catch2 assertion.** Await into a local, then assert.
- **Build with four jobs:** `cmake --build --preset clang-asan -j4`. Never unrestricted.
- **Formatting:** CI checks with clang-format **22**; the local pre-commit hook runs 23. Format with 22
  (or confirm the diff is identical) before pushing.
- **UI work** (chrome marker, header strip, palette rows) follows `.agent/rules/ui-ux.md`.
- **After each task's code changes** run `/simplify`. Each phase's change summary reports performance
  impact, a risk assessment and coverage.
- **`vendor/core-cpp` changes only by re-vendoring a released tag** (phase U).
- **Manifest grammar limits** (version 1 and 2): an argv word cannot contain a space; `{` and `}` may
  appear only as a whole-word `{name}` placeholder.
- **No declared command takes a directory as an argument.** No `git -C`. Every command and pane
  template is bound to one root and runs in a directory the host validates to be inside it. Every
  placeholder has a declared kind (§5.1).
- **A driver is single-threaded.** It executes effects and feeds inputs strictly in order; a pane
  snapshot is taken only after every earlier effect has been carried out. (A snapshot taken before
  `pane-open` returned but fed after it would make a brand-new agent look gone.)
- **Host limits:** 64 KiB per host call (`maxHostCallBytes`); hints coalesce, one slot per kind.
- **The orchestrator imports no control verbs.** Not `pane-send`, not `pane-capture`, not any.
- **The agent pane template contains no permission flag and no `--settings`.**
- **Names never come from titles.** Branch and directory names are built from numbers, Jira keys and
  timestamps only. Prompts carry the item's URL and the user's note, never its title or body.
- **Windows is out of scope**; code must still compile there. Guard with the existing platform seams.

## Verified facts (measured on 2026-11-14 — do not re-derive)

| Fact | How it was established |
| --- | --- |
| `git worktree remove` refuses a worktree with an untracked, non-ignored file; succeeds when the file is ignored, and deletes ignored files silently | scratch repository, git 2.56 |
| In a linked worktree `git rev-parse --git-path info/exclude` is the **main** repository's file | same |
| A branch checked out in one worktree cannot be checked out in another | same |
| Removing a worktree with unpushed commits succeeds and keeps the branch | same |
| `git worktree list` prints **resolved** paths: a worktree added through a symlink is listed under the real directory | scratch repository (independent review) |
| `git symbolic-ref refs/remotes/origin/HEAD` fails when that ref was never set (`git init` + `remote add`) | same |
| `git push` from a local branch whose name differs from its upstream's fails under the default `push.default=simple`; it succeeds with `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=push.default GIT_CONFIG_VALUE_0=upstream` in the environment | scratch repository with a bare remote |
| `gh pr checkout` has `--branch <name>`, `--detach`, `--force` | gh 2.95 `--help` |
| `gh pr list --json` offers every field §6.1 uses, including `statusCheckRollup` and `closingIssuesReferences` | gh 2.95 `--help` and a live call |
| A `statusCheckRollup` entry is `{"__typename":"CheckRun","name","status","conclusion","workflowName",…}`; `conclusion` is `""` while `status` is `IN_PROGRESS`; `reviewDecision` is `""` when no review is required | live call on this repository |
| One PR here carries ~33 checks ≈ 10 KiB of JSON; five open PRs ≈ 52 KiB | same |
| `claude` 2.1.325 accepts `--session-id <uuid>`, `--model`, `--settings`, `--permission-mode`, `-n/--name` | `claude --help` |
| `PostToolUse` hooks can add model context with `{"hookSpecificOutput":{"hookEventName":"PostToolUse","additionalContext":"…"}}`; hooks inherit the launching environment | Claude Code hooks documentation |
| `tab.create` takes `{workspace, placement, cwd, command: [..], label, focus}`, has no environment field, labels the tab, and defaults to taking focus | `src/vtcontrol/Api.hpp:413-422` |
| `pane.reportStatus` is `Method<ReportStatusRequest, EmptyReply>`; `runAgentReport` discards the reply | `src/vtcontrol/Catalog.hpp:372`, `src/contour/cli/AgentReport.cpp:199` |
| `git worktree add -b <b> <path> origin/master` makes `<b>` track `master`; with `push.default=upstream`, `git push --dry-run` then prints `<b> -> master`. With `--no-track` and `push.autoSetupRemote=true` it prints `[new branch] <b> -> <b>` | scratch repository (second review) |
| A worktree whose `.git` file is rewritten to point at a directory with `core.fsmonitor="touch PWNED"` runs that command on `git status --porcelain` | same |
| With `GIT_ALLOW_PROTOCOL=https:ssh:git`, a local-path or `file://` remote fails (`transport 'file' not allowed`); scp-style remotes count as ssh | same |
| A FIFO named `STATUS.md` is not reported by `git status --porcelain` | same |
| `git worktree add` creates missing parent directories | same |
| Control JSON is read with unknown keys as errors; `pane.reportStatus` is `Exposure::Everywhere` and a WIT import (`wit/control/world.wit:456`) | `src/vtcontrol/Json.hpp:73-74` |
| WAMR's native registry is process-wide; the code relies on every live runtime providing each host function alike; the world check accepts a whole import module by name | `wasm/wamr/WamrRuntime.hpp:103-105`, `wasm/WasmRuntime.hpp:48-56`, `CompiledExtension.cpp:74-80` |
| When a pane's spawned program exits, the session ends and the pane closes | `SessionHost` `onClosed` → `closeSession` |
| The daemon draws the indicator row only when `Terminal::indicatorStatusLineDependsOnHostState` is true, which needs `_extensions` to be non-null | `Terminal.cpp:868-878`, `NativeSession.cpp:1508` |
| PDU tags 60–62 and capability number 30 are free (last used: 59 and 29) | `src/vthost/proto/Pdu.hpp`, `Negotiation.hpp` |
| A C++23 reactor (`std::expected`, `std::format`, ranges, `std::variant`) built with wasi-sdk 34 loads and runs under Contour's own WAMR 2.4.5 library: `load`, `update`, `render`, host calls both ways | experiment in a scratch directory, through a small C harness — not through `CompiledExtension` |
| The command that works: `clang++ --target=wasm32-wasip1 --sysroot=<sysroot> -std=c++23 -Os -flto -fno-exceptions -fno-rtti -fvisibility=hidden -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=65536` | same |
| With `-fwasm-exceptions` the module gets a tag section and WAMR refuses it (`invalid section id`); without exception support `throw` does not link | same |
| `<iostream>` pulls the WASI import `fd_read`, which the host's allow-list refuses | same |
| `__attribute__((section(".custom_section.…")))` creates **no** custom section. Module-level asm does: `__asm__(".section \".custom_section.terminal-extension-manifest\",\"\",@\n.ascii \"…\"\n.text\n");` — the section name must be quoted; a `.S` file with `.incbin` also works; it survives LTO, `--strip-all` and `wasm-opt` | same |
| Exports use `[[clang::export_name("load")]]` (`[[gnu::export_name]]` is unknown); imports use `__attribute__((import_module("…"), import_name("…")))` | same |
| The sysroot's libc/libc++ use bulk-memory, sign-ext, nontrapping-fptoint and the overlong `call_indirect` encoding; Contour's bundled WAMR accepts them; no `-mno-…` flag is needed or would help | same |
| Module size: a `std::format` call costs ≈ 400 KiB (≈ 240 KiB after `wasm-opt -Os`); glaze `read_json` adds ≈ 32 KiB and no imports, and compiles with `-fno-exceptions -fno-rtti` | same |
| `wit-bindgen c` 0.63.0 on `wit/statusline` emits names and signatures identical to the host's; a guest built on it runs as a plain core module; the link needs the generated `*_component_type.o` | same |
| The host calls `_initialize` itself; a start section, or an export named `__wasm_call_ctors`, is refused | `Instance.cpp:300-306`, `CompiledExtension.cpp:127`, `wasm/wamr/HiddenExports.hpp:75-84` |
| Arch packages the pieces: `wasi-libc`, `wasi-libc++`, `wasi-libc++abi`, `wasi-compiler-rt`, `wit-bindgen`, `wasm-tools`, `binaryen`. **None is installed on the development machine**, and both local build trees have `CONTOUR_EXTENSIONS_BUNDLED=OFF` | `pacman`, `CMakeCache.txt` |
| Contour vendors core-cpp **v0.19.0** with modules `base;log;cli;platform;async;net;testing` and sets `CORE_CPP_WITH_TUI OFF`; upstream is at v0.20.0, where `codec` is a new mandatory module | `vendor/core-cpp/MANIFEST`, `cmake/ContourThirdParties.cmake:467` |
| In core-cpp v0.20.0, `VtParser::feed(std::string_view) -> std::vector<InputEvent>` is incremental and touches no descriptor; `Buffer`, `Canvas` and the widgets render with explicit sizes; `Screen` requires a `Terminal&`; `tui` links `platform`, `async`, `net` and libunicode; WASI is not a known platform; `Buffer::at` throws; `Terminal` uses `dynamic_cast` | read in a clone of core-cpp — **nothing in `core::tui` was compiled for wasm** |
| `glab`, `acli`, `jira` are not installed on the development machine; their flags are unverified | `command -v` |

## Prerequisites

- **Before phase 0:** nothing new to install. The core and its tests are native C++ and glaze is
  already a dependency. For the prototype and the spikes: `gh` logged in, a running Contour daemon
  (`$CONTOUR_DAEMON` set in the pane) with `contour agent setup` installed, and the
  `contour-workflows` Claude Code plugin the prompt templates name.
- **Phase U starts with phase 0**, in the core-cpp repository. It needs a maintainer's agreement on
  the shape of the headless TUI target before code is written.
- **Before phase 1:** a WASI C++ toolchain — on Arch `wasi-libc wasi-libc++ wasi-libc++abi
  wasi-compiler-rt` (plus `wit-bindgen` to regenerate bindings and, optionally, `binaryen` for
  `wasm-opt`), or an unpacked wasi-sdk ≥ 34 pointed at by `WASI_SDK_PREFIX` — and a tree configured
  with `-DCONTOUR_EXTENSIONS_ORCHESTRATOR=ON`. The Rust toolchain is still what builds the *other*
  bundled guests; the orchestrator does not need it.
- **Before phase 2:** phase U released and re-vendored.

## Review Focus

1. **The extension must not gain a way to type, read a screen, choose Claude's flags, or name a
   directory or remote the user did not configure.** Pinned by typed arguments (0.3, 1.1, 1.4), by the
   manifest having no control line, and by the security tests of phases 2–4.
2. **A lost hint or a guest restart must never lose data, leak a worktree, or remove one under a
   running agent.** Pinned in phase 0 by the recovery and lost-hint scenarios (0.9).
3. **A run is never forgotten because of a path comparison.** Only a command that cannot start in the
   worktree proves it is gone (0.7, 0.8).
4. **Nothing is ever forced.** No `--force` on `worktree remove`, `checkout` or `push` appears in any
   declared command. Pinned by a test over the command table (0.3).
5. **Live upgrade keeps working with a served tab open** (C12). Pinned in phase 2.
   **And no git command runs in a worktree whose `.git` file an agent changed** (0.7, 0.9 scenario 19).
8. **Guest faults.** In C++ without exceptions a careless `.at()` is a fault, and three faults disable
   the extension. Review every index and every `expected` access; the lint test catches the spelling,
   not the logic.
9. **Re-vendoring core-cpp** (phase U) moves every vendored module, not only `tui`. Its diff against
   Contour's use of `core::net`/`core::async` is reviewed on its own.
6. **Untrusted text never reaches a prompt, a path or the terminal unsanitised.** Pinned in phase 0.
7. **The 50 ms budget** (R2). Measured in phase 1 before the guest's polling is trusted.

## File Structure

```
extensions/orchestrator/               C++23; namespace orchestrator
  CMakeLists.txt                       native: orchestrator_core, orchestrator_test, orchestrator_proto
  core/                                library orchestrator_core — all logic, no I/O
    Utf8.hpp                           decode/encode by hand (no <locale>)
    Sanitize.{hpp,cpp}                 control-character stripping, truncation by character count
    Model.hpp                          ChangeRequest, Issue, CiState, ReviewState, ItemRef
    Commands.{hpp,cpp}                 the declared command, pane and argument tables (single source of truth)
    GitHub.{hpp,cpp}                   gh's JSON → the model (glaze)
    Config.{hpp,cpp}                   the config JSON → Config, with validation (glaze)
    Naming.{hpp,cpp}                   branch, worktree and run names; Jira key check
    Prompt.{hpp,cpp}                   prompt building
    Run.{hpp,cpp}                      Run, RunState, the per-run state machine
    Reconcile.{hpp,cpp}                stored runs × panes → decisions; orphan worktrees
    Engine.{hpp,cpp}                   Engine::step(Input) -> std::vector<Effect>
    *_test.cpp, test_main.cpp          Catch2; testdata/ holds recorded CLI output per provider
  view/                                (phase 2) library orchestrator_view on core::tui
  proto/Main.cpp                       (phase 0) native prototype driver, POSIX, never installed
  manifest/Main.cpp                    (phase 1) native tool: prints the manifest from Commands
  guest/                               (phase 1) the wasm module: CMake sub-build, generated bindings,
                                       Guest.cpp (effects → host calls), Manifest.S (.incbin)
cmake/ContourWasiCxx.cmake             (phase 1) finds the toolchain, drives the cross sub-build
cmake/toolchains/wasm32-wasip1.cmake   (phase 1)
scripts/check-guest-portability.cmake  (phase 0) the lint over extensions/orchestrator
src/vtextension/ArgumentKinds.hpp      (phase 0) header-only argument-kind rules, shared by host and guest
wit/daemon/world.wit                   (phase 1) terminal-extensions:daemon@0.1.0
src/vtpty/                             (phase 1) IProcessRunner: raw spawn with captured output
src/vtextension/                       (phase 1+) Manifest v2, DaemonWorld.hpp, the daemon Engine and
                                       instance, DaemonExtensionRunner, the guest-facing service
                                       interfaces and the declared-command policy
src/vthost/                            (phase 1+) vthost-owned host interfaces, DaemonExtensions adapters,
                                       ownership registry, served sessions, handover, PDUs, refusals
src/vtbackend/screen/                  (phase 1) the {DaemonExtension:<name>} status-line item
src/contour/config/                    (phase 1) daemon.extensions
src/contour/cli/                       (phase 1) extension verbs; (phase 4) agent transcript, inbox print
src/contour/command/, window/, qml/    (phase 2) ExtensionCommandSource, marker, header strip
vendor/core-cpp/                       (phase U) re-vendored at a release that has the headless TUI
```

---

# Phase 0 — Core, prototype and spikes

One additive header in `src/vtextension`; otherwise no host changes. Ends with a tested core, a fixture
corpus, a prototype the user has tried, and the undocumented behaviours answered.

**Build and test commands used by every phase-0 task**

- Build: `cmake --build --preset clang-asan -j4 --target orchestrator_test`
- Test: `out/clang-asan/extensions/orchestrator/orchestrator_test` (one case: add `"<name>"`), or
  `ctest --preset=clang-asan -R "orchestrator"`
- Lint: `ctest --preset=clang-asan -R orchestrator_guest_portability`
- Format: `clang-format` (version 22) on every touched file; clang-tidy runs with the pedantic build.

**Conventions for every phase-0 file.** Namespace `orchestrator`. `.hpp` with `#pragma once`. Doxygen
on every public declaration (the interface blocks below omit it for brevity). `[[nodiscard]]` on every
function whose result matters. Types `CamelCase`, everything else `camelBack`. `enum class … :
uint8_t` with the negative case as zero. No `bool` parameter or data member — the blocks below already
use enums; a `bool` *return* is used only where the function name asks the question. The Guest code
rules (Global Constraints) apply to everything except `*_test.cpp`, `test_main.cpp` and `proto/`.
Tests are table-driven: a `constexpr`/`static const` array of rows and one range-`for` with
`CAPTURE(row…)`.

### Task 0.1: The library, its test target, and the portability lint

**Files:**
- Create: `extensions/orchestrator/CMakeLists.txt`, `extensions/orchestrator/core/test_main.cpp`,
  `extensions/orchestrator/core/Utf8.hpp`, `extensions/orchestrator/core/Utf8_test.cpp`,
  `scripts/check-guest-portability.cmake`
- Modify: `CMakeLists.txt` (root)

**Interfaces:**
- Produces: targets `orchestrator_core` (static library), `orchestrator_test`; CTest tests
  `orchestrator_test` and `orchestrator_guest_portability`; and

```cpp
// Utf8.hpp — header-only
struct Decoded { char32_t codepoint; std::size_t length; };          // length ≥ 1, always advances
/// Decodes the scalar at the front of @p text; an invalid or truncated sequence yields U+FFFD, length 1.
[[nodiscard]] constexpr Decoded decodeUtf8(std::string_view text) noexcept;
constexpr void appendUtf8(std::string& out, char32_t codepoint);
```

- [ ] **Step 1: Read how the tree wires things.** `CMakeLists.txt:410-430` (how `extensions/` is
  added and under which option), `src/vtcontrol/CMakeLists.txt:55-75` (the model for a Catch2 target:
  libraries, `contour_test_trace`, the `-Wno-c2y-extensions` option, `add_test`), and
  `src/vtcontrol/test_main.cpp` (copy its shape).

- [ ] **Step 2: Write the failing test** `core/Utf8_test.cpp`:

```cpp
#include <orchestrator/core/Utf8.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace orchestrator;
using namespace std::string_view_literals;

TEST_CASE("decodeUtf8 always advances and never trusts its input", "[orchestrator][utf8]")
{
    struct Row { std::string_view text; char32_t codepoint; std::size_t length; };
    static constexpr auto Rows = std::array {
        Row { "a"sv, U'a', 1 },
        Row { "\xC3\xBC"sv, U'\u00FC', 2 },
        Row { "\xE2\x80\xA6"sv, U'\u2026', 3 },
        Row { "\xF0\x9F\x98\x80"sv, U'\U0001F600', 4 },
        Row { "\xC3"sv, U'\uFFFD', 1 },              // truncated
        Row { "\x80"sv, U'\uFFFD', 1 },              // stray continuation
        Row { "\xC0\xAF"sv, U'\uFFFD', 1 },          // overlong
        Row { "\xED\xA0\x80"sv, U'\uFFFD', 1 },      // surrogate
        Row { "\xF4\x90\x80\x80"sv, U'\uFFFD', 1 },  // above U+10FFFF
    };
    for (auto const& row: Rows)
    {
        CAPTURE(row.text);
        auto const decoded = decodeUtf8(row.text);
        CHECK(decoded.codepoint == row.codepoint);
        CHECK(decoded.length == row.length);
    }
}
```

- [ ] **Step 3: Write `extensions/orchestrator/CMakeLists.txt`:**

```cmake
# The agent orchestrator. Its logic is an ordinary C++ library: built natively here for the tests and
# the prototype, and cross-compiled to wasm32-wasip1 as the guest (guest/, phase 1).
add_library(orchestrator_core STATIC
    core/Utf8.hpp
)
set_target_properties(orchestrator_core PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(orchestrator_core PUBLIC cxx_std_23)
# Sources include each other as <orchestrator/core/X.hpp>; vtextension contributes one header-only file.
target_include_directories(orchestrator_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."            # extensions/  → <orchestrator/...>
    "${PROJECT_SOURCE_DIR}/src")                # src/         → <vtextension/ArgumentKinds.hpp>
target_link_libraries(orchestrator_core PUBLIC glaze::glaze)

if(CONTOUR_TESTING)
    add_executable(orchestrator_test
        core/test_main.cpp
        core/Utf8_test.cpp
    )
    target_compile_definitions(orchestrator_test PRIVATE
        ORCHESTRATOR_TESTDATA="${CMAKE_CURRENT_SOURCE_DIR}/core/testdata")
    target_link_libraries(orchestrator_test PRIVATE orchestrator_core Catch2::Catch2 contour_test_trace)
    if(NOT WIN32)
        target_compile_options(orchestrator_test PRIVATE -Wno-c2y-extensions)
    endif()
    add_test(NAME orchestrator_test COMMAND $<TARGET_FILE:orchestrator_test>)
    add_test(NAME orchestrator_guest_portability
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/scripts/check-guest-portability.cmake)
    set_tests_properties(orchestrator_test PROPERTIES LABELS daemon)
    set_tests_properties(orchestrator_guest_portability PROPERTIES LABELS lint)
endif()
```

  Adjust the include root if it turns out `<orchestrator/core/X.hpp>` needs the directory to be named
  differently; adjust the Catch2/trace library names to exactly what step 1 showed. Add the directory
  from the root `CMakeLists.txt` next to where `extensions` is added, **outside** any
  `CONTOUR_EXTENSIONS_BUNDLED` condition (the native library needs no wasm toolchain):
  `add_subdirectory(extensions/orchestrator)`. If `extensions/CMakeLists.txt` is itself added
  unconditionally, add it from there instead — never both.

- [ ] **Step 4: Write `scripts/check-guest-portability.cmake`.** It globs
  `${ROOT}/core/*.hpp`, `${ROOT}/core/*.cpp`, and later `view/` and `guest/`, skips `*_test.cpp` and
  `test_main.cpp`, strips `//` comments, and fails (`message(FATAL_ERROR …)` listing file and line)
  on any of these regular expressions: `(^|[^A-Za-z_])throw[ (;]`, `(^|[^A-Za-z_])try[ \n{]`,
  `catch[ ]*\(`, `\.at\(`, `\.value\(\)`, `dynamic_cast`, `typeid`, `std::stoi|std::stol|std::stoul|std::stod`,
  `std::vformat`, `std::regex`, `std::locale`, and `#include <(iostream|fstream|filesystem|thread|mutex|future|regex|locale|sstream)>`.
  Test the script itself once by hand: put `x.at(0);` in a scratch file under `core/`, run the test,
  see it fail naming the line, remove the file.

- [ ] **Step 5: Implement `Utf8.hpp`**, build, run. Expected: the Utf8 case passes; both CTest
  entries pass. `clang-format` the new files.

- [ ] **Step 6: Commit.** `git commit -m "extensions: the orchestrator core library, its tests and a guest-portability lint"`

### Task 0.2: Sanitising untrusted text

**Files:** Create `core/Sanitize.{hpp,cpp}`, `core/Sanitize_test.cpp`; add them to both targets.

**Interfaces:**
- Consumes: `decodeUtf8`, `appendUtf8`.
- Produces:

```cpp
enum class NoteError : uint8_t { StartsWithDash, TooLong };
/// Text safe to draw: no control characters, no direction overrides, one line, at most @p maxChars.
[[nodiscard]] std::string displayText(std::string_view input, std::size_t maxChars);
[[nodiscard]] std::expected<std::string, NoteError> singleLineNote(std::string_view input);
```

  `displayText` drops every C0 and C1 control character and DEL, including ESC and CR (a TAB, LF,
  U+2028 or U+2029 becomes one space); drops U+061C, U+200E, U+200F, U+202A–U+202E and U+2066–U+2069;
  drops U+FFFD produced by invalid input; collapses runs of spaces; trims; truncates to `maxChars`
  *characters* (not display columns — the view measures width in phase 2) with a trailing `…` when it
  cut. `singleLineNote` is `displayText(input, 500)`, refused when the result starts with `-` or the
  *input* exceeded 500 characters.

- [ ] **Step 1: Write the failing table test.** Rows `(input, maxChars, expected)`:

| input (C++ literal) | max | expected |
| --- | --- | --- |
| `"plain title"` | 40 | `"plain title"` |
| `"\x1b[31mred\x1b[0m"` | 40 | `"[31mred[0m"` |
| `"title\x1b]0;pwned\x07"` | 40 | `"title]0;pwned"` |
| `"a\r\nb\tc"` | 40 | `"a b c"` |
| `"a\u2028b\u2029c"` | 40 | `"a b c"` |
| `"  spaced   out  "` | 40 | `"spaced out"` |
| `"\u009b" "31m csi"` | 40 | `"31m csi"` |
| `"abc\u202e" "def"` | 40 | `"abcdef"` |
| `"abc\u200f\u061c" "def"` | 40 | `"abcdef"` |
| `"abcdefghij"` | 5 | `"abcd…"` |
| `"ünïcödé"` | 3 | `"ün…"` |
| `"ab\xC3"` (truncated UTF-8) | 40 | `"ab"` |
| `""` | 5 | `""` |

  And for `singleLineNote`: `"keep the order"` → the same text; `"-p evil"` and `"\x1b-p"` →
  `StartsWithDash`; 501 × `x` → `TooLong`.

- [ ] **Step 2: Build and run.** Expected: FAIL to compile (`displayText` undeclared).
- [ ] **Step 3: Implement** with one pass over the input through `decodeUtf8`.
- [ ] **Step 4: Run.** Expected: PASS; the lint test passes; clang-format, clang-tidy clean.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: sanitise text that came from a forge"`

### Task 0.3: Argument kinds (shared with the host), the model, the declared tables

**Files:** Create `src/vtextension/ArgumentKinds.hpp`, `src/vtextension/ArgumentKinds_test.cpp` (add
to `vtextension_test`); `core/Model.hpp`; `core/Commands.{hpp,cpp}`, `core/Commands_test.cpp`.

**Interfaces:**
- Produces (`src/vtextension/ArgumentKinds.hpp`, namespace `vtextension`, **header-only and
  including nothing but the standard library** — the guest includes this one file without linking
  `vtextension`, so the host's rule and the guest's rule are the same code):

```cpp
enum class ArgumentKind : uint8_t { Number, Uuid, Name, Ref, Path, Text };
/// Whether @p value is acceptable for @p kind (§5.1). Path is judged syntactically here — absolute,
/// no "." or ".." component; the host additionally resolves it against its root.
[[nodiscard]] constexpr bool argumentAccepts(ArgumentKind kind, std::string_view value) noexcept;
/// The kind a manifest spells as @p name ("number", "uuid", "name", "ref", "path", "text").
[[nodiscard]] constexpr std::optional<ArgumentKind> argumentKindNamed(std::string_view name) noexcept;
[[nodiscard]] constexpr std::string_view nameOf(ArgumentKind kind) noexcept;
```

- Produces (`core/Model.hpp`):

```cpp
enum class Forge : uint8_t { GitHub, GitLab };
enum class CiState : uint8_t { None, Pending, Passing, Failing };
enum class ReviewState : uint8_t { None, Required, Approved, ChangesRequested };
enum class Readiness : uint8_t { Ready, Draft };
enum class HeadOrigin : uint8_t { SameRepository, Fork };
struct ChangeRequestRef { uint64_t number = 0; };
struct IssueRef { uint64_t number = 0; };
struct JiraRef { std::string key; };
using ItemRef = std::variant<ChangeRequestRef, IssueRef, JiraRef>;
struct ChangeRequest
{
    uint64_t number = 0; std::string title; std::string url;
    std::string head;                    // display text only; never reaches a command
    std::string base;                    // a Ref
    Readiness readiness = Readiness::Ready; HeadOrigin origin = HeadOrigin::SameRepository;
    CiState ci = CiState::None; std::vector<std::string> failingChecks;
    ReviewState review = ReviewState::None; std::vector<uint64_t> closesIssues;
};
struct Issue { ItemRef item; std::string title; std::string url; };
```

  `title`, `head` and `failingChecks` hold text already passed through `displayText(_, 200)`.

- Produces (`core/Commands.hpp`):

```cpp
using vtextension::ArgumentKind;
using vtextension::argumentAccepts;
enum class Timeout : uint8_t { Short, Long };          // 30 s, 10 min
enum class Cwd : uint8_t { Repo, Worktree };           // manifest roots `repos` and `worktrees`
struct EnvironmentVariable { std::string_view name; std::string_view value; };
struct ArgumentDecl { std::string_view name; ArgumentKind kind; };
struct CommandDecl { std::string_view name; Timeout timeout; Cwd cwd; std::span<std::string_view const> argv; };
struct PaneDecl { std::string_view name; Cwd cwd; std::span<std::string_view const> argv;
                  std::span<EnvironmentVariable const> environment; };
struct Argument { std::string_view name; std::string value; };   // a placeholder and what fills it

[[nodiscard]] std::span<ArgumentDecl const> argumentDecls() noexcept;
[[nodiscard]] std::span<EnvironmentVariable const> commandEnvironment() noexcept;
[[nodiscard]] std::span<CommandDecl const> commands() noexcept;
[[nodiscard]] std::span<PaneDecl const> paneTemplates() noexcept;
[[nodiscard]] CommandDecl const* findCommand(std::string_view name) noexcept;     // nullptr: not declared
[[nodiscard]] PaneDecl const* findPaneTemplate(std::string_view name) noexcept;
[[nodiscard]] std::optional<ArgumentKind> kindOfArgument(std::string_view name) noexcept;
/// Every `arg`, `command-env`, `command`, `pane` and `pane-env` line of §5.1, in table order.
[[nodiscard]] std::vector<std::string> manifestLines();
```

  Argument kinds (§5.1): `number`, `page` → Number; `session` → Uuid; `remote`, `model` → Name;
  `branch`, `base` → Ref; `path` → Path (root `worktrees`); `query`, `prompt` → Text.

  `commandEnvironment()` is exactly the `command-env` lines of §5.1 (`GIT_TERMINAL_PROMPT=0`,
  `GIT_ALLOW_PROTOCOL=https:ssh:git:file`, and the `GIT_CONFIG_*` pairs forcing `core.fsmonitor=false`
  and `core.hooksPath=/dev/null`). `paneTemplates()` has one row: `agent`, `Cwd::Worktree`,
  `claude --model {model} --session-id {session} {prompt}`, with the `pane-env` lines of §5.1
  (`push.default=upstream`, `push.autoSetupRemote=true`).

  Command rows for phase 0 (GitHub and git only). **No row contains `-C`.**

| name | timeout | cwd | argv |
| --- | --- | --- | --- |
| `gh-auth` | Short | Repo | `gh auth status` |
| `agent-hooks` | Short | Repo | `contour agent setup --dry-run --json` |
| `gh-default-branch` | Short | Repo | `gh repo view --json defaultBranchRef` |
| `gh-prs` | Long | Repo | `gh pr list --author @me --state open --limit 100 --json number,title,url,headRefName,baseRefName,isDraft,isCrossRepository,reviewDecision,statusCheckRollup,closingIssuesReferences` |
| `gh-issues` | Long | Repo | `gh issue list --state open --limit 100 --search {query} --json number,title,url` |
| `gh-checkout` | Long | Worktree | `gh pr checkout {number} --branch {branch}` |
| `git-fetch` | Long | Repo | `git fetch --quiet {remote}` |
| `git-worktree-add` | Long | Repo | `git worktree add --detach {path} {base}` |
| `git-worktree-add-branch` | Long | Repo | `git worktree add --no-track -b {branch} {path} {base}` |
| `git-worktree-list` | Short | Repo | `git worktree list --porcelain` |
| `git-status` | Short | Worktree | `git status --porcelain` |
| `git-clean-status-board` | Short | Worktree | `git clean -f -- STATUS.md` |
| `git-worktree-remove` | Long | Repo | `git worktree remove {path}` |

- [ ] **Step 1: Write the failing tests.**

  `ArgumentKinds_test.cpp` (in `vtextension_test`) — rows `(kind, value, accepted)`:

| kind | accepted | refused |
| --- | --- | --- |
| Number | `2334` | empty, `-1`, `12345678901` (11 digits), `12a` |
| Name | `origin`, `claude-opus-5-5` | `ext::sh`, `-x`, `.hidden`, `a/b`, `https://evil`, empty, 65 characters |
| Ref | `origin/master`, `agent/pr-1-20261113-141207`, `user@branch`, `fix#12`, `c++`, `dependabot/npm_and_yarn/x-1.2.3` | `a..b`, `a//b`, `-a`, `/a`, `a b`, `a:b`, `a~1`, `a@{1}`, empty, 201 characters |
| Path | `/home/u/worktrees/contour/pr-1` | `relative/x`, `/a/../b`, `/a/./b`, empty, a path containing NUL |
| Uuid | `550e8400-e29b-41d4-a716-446655440000` | `550e8400`, the same UUID in upper case, one with a `g` |
| Text | `assignee:@me`, `"/x:fix-ci https://h/p\nNote: n"` | `-label:x`, text containing NUL, 8193 characters |

  Also `static_assert`s that the function is usable in a constant expression, and that
  `argumentKindNamed(nameOf(k)) == k` for all six kinds.

  `Commands_test.cpp`:

```cpp
TEST_CASE("every declared command and pane obeys the manifest grammar", "[orchestrator][commands]")
{
    auto const check = [](std::string_view owner, std::span<std::string_view const> argv) {
        CAPTURE(owner);
        REQUIRE_FALSE(argv.empty());
        CHECK_FALSE(argv.front().starts_with('{'));
        for (auto const word: argv)
        {
            CAPTURE(word);
            CHECK(word.find(' ') == std::string_view::npos);
            auto const hasBrace = word.find_first_of("{}") != std::string_view::npos;
            auto const isPlaceholder = word.size() > 2 && word.front() == '{' && word.back() == '}'
                && std::ranges::all_of(word.substr(1, word.size() - 2), [](char c) {
                       return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                              || c == '_' || c == '-';
                   });
            CHECK((!hasBrace || isPlaceholder));
            if (isPlaceholder)
                CHECK(kindOfArgument(word.substr(1, word.size() - 2)).has_value());
        }
    };
    for (auto const& decl: commands())
        check(decl.name, decl.argv);
    for (auto const& decl: paneTemplates())
        check(decl.name, decl.argv);
}

TEST_CASE("no command names a directory or forces anything", "[orchestrator][commands]")
{
    static constexpr auto Forbidden = std::array<std::string_view, 8> {
        "-C", "--git-dir", "--work-tree", "-R", "--force", "-f", "--force-with-lease", "--hard",
    };
    for (auto const& decl: commands())
    {
        CAPTURE(decl.name);
        if (decl.name == "git-clean-status-board")
        {
            // The one exception: git refuses `clean` without -f. Pinned to one fixed path.
            static constexpr auto Expected = std::array<std::string_view, 5> { "git", "clean", "-f", "--", "STATUS.md" };
            CHECK(std::ranges::equal(decl.argv, Expected));
            continue;
        }
        for (auto const word: decl.argv)
            CHECK(std::ranges::find(Forbidden, word) == Forbidden.end());
    }
}

TEST_CASE("an issue branch never tracks the default branch", "[orchestrator][commands]")
{
    // Without --no-track, `git push` under push.default=upstream goes to the default branch (measured).
    auto const* add = findCommand("git-worktree-add-branch");
    REQUIRE(add != nullptr);
    CHECK(std::ranges::find(add->argv, std::string_view { "--no-track" }) != add->argv.end());
    auto const* agent = findPaneTemplate("agent");
    REQUIRE(agent != nullptr);
    auto const has = [&](std::string_view name, std::string_view value) {
        return std::ranges::any_of(agent->environment,
                                   [&](auto const& v) { return v.name == name && v.value == value; });
    };
    CHECK(has("GIT_CONFIG_VALUE_0", "upstream"));
    CHECK(has("GIT_CONFIG_KEY_1", "push.autoSetupRemote"));
    CHECK(std::ranges::none_of(agent->argv, [](std::string_view w) {
        return w.starts_with("--settings") || w.find("permission") != std::string_view::npos;
    }));
}
```

  Plus: command names are unique; pane names are unique; `manifestLines()` starts with the `arg`
  lines in table order and contains `command git-status short @worktrees = git status --porcelain`
  and `pane agent @worktrees = claude --model {model} --session-id {session} {prompt}` verbatim.

- [ ] **Step 2: Build and run** both test binaries. Expected: FAIL to compile.
- [ ] **Step 3: Implement.** `ArgumentKinds.hpp` as one `constexpr` function per kind plus a
  dispatching `switch` (no table of function pointers needed; the kinds are the table). `Commands.cpp`
  holds the rows as `static constexpr std::array`s. `vtextension`'s public-headers test must still
  pass — the new header includes only `<cstdint>`, `<optional>`, `<string_view>`.
- [ ] **Step 4: Run** `vtextension_test "[argument-kinds]"` and `orchestrator_test`. Expected: PASS;
  lint, clang-format, clang-tidy clean.
- [ ] **Step 5: Commit.** `git commit -m "vtextension, orchestrator: typed arguments shared by host and guest, and the declared tables"`

### Task 0.4: Record GitHub fixtures and map them

**Files:** Create `core/GitHub.{hpp,cpp}`, `core/GitHub_test.cpp`,
`core/testdata/github/{prs-open.json,prs-empty.json,prs-mixed-checks.json,issues-open.json,default-branch.json,auth-failed.stderr,VERSION}`.

**Interfaces:**
- Consumes: the model, `displayText`, `argumentAccepts`.
- Produces:

```cpp
inline constexpr std::size_t ListLimit = 100;            // matches `--limit 100` in the command table
enum class ParseError : uint8_t { NotJson, WrongShape };
enum class ListCompleteness : uint8_t { Complete, AtLimit };
template <typename T>
struct Listed { std::vector<T> items; std::size_t skipped = 0; ListCompleteness completeness = ListCompleteness::Complete; };
namespace github
{
    struct Check { std::string_view typeName; std::string_view status; std::string_view conclusion; std::string_view state; };
    [[nodiscard]] CiState ciStateOf(std::span<Check const> checks) noexcept;
    [[nodiscard]] ReviewState reviewStateOf(std::string_view reviewDecision) noexcept;
    [[nodiscard]] std::expected<Listed<ChangeRequest>, ParseError> changeRequests(std::string_view stdoutText);
    [[nodiscard]] std::expected<Listed<Issue>, ParseError> issues(std::string_view stdoutText);
    [[nodiscard]] std::expected<std::string, ParseError> defaultBranch(std::string_view stdoutText);   // a Ref
}
[[nodiscard]] std::vector<Issue> unclaimed(std::vector<Issue> issues, std::span<ChangeRequest const> mine);
```

- [ ] **Step 1: Record fixtures from the real CLI,** from the repository root:

```bash
F=extensions/orchestrator/core/testdata/github
mkdir -p $F
gh pr list --state open --limit 5 --json number,title,url,headRefName,baseRefName,isDraft,isCrossRepository,reviewDecision,statusCheckRollup,closingIssuesReferences > $F/prs-open.json
gh issue list --state open --limit 5 --json number,title,url > $F/issues-open.json
gh repo view --json defaultBranchRef > $F/default-branch.json
echo '[]' > $F/prs-empty.json
gh --version | head -1 > $F/VERSION
```

  (`--author @me` is dropped here only so the fixture is non-empty for any contributor.) Read each file
  before committing: it is public repository data, but confirm it holds no token or private URL.
  Hand-write `prs-mixed-checks.json` from one recorded PR so that its entries cover: a `CheckRun` with
  `conclusion: "FAILURE"`; a `StatusContext` entry (`"__typename":"StatusContext",
  "context":"ci/legacy","state":"PENDING"`); `reviewDecision: "CHANGES_REQUESTED"`;
  `isCrossRepository: true`; `closingIssuesReferences: [{"number": 2301}]`; a title containing
  `\u001b[31m`; and one PR whose `baseRefName` is `"--upload-pack=x"`. Hand-write `auth-failed.stderr`
  with gh's "not logged in" text. The tests read fixtures from `ORCHESTRATOR_TESTDATA` with a small
  `readFixture(name)` helper in the test file (tests may use `<fstream>`; the library may not).

- [ ] **Step 2: Write the failing tests.** `ciStateOf` rows — each check is
  `(typeName, status, conclusion, state)`:

| checks | expected |
| --- | --- |
| none | `None` |
| `(CheckRun, COMPLETED, SUCCESS, "")` | `Passing` |
| `(CheckRun, COMPLETED, SKIPPED, "")` | `Passing` |
| `(CheckRun, COMPLETED, NEUTRAL, "")` | `Passing` |
| `(CheckRun, IN_PROGRESS, "", "")` | `Pending` |
| `(CheckRun, QUEUED, "", "")` | `Pending` |
| `(StatusContext, "", "", PENDING)` | `Pending` |
| `(StatusContext, "", "", SUCCESS)` | `Passing` |
| `(CheckRun, COMPLETED, FAILURE, "")` | `Failing` |
| `(CheckRun, COMPLETED, TIMED_OUT, "")` | `Failing` |
| `(CheckRun, COMPLETED, CANCELLED, "")` | `Failing` |
| `(CheckRun, COMPLETED, ACTION_REQUIRED, "")` | `Failing` |
| `(StatusContext, "", "", ERROR)` | `Failing` |
| a `SUCCESS` and an `IN_PROGRESS` | `Pending` |
| an `IN_PROGRESS` and a `FAILURE` | `Failing` |
| `(CheckRun, COMPLETED, SOMETHING_NEW, "")` | `Pending` |

  An unknown conclusion maps to `Pending`, never `Passing`: an unknown value must not look green.
  Further tests: `changeRequests(prs-open.json)` returns one item per element with the fixture's
  numbers and `skipped == 0`; `prs-empty.json` → no items; `prs-mixed-checks.json` → the failing
  check's name is in `failingChecks`, `review == ChangesRequested`, `origin == Fork`,
  `closesIssues == {2301}`, the title contains no `\x1b`, and the PR with the hostile base is not in
  `items` while `skipped == 1`; a list of exactly `ListLimit` generated entries → `AtLimit`;
  `"not json"` → `NotJson`; `{"a":1}` → `WrongShape`; the first half of `prs-open.json` → `NotJson` (a
  cut-off reply must be an error, not a short list); `reviewStateOf` rows `"" → None`,
  `REVIEW_REQUIRED → Required`, `APPROVED → Approved`, `CHANGES_REQUESTED → ChangesRequested`,
  anything else → `Required`; `defaultBranch` of the fixture is `"master"`, and of
  `{"defaultBranchRef":{"name":"-x"}}` is `WrongShape`. `unclaimed` drops exactly the issues whose
  number is in some PR's `closesIssues`.

- [ ] **Step 3: Run.** Expected: FAIL. **Step 4: Implement** with glaze: private structs mirroring
  the JSON (every field defaulted), read with
  `glz::read<glz::opts { .error_on_unknown_keys = false }>(value, text)` so a newer `gh` adding fields
  does not break parsing; glaze returns an error code, never throws. Keep glaze out of `GitHub.hpp`
  (include it in the `.cpp` only). **Step 5: Run.** Expected: PASS; lint, format, tidy clean.
- [ ] **Step 6: Commit.** `git commit -m "orchestrator: map gh's JSON to the shared model"`

### Task 0.5: Configuration

**Files:** Create `core/Config.{hpp,cpp}`, `core/Config_test.cpp`.

**Interfaces:**
- Produces:

```cpp
enum class Task : uint8_t { FixCi, AddressReview, Issue, Custom };
struct ForgeIssueSource { std::string query; };
using IssueSource = std::variant<ForgeIssueSource>;             // Jira arrives in phase 4
struct Repo { std::string name; std::string path; Forge forge = Forge::GitHub; std::string remote;
              std::optional<std::string> base; std::vector<IssueSource> issues; };
struct ConfigError { std::string key; std::string why; };        // key empty: the document is not JSON
class Config
{
  public:
    [[nodiscard]] static std::expected<Config, ConfigError> parse(std::string_view json, std::string_view home);
    [[nodiscard]] std::string const& worktreeRoot() const noexcept;
    [[nodiscard]] std::span<std::string const> models() const noexcept;
    [[nodiscard]] uint32_t maxAgents() const noexcept;
    [[nodiscard]] uint32_t maxAgentsPerRepo() const noexcept;
    [[nodiscard]] std::chrono::milliseconds pollEvery() const noexcept;
    [[nodiscard]] std::chrono::milliseconds statusBoardAfter() const noexcept;   // zero: never
    [[nodiscard]] std::span<Repo const> repos() const noexcept;
    [[nodiscard]] Repo const* findRepo(std::string_view name) const noexcept;
    [[nodiscard]] std::string_view defaultModel(Task task) const noexcept;        // Custom → models()[0]
    [[nodiscard]] std::optional<std::string_view> promptTemplate(Task task) const noexcept;   // Custom → nullopt
  private: /* … */
};
[[nodiscard]] std::optional<std::chrono::milliseconds> parseDuration(std::string_view text) noexcept;   // "90s", "2m", "1h", "0"
```

  `Config` has no public constructor and no setters: `parse` is the factory, and a `Config` that exists
  is valid.

- [ ] **Step 1: Write failing tests** covering: the §6.5 example (as JSON) parses, `~/worktrees` and
  `~/repo/contour` are expanded against `home = "/home/u"`, the repo's `name == "contour"` (last path
  component), `remote` defaults to `"origin"`, `base` is empty when omitted; defaults when omitted
  (`max_agents 4`, `max_agents_per_repo 2`, `poll_every 2m`, `status_board_after 15m`, models
  `{opus, sonnet, haiku}`, the three prompt templates of §6.5); and one rejection row each, checking
  the `key` reported: a relative `worktree_root`; a `worktree_root` or repo `path` that fails
  `argumentAccepts(Path, _)` after expansion; an empty `repos`; two repos with the same `name`; a repo
  `name` that fails `Name`; `max_agents == 0`; `poll_every` below `30s`; a model, `remote` or `base`
  that fails its kind; an issue `query` that fails `Text` (so `-label:x …` is refused with `why`
  telling the user to start with another term); a prompt template without `{url}`; a prompt template
  starting with `-`; `forge: bitbucket`; `"2 minutes"` as a duration; a document that is not JSON
  (`key` empty).
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement** — glaze into private mirror structs, then
  validate field by field. **Step 4: Run** (PASS); lint, format, tidy.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: parse and validate the configuration"`

### Task 0.6: Names and prompts

**Files:** Create `core/Naming.{hpp,cpp}`, `core/Prompt.{hpp,cpp}`, and their `_test.cpp`.

**Interfaces:**
- Produces:

```cpp
struct Stamp { uint16_t year; uint8_t month; uint8_t day; uint8_t hour; uint8_t minute; uint8_t second; };
[[nodiscard]] constexpr Stamp stampFromUnixMs(uint64_t ms, int32_t utcOffsetMinutes) noexcept;
[[nodiscard]] constexpr bool isJiraKey(std::string_view text) noexcept;     // ^[A-Z][A-Z0-9]+-[0-9]+$
[[nodiscard]] std::string itemSlug(ItemRef const& item);                    // "pr-2334" | "issue-2301" | "ct-418"
[[nodiscard]] std::string runId(ItemRef const& item, Stamp const& stamp);   // "pr-2334-20261113-150847"
[[nodiscard]] std::string branchName(std::string_view runId);               // "agent/pr-2334-20261113-150847"
[[nodiscard]] std::string worktreePath(std::string_view root, std::string_view repo, std::string_view runId);

enum class PromptError : uint8_t { StartsWithDash, UrlNotHttps, TooLong, NoteStartsWithDash, NoteTooLong };
[[nodiscard]] std::expected<std::string, PromptError> buildPrompt(std::string_view promptTemplate, std::string_view url, std::optional<std::string_view> note);
[[nodiscard]] std::expected<std::string, PromptError> buildCustomPrompt(std::string_view text, std::string_view url);   // "<text>\n<url>"
```

- [ ] **Step 1: Write failing tests:** `stampFromUnixMs(1794578927000, 60)` is 2026-11-13 15:08:47
  (`date -u -d @1794578927` prints `2026-11-13 14:08:47`; plus one hour — keep that command in a
  comment), and with offset `-480` it is 2026-11-13 06:08:47; a leap day
  (`date -u -d '2028-02-29 12:00:00' +%s` → use the printed value); both also as `static_assert`s;
  `isJiraKey` accepts `CT-418`, `AB2-1`, rejects `ct-418`, `C-1`, `CT-`, `CT-1a`, `CT-1 `, `-CT-1`; the
  four name functions give exactly the strings in the comments above for that stamp; for a table of
  item refs, `branchName` passes `Ref`, `runId` passes `Name` and `worktreePath` passes `Path`;
  `buildPrompt("/x:fix-ci {url}", "https://github.com/o/r/pull/1", nullopt) ==
  "/x:fix-ci https://github.com/o/r/pull/1"`; with a note the result is `"<prompt>\nNote: <note>"`; a
  non-`https://` URL is refused; a URL containing a space, a control character or a quote is refused; a
  template or custom text that yields something starting with `-` is refused; every accepted prompt
  passes `Text`; custom text is sanitised to one line and capped at 2000 characters.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement** — civil-from-days arithmetic by hand
  (`<chrono>` calendar types are fine too, they need no clock); numbers formatted with
  `std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", …)` — a compile-time format string is allowed.
  **Step 4: Run** (PASS); lint, format, tidy.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: names and prompts, built from identifiers only"`

### Task 0.7: The run state machine

**Files:** Create `core/Run.{hpp,cpp}`, `core/Run_test.cpp`.

**Interfaces:**
- Consumes: the model, naming, `Task`, `displayText`, `Argument`.
- Produces:

```cpp
enum class PrepareStep : uint8_t { Fetch, WorktreeAdd, Checkout, OpenPane };
enum class EndStep : uint8_t { Verify, Status, CleanStatusBoard, Remove };
enum class AgentStatus : uint8_t { Idle, Working, Done, Blocked, Error };    // contour's ProgramState
enum class Reported : uint8_t { Never, Yes };
enum class BoardSeen : uint8_t { No, Yes };
struct RunFailure { PrepareStep step = PrepareStep::Fetch; std::string why; };

struct Preparing { PrepareStep step = PrepareStep::Fetch; };
struct Running {};
struct Blocked {};
struct Done {};
struct Ending { EndStep step = EndStep::Verify; };
struct Kept { std::vector<std::string> entries; };
struct Failed { RunFailure failure; };
using RunState = std::variant<Preparing, Running, Blocked, Done, Ending, Kept, Failed>;

struct Run
{
    std::string id;                      // also the pane tag and the prefix of every command tag
    std::string repo; std::string repoPath; std::string remote;
    std::string base;                    // "<remote>/<branch>", a Ref
    ItemRef item; std::string url; Task task = Task::FixCi; std::string model; std::string prompt;
    std::string session; std::string branch; std::string worktree;
    std::optional<std::string> pane; RunState state; AgentStatus lastStatus = AgentStatus::Idle;
    Reported reported = Reported::Never;
    std::optional<RunFailure> failure;   // why the run failed; carried through Ending
    uint8_t boardPosts = 0; BoardSeen boardSeen = BoardSeen::No;
    uint64_t runningSinceMs = 0; uint64_t boardPostedMs = 0;   // monotonic; NOT persisted (see below)
};

// Inputs
struct CommandOk { std::string_view stdoutText; };
struct CommandFailed { std::string_view why; };   // non-zero exit, timed out, truncated output, refused start
struct CwdMissing {};                             // the command could not start: its directory is gone
struct Lost {};                                   // the guest restarted and this step's command or pane is unknown
struct GitFileRead { std::optional<std::string_view> content; };   // <worktree>/.git, or nullopt if unreadable
struct PaneOpened { std::string_view pane; uint64_t monoMs = 0; };
struct PaneOpenFailed { std::string_view why; };
struct StatusReported { AgentStatus status = AgentStatus::Idle; };
struct PaneGone {};
struct RetryRemoval {};
struct Dismiss {};
using RunInput = std::variant<CommandOk, CommandFailed, CwdMissing, Lost, GitFileRead, PaneOpened,
                              PaneOpenFailed, StatusReported, PaneGone, RetryRemoval, Dismiss>;

// Effects
struct RunDeclared { std::string_view name; std::vector<Argument> arguments; };   // cwd comes from the table
struct ReadGitFile {};
struct OpenAgentPane {};
struct CloseAgentPane {};
struct Persist {};
struct Forget {};
using RunEffect = std::variant<RunDeclared, ReadGitFile, OpenAgentPane, CloseAgentPane, Persist, Forget>;

inline constexpr uint32_t RunSchema = 1;
[[nodiscard]] std::vector<RunEffect> advance(Run& run, RunInput const& input, std::optional<std::string_view> mainCheckout);
[[nodiscard]] std::optional<std::string> stepTag(Run const& run);   // "<id>/fetch", "<id>/verify", …; nullopt: nothing awaited
[[nodiscard]] bool occupiesASlot(Run const& run) noexcept;          // Preparing, Running, Blocked, Done, Ending
[[nodiscard]] RunState stateFor(AgentStatus status) noexcept;       // Idle|Working → Running, Blocked|Error → Blocked, Done → Done
[[nodiscard]] bool gitFileIsIntact(std::string_view content, std::string_view mainCheckout, std::string_view worktree) noexcept;
[[nodiscard]] std::string toJson(Run const& run);
[[nodiscard]] std::optional<Run> runFromJson(std::string_view text);
```

  - There is no `Removed` state: a run whose worktree was removed cleanly is forgotten (`Forget`).
  - **Once a worktree may exist, a failure always passes through `Ending`**, so nothing is left behind
    unnoticed; it finishes as `Failed` (worktree gone) or `Kept` (still there, `failure` shown beside
    its entries). A failure *before* any worktree can exist (`Fetch`, or `WorktreeAdd` that could not
    start) goes straight to `Failed`.
  - `mainCheckout` is the repository's resolved main-worktree path, learned by the engine from the
    first entry of `git worktree list`. `gitFileIsIntact` is true only when the content, trimmed, is
    exactly `gitdir: <mainCheckout>/.git/worktrees/<last component of worktree>`. With no
    `mainCheckout` the file cannot be judged and counts as altered.
  - Persistence goes through a private flat struct (`StoredRun`: every field a string, number or enum
    name, plus `"v": RunSchema`) that glaze reads and writes — do not serialise the variants directly.
    `toJson` omits the two monotonic fields: a monotonic clock restarts with the machine, so a
    persisted value would be meaningless. `runFromJson` rejects any other `v`, any unknown enum name
    and any field failing its argument kind, and sets both monotonic fields to 0; the engine re-bases
    them at start.
  - `advance` is one `std::visit` over `(state, input)` with `core::Overloaded`-style lambdas written
    locally (do not pull `core-cpp` into the guest for this; five lines).

  **Notation in the table below:** `Preparing(Fetch)` means `Preparing{PrepareStep::Fetch}`;
  `Kept{[…]}` lists `Kept::entries`; `GitFile(Some(intact))` means `GitFileRead` with content for
  which `gitFileIsIntact` holds, `GitFile(None)` means `GitFileRead{std::nullopt}`; `Status(s)` is
  `StatusReported{s}`; `CommandOk{"…"}` gives `stdoutText`; "`reported`" means `Reported::Yes`;
  effect names `ReadGitFile`, `OpenPane` (= `OpenAgentPane`), `Persist`, `Forget`, and a command
  name with its placeholders (= `RunDeclared`).

- [ ] **Step 1: Write the failing transition table.** One test drives rows of
  `(start state, failure before, input, expected state, expected effects)`. "fail(X)" means
  `failure = RunFailure{X, why}`. "→ Verify" abbreviates: state `Ending(Verify)`, effects `ReadGitFile`,
  `Persist`.

| From | Input | To | Effects |
| --- | --- | --- | --- |
| `Preparing(Fetch)` | `CommandOk` | `Preparing(WorktreeAdd)` | PR: `git-worktree-add {path, base}`; issue or Jira: `git-worktree-add-branch {branch, path, base}`; Persist |
| `Preparing(Fetch)` | `CommandFailed` / `Lost` / `CwdMissing` | `Failed{Fetch}` | Persist (no worktree exists yet) |
| `Preparing(WorktreeAdd)` | `CommandOk`, PR | `Preparing(Checkout)` | `gh-checkout {number, branch}`, Persist |
| `Preparing(WorktreeAdd)` | `CommandOk`, issue | `Preparing(OpenPane)` | OpenPane, Persist |
| `Preparing(WorktreeAdd)` | `CwdMissing` | `Failed{WorktreeAdd}` | Persist (the repository is gone; nothing was created) |
| `Preparing(WorktreeAdd)` | `CommandFailed` / `Lost` | → Verify, fail(WorktreeAdd) | |
| `Preparing(Checkout)` | `CommandOk` | `Preparing(OpenPane)` | OpenPane, Persist |
| `Preparing(Checkout)` | `CommandFailed` / `Lost` / `CwdMissing` | → Verify, fail(Checkout) | |
| `Preparing(OpenPane)` | `PaneOpened` | `Running`, `pane` set, `runningSinceMs` set | Persist |
| `Preparing(OpenPane)` | `PaneOpenFailed` / `Lost` | → Verify, fail(OpenPane) | |
| `Running`/`Blocked`/`Done` | `Status(s)`, `s != lastStatus` | `stateFor(s)`, `lastStatus = s`, `reported = true` | Persist |
| `Running`/`Blocked`/`Done` | `Status(s)`, `s == lastStatus` | unchanged (`reported = true`) | none, or Persist if `reported` changed |
| `Running`/`Blocked`/`Done` | `PaneGone`, `reported` | → Verify, `pane` cleared | |
| `Running`/`Blocked`/`Done` | `PaneGone`, not `reported` | → Verify, `pane` cleared, fail(OpenPane, "the agent exited before reporting") | |
| `Ending(Verify)` | `GitFile(Some(intact))` | `Ending(Status)` | `git-status`, Persist |
| `Ending(Verify)` | `GitFile(Some(altered))` | `Kept{["the worktree's .git file was changed; nothing was run there"]}` | Persist |
| `Ending(Verify)` | `GitFile(None)`, no failure | — | Forget (the user removed the worktree by hand) |
| `Ending(Verify)` | `GitFile(None)`, fail(X) | `Failed{X}` | Persist |
| `Ending(Status)` | `CommandOk{""}` | `Ending(Remove)` | `git-worktree-remove {path}`, Persist |
| `Ending(Status)` | `CommandOk{"?? STATUS.md\n"}` | `Ending(CleanStatusBoard)` | `git-clean-status-board`, Persist |
| `Ending(Status)` | `CommandOk{" M src/a.cpp\n?? STATUS.md\n"}` | `Kept{[" M src/a.cpp","?? STATUS.md"]}` | Persist |
| `Ending(Status)` | `CwdMissing`, no failure | — | Forget |
| `Ending(Status)` | `CwdMissing`, fail(X) | `Failed{X}` | Persist |
| `Ending(Status)` | `CommandFailed` | `Kept{["status failed: …"]}` | Persist |
| `Ending(CleanStatusBoard)` | `CommandOk` | `Ending(Remove)` | `git-worktree-remove {path}`, Persist |
| `Ending(CleanStatusBoard)` | `CommandFailed` / `CwdMissing` | `Kept{["clean failed: …"]}` | Persist |
| `Ending(Remove)` | `CommandOk`, no failure | — | Forget |
| `Ending(Remove)` | `CommandOk`, fail(X) | `Failed{X}` | Persist |
| `Ending(Remove)` | `CommandFailed` / `CwdMissing` | `Kept{["remove failed: …"]}` | Persist |
| `Ending(any)` | `Lost` | → Verify | |
| `Kept` | `RetryRemoval` | → Verify | |
| `Failed` | `Dismiss` | — | Forget |
| any state | any input not listed for it | unchanged | none |

  The last row is tested explicitly for: `Status` and `PaneGone` while `Preparing` or `Ending`;
  `Dismiss` on `Kept` (a kept worktree can only be removed, never dismissed); `RetryRemoval` on
  `Running`. Entries in `Kept`, and every `why`, pass through `displayText(_, 200)` (file
  names and error text are not ours).

  **A property test guards against a run that holds a slot forever:** for every state in which
  `stepTag(run)` is `Some`, each of `CommandOk{""}`, `CommandFailed`, `CwdMissing` and `Lost` (and for
  `Ending(Verify)`, each `GitFile` case) either changes the state or emits an effect. No awaited step
  may swallow its own result.

  Also test: `gitFileIsIntact` — exact match true; trailing newline true; a different prefix
  (`gitdir: /tmp/evil/.git/worktrees/<name>`) false; a different name false; extra lines false;
  `runFromJson(toJson(run))` round-trips every state with and without `failure`, with the two
  monotonic fields zeroed; `runFromJson` of garbage, and of a document with `"v": 2`, is `None`;
  `stepTag` per state; `occupiesASlot` per state; `stateFor` for all five statuses; every `Command`
  effect's argument names equal the placeholders of that command's argv, and every value passes its
  `argumentAccepts`.

- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); lint, format, tidy.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: the run lifecycle as a state machine"`

### Task 0.8: Reconciliation and orphans

**Files:** Create `core/Reconcile.{hpp,cpp}`, `core/Reconcile_test.cpp`.

**Interfaces:**
- Produces:

```cpp
enum class ProgramLiveness : uint8_t { Gone, Alive };
struct PaneFact { std::string pane; std::string tag; AgentStatus status = AgentStatus::Idle;
                  ProgramLiveness program = ProgramLiveness::Gone; };
struct Adopt { std::string run; std::string pane; };
struct StatusChanged { std::string run; AgentStatus status = AgentStatus::Idle; };
struct ClosePane { std::string pane; };
struct PaneWentAway { std::string run; };
using Decision = std::variant<Adopt, StatusChanged, ClosePane, PaneWentAway>;
[[nodiscard]] std::vector<Decision> reconcile(std::span<Run const> runs, std::span<PaneFact const> panes);

struct WorktreeEntry { std::string path; std::optional<std::string> branch; };   // branch without "refs/heads/"
[[nodiscard]] std::vector<WorktreeEntry> worktreeEntries(std::string_view porcelain);
[[nodiscard]] std::vector<WorktreeEntry> orphans(std::span<Run const> runs, std::string_view repo,
                                                 std::string_view worktreeRoot, std::span<WorktreeEntry const> entries);
/// The first entry: git lists the main worktree first.
[[nodiscard]] std::optional<std::string_view> mainCheckout(std::span<WorktreeEntry const> entries) noexcept;
```

  `reconcile` never compares paths and never forgets a run (Review Focus 3). In the rules below,
  "`Status`" is `StatusChanged`, "`PaneGone`" is `PaneWentAway`.

- [ ] **Step 1: Write failing tests,** one per rule:
  1. A `Running`/`Blocked`/`Done` run whose pane is listed, `Alive`, with a status different from
     `lastStatus` → `Status`. Same status → nothing.
  2. Such a run whose pane is absent → `PaneGone`.
  3. Such a run whose pane is listed with `program == ProgramLiveness::Gone` → `ClosePane` then `PaneGone` (a cold
     restart left a shell behind; close it before the worktree is touched).
  4. A `Preparing(OpenPane)` run and a listed pane whose `tag == run.id` → `Adopt` (the guest restarted
     between `pane-open` and storing the pane).
  5. A pane whose tag names no run → no decision (it stays a visible tab; nothing is closed blindly).
  6. A run in `Preparing(Fetch|WorktreeAdd|Checkout)`, `Ending`, `Kept` or `Failed` → no decision.
  7. **Idempotence:** apply the decisions (`Status` → `advance(…, StatusReported)`, `PaneGone` →
     `advance(…, PaneGone)`, `Adopt` → `advance(…, PaneOpened)`), remove closed panes, reconcile again → empty.
     Include a pane reporting `Idle` for a run that never reported: one `Status`, then nothing.
  8. `worktreeEntries` on the real shape (`worktree <path>\nHEAD <sha>\nbranch refs/heads/<name>\n\n`,
     a `detached` entry, a path containing a space) returns paths and branches in order.
  9. `orphans` returns the entries that no run of that repository accounts for and that are either
     on an `agent/…` branch or located under `worktreeRoot` (which catches a detached worktree whose
     run was lost before its branch existed). An entry is accounted for when a run has its branch, or
     its path. The first entry (the main checkout) and the user's own worktrees elsewhere are never
     orphans; a run whose stored `worktree` differs textually from the listed path (symlinked root) is
     **not** reported when its branch matches.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); lint, format, tidy.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: reconcile runs with panes, and find orphan worktrees"`

### Task 0.9: The engine

**Files:** Create `core/Engine.{hpp,cpp}`, `core/Engine_test.cpp`.

**Interfaces:**
- Consumes: everything above.
- Produces:

```cpp
inline constexpr std::size_t MaxInFlight = 4;                 // commands running at once; the rest queue
inline constexpr auto BoardRepostEvery = std::chrono::minutes { 5 };
inline constexpr uint8_t BoardMaxPosts = 3;                    // three posts in total
inline constexpr auto ReconcileEvery = std::chrono::seconds { 30 };
inline constexpr auto MaxBackoff = std::chrono::minutes { 30 };

enum class StartResult : uint8_t { Started, CwdMissing, Refused };
enum class Completion : uint8_t { Exited, TimedOut };
enum class OutputExtent : uint8_t { Whole, Truncated };
struct ProcessOutcome { StartResult start = StartResult::Started; Completion completion = Completion::Exited;
                        std::optional<int32_t> exitCode; OutputExtent extent = OutputExtent::Whole;
                        std::string stdoutText; std::string stderrText; };
struct StoredEntry { std::string key; std::string value; };

/// Everything the engine needs to exist. A constructed Engine is a usable Engine.
struct StartContext
{
    uint64_t monoMs = 0; std::string home; std::string configJson;
    std::vector<StoredEntry> stored;           // every store key and value
    std::vector<std::string> inFlight;         // tags of commands still running in the host
};

// Inputs
struct Recovered {};                           // the driver has fed every finished result and one PanesListed
struct Wake { uint64_t monoMs = 0; };
struct ProcessFinished { std::string tag; ProcessOutcome outcome; };
struct FileRead { std::string tag; std::optional<std::string> content; };   // nullopt: missing, unreadable, not a regular file
struct PanesListed { std::vector<PaneFact> panes; };
struct PaneOpenedFor { std::string run; std::string pane; };
struct PaneOpenFailedFor { std::string run; std::string why; };
struct Dispatch { std::string repo; ItemRef item; std::string url; Task task = Task::FixCi; std::string model;
                  std::optional<std::string> note; std::optional<std::string> customPrompt; std::string session;
                  uint64_t wallMs = 0; int32_t utcOffsetMinutes = 0; };
struct RetryRemovalOf { std::string run; };
struct DismissRun { std::string run; };
struct RefreshNow {};
using Input = std::variant<Recovered, Wake, ProcessFinished, FileRead, PanesListed, PaneOpenedFor,
                           PaneOpenFailedFor, Dispatch, RetryRemovalOf, DismissRun, RefreshNow>;

// Effects
struct RunCommand { std::string tag; std::string_view command; std::vector<Argument> arguments; std::string cwd; };
struct ReadFile { std::string tag; std::string path; };        // always inside the `worktrees` root
struct OpenPane { std::string run; std::string_view paneTemplate; std::vector<Argument> arguments; std::string cwd; std::string title; };
struct StoreSet { std::string key; std::string value; };
struct StoreRemove { std::string key; };
struct PostInbox { std::string pane; std::string_view message; };
struct PublishSummary { std::string text; };
struct WakeAt { uint64_t monoMs = 0; };
struct Redraw {};
using Effect = std::variant<RunCommand, ReadFile, OpenPane, ClosePane, StoreSet, StoreRemove, PostInbox,
                            PublishSummary, WakeAt, Redraw>;

enum class DispatchRefusal : uint8_t { NotConfigured, AgentLimit, RepoLimit, UnknownRepo, UnknownModel,
                                       AlreadyRunning, BaseUnknown, BadPrompt };

class Engine
{
  public:
    struct Started;                                            // { Engine engine; std::vector<Effect> effects; }
    /// Builds the engine and its first effects. Never fails: a bad configuration is a state the
    /// engine reports through problems().
    [[nodiscard]] static Started start(StartContext context);
    [[nodiscard]] std::vector<Effect> step(Input const& input);
    [[nodiscard]] std::optional<DispatchRefusal> lastRefusal() const noexcept;
    [[nodiscard]] std::span<Run const> runs() const noexcept;
    [[nodiscard]] std::span<ChangeRequest const> changeRequests(std::string_view repo) const noexcept;
    [[nodiscard]] std::span<Issue const> unclaimedIssues(std::string_view repo) const noexcept;
    [[nodiscard]] std::span<WorktreeEntry const> orphans(std::string_view repo) const noexcept;
    [[nodiscard]] std::span<std::string const> problems() const noexcept;   // "gh: not logged in", …
  private: /* … */
};
```

  (`ClosePane` is the struct from `Reconcile.hpp`.) There is no `Started` *input*: the context is the
  constructor's argument, so there is no not-yet-started engine to tolerate.

  Conventions the tests pin:
  - **Clocks.** `monoMs` is monotonic and drives every interval; the engine's "now" is the latest one
    it was given (the start context or a `Wake`), and that is what it passes as `PaneOpened::monoMs`.
    Every difference is computed as `now >= then ? now - then : 0`. `wallMs` and `utcOffsetMinutes`
    appear only in `Dispatch`, for the names; the driver supplies them (the prototype from the system
    clock and `date +%z`; the guest from the host's `host-info`). The engine has no randomness: the
    driver supplies the session UUID.
  - **Tags.** A run's command or file read is tagged `stepTag(run)`; its board check `<id>/board`;
    polls are tagged `poll/<repo>/prs`, `poll/<repo>/issues/<n>`, `poll/<repo>/default-branch`,
    `poll/<repo>/worktrees`, `poll/auth`, `poll/hooks`. A result whose tag is unknown or stale is
    ignored.
  - **`cwd`** comes from the command's `Cwd` row: `Repo` → the repository's path (for `gh-auth` and
    `agent-hooks`, which concern no repository in particular, the first configured one), `Worktree` →
    the run's worktree. Nothing ever runs in `worktree_root` itself, which may not exist yet.
  - **Outcome → `RunInput`:** `StartResult::CwdMissing` → `CwdMissing`; `Refused`, `TimedOut`, a
    non-zero or missing exit code, or `Truncated` → `CommandFailed`; otherwise `CommandOk`. For a
    poll, `CwdMissing` produces its own problem line ("<path> does not exist"), never a "gh" one.
  - **`ReadGitFile`** → `ReadFile{…, "<worktree>/.git"}`; the answer becomes `GitFileRead`, judged
    against `mainCheckout` of that repository's last listing.
  - **Store.** `run/<id>` holds `toJson(run)`. `Persist` → `StoreSet`, `Forget` → `StoreRemove`.
    A stored value `runFromJson` rejects becomes a problem line naming the key; the key is **kept**
    (never `StoreRemove`d) so nothing is silently lost.
  - **Summary.** `⬢ agents <slots used>/<max> · ✗<failing PRs> · ●<blocked> waiting`, omitting a part
    whose count is 0.
  - **Driver contract.** The driver is single-threaded: it carries out every effect of a step before
    feeding the next input, and takes a pane snapshot only then. After every `Wake` it pulls panes and
    feeds `PanesListed`. After `Engine::start` it carries out the start effects, feeds every
    already-finished result, one `PanesListed`, then `Recovered`.

- [ ] **Step 1: Write failing scenario tests.** Two helpers in the test file:
  `finish(engine, effects, command, outcome)` finds the `RunCommand` with that command name and feeds
  `ProcessFinished` with its tag; `read(engine, effects, suffix, content)` does the same for a
  `ReadFile` whose path ends with `suffix`. Both return the new effects. "The ending" below means:
  `ReadFile …/.git` → intact → `git-status` → … as the 0.7 table says. In the scenarios, "`Panes`"
  is `PanesListed`, "`Dispatch`" the input of that name, "`Summary`" is `PublishSummary`,
  "`PaneOpened`" is `PaneOpenedFor`, "`RetryRemoval`"/"`Dismiss`" are `RetryRemovalOf`/`DismissRun`,
  "`FileRead None`" is `FileRead` with `nullopt`. Scenarios:
  1. **Start.** Starting with a one-repo config → `gh-auth` and `agent-hooks` are issued first, both
     with `cwd` = that repository, plus a `WakeAt`. No list command is issued until `gh-auth`
     succeeds; then `gh-default-branch`, `gh-prs`, one `gh-issues` per source and `git-worktree-list`.
     Feeding the `prs-open.json` fixture fills `changeRequests("contour")` and emits `Summary` and
     `Redraw`. With `worktree_root` not existing anywhere, all of this still happens.
  2. **A full PR dispatch:** `Dispatch` → `StoreSet run/…` + `git-fetch {remote: origin}` with
     `cwd == "/home/u/repo/contour"`; then `git-worktree-add` with `path` under
     `/home/u/worktrees/contour/` and `base == "origin/master"`; then `gh-checkout` with
     `cwd == worktree` and `branch == "agent/<run id>"`; then
     `OpenPane{template "agent", arguments model/session/prompt, cwd = worktree}`; `PaneOpened` →
     state `Running`.
  3. **An issue dispatch** uses the default branch learned in scenario 1 as `base`, issues
     `git-worktree-add-branch`, and never `gh-checkout`. Before `gh-default-branch` has answered and
     with no `base` configured, it is refused with `BaseUnknown`; with `base: main` configured it
     proceeds without waiting.
  4. **Limits.** With `max_agents: 1`, a second `Dispatch` emits nothing and
     `lastRefusal() == DispatchRefusal::AgentLimit`; with `max_agents_per_repo: 1` and two repos, the second
     dispatch in the same repo is `RepoLimit` and one in the other repo proceeds; a second dispatch of
     the same item while the first occupies a slot, or is `Kept`, is `AlreadyRunning`.
  5. **Clean ending.** After the agent reported `Working` then `Done`: `Panes` without the run's pane →
     `ReadFile …/.git`; intact → `git-status`; empty → `git-worktree-remove`; ok → `StoreRemove run/…`,
     and the slot is free again.
  6. **Kept, then cleaned by hand.** Status ` M f` → `Kept`, and no remove command is ever emitted. On
     the next poll the engine starts the ending again for the kept run; clean this time → removed.
     `RetryRemoval` does the same without waiting for the poll.
  7. **Removed by hand.** A `Kept` run whose `.git` read comes back `None` → forgotten. A `Running` run
     is never forgotten while its pane is listed, whatever `git-worktree-list` says.
  8. **Lost hints.** As scenario 5 but the pane's disappearance is only ever seen in the `Panes` that
     follows a later `Wake`: the ending still proceeds. `WakeAt` never lies further ahead than
     `RECONCILE_EVERY_MS` while any run occupies a slot.
  9. **Cold restart.** A new `Engine` started with `stored = {{"run/…", <Running run JSON>}}`, then
     `Panes` with that pane `program: Gone` → `ClosePane`, then the ending.
  10. **Guest restart mid-dispatch.** For a stored run in each of `Preparing(Fetch)`,
      `Preparing(WorktreeAdd)`, `Preparing(Checkout)`, `Ending(Verify)`, `Ending(Status)`,
      `Ending(Remove)`: (a) its tag is in `inFlight` → nothing happens until its `ProcFinished`
      arrives, then it continues normally; (b) its result arrives before `Recovered` → continues
      normally; (c) neither, at `Recovered` → `Lost` per the 0.7 table (so a possibly-created worktree
      is always checked). File reads are never in flight across a restart: a stored `Ending(Verify)`
      is always case (c). For `Preparing(OpenPane)`: a pane with the run's tag in `Panes` → adopted,
      `Running`; none at `Recovered` → `Lost` → the ending. **No path issues `git-worktree-remove`
      while a pane with the run's tag is listed.**
  11. **Status board.** A run `Running` for longer than `status_board_after` gets
      `PostInbox{pane, "status-board"}` on the first `Wake` past the threshold, and from then on
      each `Wake` issues `ReadFile <worktree>/STATUS.md` (tag `<id>/board`) until one comes back
      `Some`. It is posted again every `BOARD_REPOST_MS` while still `Running` and unseen, three posts
      in total; never while `Blocked`/`Done`; never when the setting is `0`. `boardPosts` and
      `boardSeen` survive `toJson`/`runFromJson`.
  12. **Problems and back-off.** `gh-auth` exiting 1 → `problems()` has a `gh` line, no list command
      is issued, and the next poll is `2 × poll_every` away, doubling to at most `MAX_BACKOFF_MS`;
      a success resets it and `gh-auth` is not run again until a list command fails. `gh-auth` with
      `StartResult::CwdMissing` → a "does not exist" line, not a `gh` one.
      `agent-hooks` reporting hooks not installed → a second problem line. (Run
      `contour agent setup --dry-run --json` first and record its real output as a fixture in
      `core/testdata/contour/`; write the parser against that fixture.)
  13. **Bad lists.** `gh-prs` with `truncated`, with non-JSON, or with a non-zero exit → the
      previous list is kept, a problem line is added, nothing panics. A list `AtLimit` or with
      `skipped > 0` → shown, plus a problem line saying it is incomplete and why.
  14. **Concurrency.** Starting with three repos never has more than `MAX_IN_FLIGHT` commands
      unanswered; the rest are issued as results arrive; a run's commands are queued ahead of polls.
  15. **Orphans.** A `git-worktree-list` result naming an `agent/…` branch no run owns, or a detached
      worktree under `worktree_root` no run owns → `orphans("contour")` has it and `problems()`
      mentions it.
  16. **Every effect is well-formed:** a helper asserts, for every `RunCommand` and `OpenPane`
      produced across all scenarios, that the command or pane template is declared in
      `commands()` / `paneTemplates()`, that the argument names equal its placeholders, that
      every value passes `argumentAccepts` for its kind, and that `cwd` passes `argumentAccepts(ArgumentKind::Path, _)`; and for
      every `ReadFile`, that `path` lies under `worktree_root`.
  17. **A bad configuration** (starting with JSON that fails `parseConfig`) → one problem line, no
      commands, and every `Dispatch` is refused with `NotConfigured`.
  18. **An agent that never started.** `PaneOpened`, then `Panes` without the pane and no status ever
      reported → the ending; once the worktree is removed the run is `Failed{OpenPane}` with "the agent
      exited before reporting", not forgotten. `Dismiss` forgets it.
  19. **A tampered worktree.** At the ending, `.git` read as `gitdir: /tmp/evil/.git/worktrees/x` →
      `Kept`, and **no `RunCommand` with `cwd` = that worktree is ever issued** afterwards except
      through a later `.git` read that is intact. Before any `git-worktree-list` answered
      (`mainCheckout` unknown) the result is also `Kept`.
  20. **A stored run that cannot be read** (`("run/x", "garbage")`, or `"v": 2`) → a problem line
      naming `run/x`; no `StoreRemove`; the other runs load.
  21. **A reboot.** Starting with `monoMs = 5` and a stored `Running` run → no panic; the status-board
      threshold counts from this start, not from a stale timestamp.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); lint, format, tidy.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator: step inputs into effects"`

### Task 0.10: The prototype driver

**Files:** Create `extensions/orchestrator/proto/Main.cpp`; modify
`extensions/orchestrator/CMakeLists.txt`.

**Interfaces:**
- Consumes: `Engine`, `Input`, `Effect`, `findCommand`, `findPaneTemplate`, `argumentAccepts`,
  `commandEnvironment`.
- Produces: a native executable target `orchestrator_proto` — `EXCLUDE_FROM_ALL`, POSIX only
  (`if(UNIX)`), never installed, not part of any package. Build with
  `cmake --build --preset clang-asan -j4 --target orchestrator_proto`; run
  `out/clang-asan/extensions/orchestrator/orchestrator_proto <config.json>`.

  The driver is deliberately dumb — it is the model for the wasm guest's effect loop — and
  **single-threaded**: one `poll(2)` loop over stdin and the children's pipes with a one-second
  timeout, which carries out every effect of a step before feeding the next input, and only then
  takes a pane snapshot. It is a native tool, so the Guest code rules do not apply to it (it may use
  `<iostream>`, `<filesystem>`, `posix_spawnp`).
  - `RunCommand` → check every argument with `argumentAccepts`, then `posix_spawnp` with the declared
    argv, placeholders substituted as whole arguments, the working directory set
    (`posix_spawn_file_actions_addchdir_np`, or `fork`/`chdir`/`execvp` where that is missing),
    `commandEnvironment()` added, stdout and stderr captured through pipes (cap 1 MiB each →
    `Truncated`), killed at its timeout. A `cwd` that does not exist → `StartResult::CwdMissing`
    without spawning. Children run concurrently; each result is fed as `ProcessFinished` when the
    loop reaps it.
  - `ReadFile` → refuse a path outside the worktree root; read at most 64 KiB if it is a regular file
    (`std::filesystem::symlink_status`, so neither a symlink nor a FIFO is opened); feed `FileRead`.
  - `OpenPane` → `contour api call tab.create` with `cwd`, `label: <title>`, the focus value that
    keeps focus where it is, and
    `"command": ["env", <the pane's environment as KEY=value…>, <its argv>]`; the reply's `pane` is
    fed back as `PaneOpenedFor`. (`env` stands in for the host's `pane-env`, which does not exist yet.
    Run `contour api describe tab.create` first for the exact request and the spelling of the focus
    values. Build and parse the JSON with glaze.)
  - `ClosePane` → `contour api call pane.close`.
  - Panes → `contour api call workspace.list`, flattened to `PaneFact`s for the panes the driver
    opened (it keeps its own pane → tag map in the store directory); `program` is `Alive` while the
    pane is listed. A cold restart is out of the prototype's reach — say so in `--help`.
  - Store → one file per key under `$XDG_STATE_HOME/contour/orchestrator-proto/`.
  - `PostInbox` → printed as "would post"; `PublishSummary`/`Redraw` → reprint a plain list.
  - A line-mode prompt: `list`, `dispatch <index> <fix-ci|review|issue> [model] [note…]`, `runs`,
    `retry <run>`, `dismiss <run>`, `refresh`, `quit`. Each loop turn feeds `Wake` then `PanesListed`.
    After `Engine::start`: its effects, one `PanesListed`, `Recovered` (`inFlight` is empty: the
    prototype's processes die with it).
  - Session UUIDs from `/proc/sys/kernel/random/uuid`, falling back to `uuidgen`; the UTC offset from
    `localtime_r`'s `tm_gmtoff`.

- [ ] **Step 1: Write the driver** (≈450 lines; no tests of its own — it has no logic the core does not
  own, and anything that turns out to need a test is moved into the core).
- [ ] **Step 2: Build it** under the pedantic preset. Expected: no warnings; the default `all` build
  does not include it.
- [ ] **Step 3: Try it against a real daemon** with a config naming one repository and a
  `worktree_root` that does not exist yet. Expected: `list` shows the user's open PRs; `dispatch` on a
  PR with failing CI opens a tab running `claude` in a new worktree under `worktree_root` without
  stealing focus; closing that tab leads to `runs` no longer showing it, and `git worktree list` no
  longer naming it. Dispatch an issue too and check, in its tab, that `git push --dry-run` names a new
  remote branch `agent/…` and **not** the default branch.
- [ ] **Step 4: Commit.** `git commit -m "extensions: a prototype driver for the orchestrator core"`

### Task 0.11: Spikes

Each spike ends by updating the spec's "Facts about Claude Code" paragraph (§3) — moving the item from
*not documented* to *measured, with the version* — in the same commit as any fixture.

- [ ] **Spike A — a slash command as the initial prompt.** In a scratch repository run
  `claude --model haiku --session-id "$(uuidgen)" "/contour-workflows:sloc"` in a daemon pane.
  Expected: the skill runs without further input. If it does not, change the default prompt templates
  in `Config.cpp` to plain language naming the skill ("Use the contour-workflows:fix-ci skill on {url}")
  and update the §6.5 example and the tests of task 0.5.
- [ ] **Spike B — context injected by `PostToolUse` mid-turn.** Temporarily add to a scratch project's
  `.claude/settings.local.json` a `PostToolUse` hook that prints
  `{"hookSpecificOutput":{"hookEventName":"PostToolUse","additionalContext":"A status board has been requested for this run. The contour-workflows:status-board skill keeps STATUS.md in the working directory current; it is wanted locally only, without publishing."}}`
  once (guard with a marker file). Give the agent a task with several tool calls. Expected: it starts
  maintaining `STATUS.md` and does not publish. Record the wording that worked; it becomes the
  `inbox status-board` line of the manifest. Try three runs; if it is not reliable, record that and
  raise it before phase 4 — the fallback is a `Stop`-hook continuation.
- [ ] **Spike C — a fork PR in a worktree.** With the prototype, dispatch on a PR whose head is a fork.
  Expected: `gh pr checkout --branch` succeeds inside the detached worktree, and from the agent tab
  `git push` reaches the fork's branch. Also confirm on a same-repository PR whose head branch is
  checked out in the main checkout, and in a checkout with two remotes note which repository `gh`
  lists PRs for.
- [ ] **Spike D — agent status and first-run prompts in a new worktree.** With `contour agent setup`
  installed, confirm the dispatched tab's badge moves through working → blocked (on a permission
  prompt) → done, and note what
  `contour events --follow --json --kind pane.programStatusChanged` prints; save one event of each as a
  fixture under `core/testdata/contour/`. Also record whether Claude Code shows its folder-trust
  prompt in a never-seen worktree path, and what status the pane reports while it does — every
  dispatch creates a new path, so if it asks every time, raise it with the user before phase 3.
- [ ] **Commit.** `git commit -m "docs: record what the orchestrator spikes measured"`

### Phase 0 gate

- `orchestrator_test`, `vtextension_test "[argument-kinds]"` and `orchestrator_guest_portability` pass;
  the pedantic build is warning-free; ASan/UBSan report nothing in `orchestrator_test`.
- The four spikes are answered in the spec.
- The user has dispatched at least one real PR with the prototype and confirmed the workflow is the one
  they want. **Anything they want changed is changed in the spec first**, then in the core.

---

# Phase U — core-cpp: a TUI a guest can use

In the core-cpp repository, in parallel with phases 0 and 1. Phase 2 cannot start before U.6. Every
task is a pull request there, under core-cpp's own rules; nothing under `vendor/core-cpp` is edited in
Contour.

### Task U.0: Agree the shape upstream

Open an issue in core-cpp quoting §5.8 of the spec and the evidence behind it: `Screen.hpp:112`
(`explicit Screen(Terminal& terminal, …)`), `Terminal.cpp:40,50,117` (the `dynamic_cast`s),
`Buffer.cpp:82,89` (the throws), `cmake/CoreCppModules.cmake:260-261` (`tui … DEPS base platform async
net PLATFORMS native`), `cmake/CoreCppOptions.cmake:75-83` (only Emscripten is known),
`Component.cpp`/`InputField.cpp` including `Screen.hpp`. Settle three things before any code: the name
and contents of the headless target; whether `Screen` becomes tty-free or the guest gets a smaller
frame driver beside it; and the libunicode question (U.5). **Record the answers in the spec's §5.8.**

### Task U.1: The headless target

- A target (working name `core::tui_core`) with `VtParser`, `InputEvent`, `KeyCode`, `Modifier`,
  `KeyBindings`, `Rect`, `Cell`, `Buffer`, `Canvas`, `Component`, `Theme`, `Unicode` and the widgets;
  `DEPS base tui_output`, `PLATFORMS any`. Widgets stop including `Screen.hpp`/`Terminal.hpp`.
- Test: the target builds and its existing tests pass with `tui` itself switched off.

### Task U.2: A frame driver with no tty

- `Screen` (or the new driver) takes a size and a byte sink instead of a `Terminal&`; the inline
  viewport's cursor query stays in the tty layer. The diff is exposed as a function:
  previous buffer × current buffer → bytes. `TerminalOutput` gets a base with no operating-system
  calls; headless is an explicit property, not a `dynamic_cast`.
- Test: a headless `Screen` test that feeds bytes through `VtParser`, dispatches, and compares
  `canvasToString` — the harness Contour's view tests will reuse.

### Task U.3: No exceptions, no RTTI

- `Buffer::at` no longer throws (a checked accessor returning a pointer or `expected`); the headless
  target builds with `-fno-exceptions -fno-rtti`.
- Test: a CI configuration building the headless target with those flags.

### Task U.4: An injected clock

- `HoverState`, `TreeTableView`, `InputField` and the hover tick take a clock instead of calling
  `steady_clock::now()`.
- Test: time-dependent behaviour tested with a manual clock.

### Task U.5: Widths without a system libunicode

- Decide and implement one: (a) confirm libunicode builds for `wasm32-wasip1` and document how a guest
  build obtains its source; or (b) let the caller of the headless target supply the width and
  grapheme-boundary functions, with libunicode as the default implementation on native platforms.
- Test: for (a), the wasm CI job of U.6 links it; for (b), a test with a stub provider.

### Task U.6: WASI in the build, a release, and re-vendoring

- core-cpp's platform table learns `wasm32-wasip1`; a CI job cross-compiles the headless target with
  wasi-sdk (the command in this plan's Verified facts is the starting point).
- Release a tag. In Contour, re-vendor with that tag and the module list
  `base;log;cli;platform;async;net;testing;codec;tui` (`codec` is mandatory from v0.20.0), using the
  **checkout's** `cmake/CoreCppVendor.cmake` as `vendor/README.md` describes, in one commit.
- In Contour: keep the tty half of `tui` off, enable the headless target for the native build
  (`cmake/ContourThirdParties.cmake:467`), fix whatever the move from v0.19.0 breaks in Contour's use
  of the other modules, and run the whole suite including `core-cpp-vendored-copy`.

### Phase U gate

A core-cpp release whose headless TUI target (1) builds for `wasm32-wasip1` with `-fno-exceptions
-fno-rtti` in core-cpp's CI, (2) drives a screen from bytes to bytes with no descriptor, (3) takes its
clock as a parameter; and a Contour tree vendoring it with every existing test green.

---

# Phase 1 — The daemon world

Usable result: a `{DaemonExtension:orchestrator}` status-line segment showing the user's PR counts,
polled by a guest that runs with no window attached.

### Task 1.0: Expand this phase to step level

Read, at minimum: `src/vtextension/{Manifest,CompiledExtension,Engine,Runner,Instance,HostBindings,StatuslineWorld,ExtensionLimits,EventQueue,CommandPolicy,ExtensionComposition,ExtensionConsent}.{hpp,cpp}`,
`src/vtextension/wasm/{WasmRuntime.hpp,wamr/WamrRuntime.hpp,wamr/WamrInstance.cpp}`,
`src/vtextension/testing/{Harness,RunnerHarness,TestModules}.hpp`,
`src/vthost/{DaemonExtensions,ExtensionHosting,CommandRunner,StdioChild,WorkspaceGit}.hpp`,
`src/vtpty/CommandCapture.hpp`, `cmake/ContourCargo.cmake` (the model for 1.10),
`src/vtextension/wasm/{WasiAllowList.hpp,wamr/HiddenExports.hpp}`,
`src/vtbackend/screen/{StatusLineBuilder,StatusLineExtensions}.hpp`,
`src/vtbackend/screen/Terminal.cpp` around `statusLineExtensionText`,
`src/vthost/client/ScreenMirror.hpp:323-326`, `src/contour/cli/ContourApp.cpp:1035-1090,1386-1500`,
`docs/extensions.md`. Write the steps for tasks 1.1–1.11 into this file, each with its failing test
first, and have the result reviewed before coding.

### Task 1.1: Manifest version 2

- **Files:** `src/vtextension/Manifest.{hpp,cpp}`, `Manifest_test.cpp`, `docs/extensions.md`. (The Rust
  `extensions/manifest` macro is not touched: the Rust guests stay on version 1.)
- **Produces:** `SupportedVersions = {1, 2}`; `Manifest` gains
  `enum class ManifestPlacement : uint8_t { StatusLine, Daemon }`, `std::vector<DeclaredRoot> roots`
  (label + config pointer), the readable roots, a root label on every command and pane, a root on
  every `path` argument, `std::vector<DeclaredArgument> arguments` (name + `ArgumentKind` from
  `ArgumentKinds.hpp`, task 0.3), command environment,
  `std::vector<DeclaredPane> panes` (argv template + environment),
  `std::vector<DeclaredInboxMessage> inbox`, `std::vector<std::string> servedSchemes`,
  `std::vector<DeclaredAction> actions`; `DeclaredCommand` gains
  `enum class CommandTimeout : uint8_t { Short, Long }`. Argument checking is `argumentAccepts` from task 0.3 — the same header
  the guest includes, so there is one implementation.
- **Tests (table):** every §5.1 line kind parses; each is rejected under `version = 1`; a version-1
  manifest parses byte-for-byte as before and keeps its digest; duplicates, a placeholder with no
  `arg` line, a version-2 command or pane without `@<root>` or naming an undeclared root, `read` or
  `path:` naming an undeclared root, a `pane-env` for an undeclared pane, a `root` whose pointer is malformed, an `inbox`
  text with a control character, an `action` id outside `[a-z0-9-]`, a `command-env` key outside
  `[A-Z0-9_]`, and `placement daemon` together with a `control` line are each `ManifestInvalid`.
  The host parses the orchestrator's real manifest (the output of `manifestLines()` plus the fixed
  lines) without error — a test that links `orchestrator_core` into `vtextension_test` is not allowed
  (layering), so feed it a checked-in copy that task 1.10's test keeps in sync.

### Task 1.2: The daemon world's engine, table and world check

- **Files:** `src/vtextension/{Engine.{hpp,cpp},HostBindings.hpp,CompiledExtension.{hpp,cpp},Instance.{hpp,cpp}}`,
  new `DaemonWorld.hpp` and `DaemonInstance.{hpp,cpp}`, `wit/daemon/world.wit`, `Wit_test.cpp`,
  `ExtensionComposition` (the `EngineProvider`).
- **Produces:** `enum class World : uint8_t { StatusLine, Daemon }`, decided at load from the manifest's
  placement and checked against the module's exports (`load`, `update`, `cabi_realloc`, `memory`; a
  daemon module exporting `render`, or a status-line module without it, is `WorldMismatch`). The host
  table is bound in `Engine::create` (`Engine.cpp:18-20`) and limits are per runtime, so the daemon
  world gets **its own `Engine`** built with its own table and `ExtensionLimits`. `DaemonInstance` is
  the daemon-world sibling of `Instance` with its own handler table (the status-line one is at
  `Instance.cpp:229`).
- **The invariant this breaks, and the three steps that restore it (C22).** WAMR runtimes are
  reference-counted and do coexist, but the native registry is process-wide, and today's code is only
  correct because every live runtime registers the same functions
  (`wasm/WasmRuntime.hpp:48-56,333-334`, `wamr/WamrRuntime.hpp:103-105`); the trampoline's attachment
  is just a table index (`WamrInstance.cpp:304-314`), so a module linked against the other world's
  registration would call the wrong handler. Therefore: (1) the daemon world's imports live under
  their own module name, `terminal-extensions:daemon/host@0.1.0`, which no status-line table
  registers; (2) the import check (`isProvided`, `CompiledExtension.cpp:74-80`) stops accepting a
  whole import module by name and becomes an exact per-world `(module, name)` table, so a module that
  imports anything from the other world fails to load; (3) the `createRuntime` contract comment is
  rewritten to say what now holds.
- **Tests:** the existing suite unchanged; a daemon test guest loads on the daemon engine; the two
  export mismatches are refused; a daemon module is refused by the status-line engine and the reverse;
  a module importing one function from each world is refused by both; with both engines alive, a call
  from each world reaches its own handler (the regression test for the cross-link); the WIT file
  matches the codec rows (model: the existing `Wit_test.cpp`); the backend contract suite passes for a
  second runtime in one process.
- **Watch:** the `static_assert(StatuslineHostFunctions.size() == 4)`; the stale "five" in
  `WasmRuntime.hpp:120` (the code says 17) — fix the comment while there.

### Task 1.3: `DaemonExtensionRunner`

- **Files:** new `src/vtextension/DaemonExtensionRunner.{hpp,cpp}`, `_test.cpp`; `ExtensionWorkerPool`
  if a dedicated lane needs a policy.
- **Produces:** a runner constructed with `(CompiledExtension, DaemonServices, IExtensionScheduler&,
  ExtensionLimits, config JSON, HostInfo)` where `DaemonServices` is a struct of references to the
  guest-facing interfaces of tasks 1.4–1.6 (later phases append to it). It calls `load`, delivers
  hints, restarts the guest after a fault with the existing back-off, and disables after
  `faultsBeforeDisable`. `enum class DaemonHint : uint8_t { Wake, Procs, Panes, Tabs, Actions }`, one
  coalescing slot each — no stack, nothing droppable.
- **Tests (with `ManualScheduler` and fakes):** hints coalesce; a fault re-runs `load`; processes
  started before a fault are still listed to the new instance with their tags; three faults disable and
  the notice names the extension; destruction while a hint is queued is safe.

### Task 1.4: Processes

- **Files:** new `src/vtpty/ProcessRunner.{hpp,cpp}` (+ `_test.cpp`; posix implementation, a Windows
  implementation returning `Unsupported`); new `src/vtextension/IProcessService.hpp` and
  `DeclaredProcesses.{hpp,cpp}` (+ `_test.cpp`).
- **Produces, raw layer (`vtpty`, knows nothing of manifests):**

```cpp
struct ProcessSpec { std::vector<std::string> argv; std::string workingDirectory;
                     std::vector<std::pair<std::string, std::string>> environment;
                     std::chrono::milliseconds timeout; std::size_t outputCap; };
enum class ProcessStartError : uint8_t { DirectoryMissing, SpawnFailed, Unsupported };
class IProcessRunner {
  public:
    virtual ~IProcessRunner() = default;
    /// Starts @p spec; @p done runs once, on the runner's completion executor.
    [[nodiscard]] virtual std::expected<ProcessToken, ProcessStartError> start(ProcessSpec spec, CompletionHandler done) = 0;
    virtual void cancel(ProcessToken token) = 0;
};
```

  Direct `exec` of the argv, both pipes drained off the loop, cap with a truncated flag, SIGTERM then
  SIGKILL at the timeout. **The clock and the grace period are constructor parameters**, so the timeout
  tests use a manual clock and never sleep. Look at `vthost::IStdioChild` and `WorkspaceGitTracker`
  before writing a new spawn path.
- **Produces, policy layer (`vtextension`):** `DeclaredProcesses` implements `IProcessService`
  (`start(command, args, cwd, tag)`, `list()`, `take(handle, stream, offset)`) over an injected
  `IProcessRunner`: looks the command up, checks consent through `IGrantStore`, checks every argument
  with `argumentAccepts` (a `path` argument against *its* root), resolves the working directory and
  refuses one outside **the command's own root** (after normalisation and symlink resolution, through
  an injected filesystem interface that hands back a directory handle — the child is started from
  that handle, `open(O_DIRECTORY)` + `fchdir`, not by re-resolving the name), applies the class
  timeout from a constructor-supplied `ProcessTimeouts { shortClass, longClass }`, adds the manifest's
  command environment, enforces at most 8 running and 32 untaken results, drops a result once its last
  chunk is taken, and posts the `Procs` hint.
  `enum class ProcessError : uint8_t { NotDeclared, Pending, Denied, ArgumentRejected, CwdOutsideRoots, CwdMissing, TooMany, Unsupported }`.
- **Tests:** raw — exit codes 0 and 3; stderr captured; a 2 MiB writer is truncated at the cap and
  reaped; a sleeper is killed when the manual clock passes the timeout; a child that ignores SIGTERM is
  killed after the grace period; destruction with a child running kills and reaps it. Policy (against a
  fake runner) — an argument failing its kind, a `cwd` outside every root, a `cwd` inside a root that
  is not the command's (a worktree command aimed at a repository), a `path` argument inside the wrong
  root, a `cwd` that is a symlink leaving a root, an undeclared command, a ninth concurrent start; paging returns contiguous 64 KiB
  chunks; tags come back in `list()`.

### Task 1.5: The store

- **Files:** new `src/vtextension/IExtensionStore.hpp`, `FileExtensionStore.{hpp,cpp}`, `_test.cpp`.
- **Produces:** `get/set/remove/keys(prefix)`; keys `[a-z0-9/._-]{1,128}` with no `..` component;
  values ≤ 64 KiB; ≤ 1024 keys; one file per key under `<state-dir>/extensions/<name>/store/`, written
  to a temporary name and renamed. The filesystem is reached through an injected interface so tests use
  a temporary directory and a failing double.
- **Tests:** round trip; survives a new instance over the same directory; a key with `..` or an
  absolute path is refused; an oversized value is refused; a torn temporary file is ignored on load.

### Task 1.6: `log`, `wake`, `monotonic-ms`, `summary-set`

- **Files:** `DaemonWorld.hpp`, `DaemonInstance`, `DaemonExtensionRunner`, new
  `src/vtextension/IExtensionPresence.hpp` (summary half).
- **Produces:** `wake(after-ms)` with the existing 16 ms floor; `monotonic-ms()` from the scheduler's
  clock; `summary-set(text)` sanitised by the host (control characters dropped, ≤ 120 characters) and
  handed to `IExtensionPresence::setSummary`.

### Task 1.7: Configuration and composition

- **Files:** `src/contour/config/{Config.hpp,Config.cpp,ConfigReader.hpp,ConfigDocumentation.hpp}`,
  `DaemonSettings_test.cpp`, `src/contour/cli/ContourApp.cpp`,
  `src/vthost/{ExtensionHosting.hpp,DaemonExtensions.{hpp,cpp},Daemon.cpp}`.
- **Produces:** `DaemonSettings::extensions` — a map of name to
  `{ path, autostart: enum class ExtensionAutostart : uint8_t { OnDemand, AtDaemonStart }, config: JSON text }`;
  a YAML-subtree-to-JSON conversion in `contour::config` (table-tested: scalars keep their YAML type,
  maps and sequences nest, a 17-level nest is refused); root resolution from the config pointer
  (`repos/*/path`); `IExtensionHosting` gains the daemon-scoped slot, expressed in `vthost`-owned types
  so `vthost` still builds with extensions off; `ContourApp` builds the services and the runner.
- **Tests:** the reader round-trips the §6.5 example; pointer resolution table (missing key, non-string
  leaf, `*` over a non-list); a `vthost_test` with `FakeExtensionHosting` proves the daemon-scoped
  extension starts at daemon start when asked and not otherwise; **a `CONTOUR_EXTENSIONS=OFF`
  configure-and-build of `vthost` still succeeds** (run it; it is the constraint most easily broken).

### Task 1.8: Consent and the `contour extension` verbs

- **Files:** `src/vtextension/ExtensionConsent.{hpp,cpp}`, `src/vthost/PermissionWire.hpp`,
  `src/vthost/client/DaemonGrantReplies.*`, `src/contour/cli/` (new `ExtensionVerbs.{hpp,cpp}`),
  `docs/extensions.md`.
- **Produces:** `consentQuestion()` renders version-2 declarations, the resolved roots, and names the
  combination; `contour extension list` (name, placement, grant state, digest), `grant <name>`,
  `revoke <name>`.
- **Tests:** the question text for the orchestrator's manifest is pinned as a golden string; the verbs
  against an in-memory grant store.
- **Check in 1.0:** today's consent is asked per pane session; find where a daemon-scoped question is
  queued and which window shows it.

### Task 1.9: The summary in the status line

- **Files:** `src/vtbackend/screen/{StatusLineBuilder.{hpp,cpp},StatusLineExtensions.hpp,Terminal.{hpp,cpp}}`,
  `src/vthost/{SessionHost.{hpp,cpp},DaemonExtensions.*}`, `docs/`.
- **Produces:** the item `{DaemonExtension:<name>}`, resolved **in the daemon's terminal** next to
  `statusLineExtensionText` (`Terminal.cpp:991`), from a provider injected through `SessionHost`. No
  PDU: windows mirror the row the daemon draws. A summary change marks each session's status row dirty,
  rate-limited to the existing frame interval.
- **Tests:** the item parses and renders the provider's text; renders nothing when the extension is
  absent; a daemon whose only extension is daemon-scoped still draws the indicator row — the condition
  is `Terminal::indicatorStatusLineDependsOnHostState` (`Terminal.cpp:868-878`, used at
  `NativeSession.cpp:1508`), which is false while `_extensions` is null, so it needs a branch for the
  provider, and the item must be in the daemon's own template; pin both; `vtbackend`'s layering holds (the provider is an interface in `screen/`).
- **Watch:** in a GUI-hosted (non-daemon) session the item resolves to nothing; say so in the docs.

### Task 1.10: The C++ guest toolchain

- **Files:** new `cmake/ContourWasiCxx.cmake`, `cmake/toolchains/wasm32-wasip1.cmake`; root
  `CMakeLists.txt` (option `CONTOUR_EXTENSIONS_ORCHESTRATOR`, default `OFF`);
  `.github/workflows/{build,clang-tidy}.yml` (install the toolchain in the jobs that enable it);
  `docs/` (build requirements).
- **Produces:** detection of clang with the wasm32 target, `wasm-ld`, and a sysroot — from
  `WASI_SDK_PREFIX`, else the distribution layout (`/usr/share/wasi-sysroot` plus the system clang's
  `wasm32-unknown-wasip1` builtins) — proven at configure time by compiling and linking a C++23
  reactor; a `FATAL_ERROR` naming what is missing, as `ContourCargo.cmake` does for Rust. A function
  `contour_add_wasm_cxx_guest(NAME <n> SOURCE_DIR <dir> OUTPUT_VAR <var>)` that runs a **cross
  sub-build** (`ExternalProject` or `add_custom_command` invoking CMake with the toolchain file),
  because the guest links libraries that must be built for wasm too. The toolchain file fixes:
  `--target=wasm32-wasip1`, `-fno-exceptions -fno-rtti -fvisibility=hidden`, `-mexec-model=reactor`,
  `-Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=65536`, and `-Os -flto` only when the sysroot
  ships LTO libraries for this clang version. Optional `wasm-opt -Os` when binaryen is found.
- **Tests:** a configure on a machine without the toolchain and with the option `OFF` is unchanged; with
  the option `ON` and no toolchain it fails with the message; with both, a CTest entry checks the built
  module with a small script: the manifest custom section is present exactly once, there is no start
  section, no `__wasm_call_ctors` export, and every import is from the daemon host module or the WASI
  allow-list (`src/vtextension/wasm/WasiAllowList.hpp`).
- **Watch:** offline distribution builds — everything must come from packages; nothing is downloaded.

### Task 1.11: The guest

- **Files:** new `extensions/orchestrator/guest/{CMakeLists.txt,Guest.cpp,HostCalls.{hpp,cpp},Manifest.S}`,
  `extensions/orchestrator/guest/bindings/{daemon.h,daemon.c}` (generated, committed),
  `extensions/orchestrator/manifest/Main.cpp`; `extensions/CMakeLists.txt` (install beside the bundled
  modules); `src/vtextension/Bundled_test.cpp`.
- **Bindings.** Generated once with `wit-bindgen c wit/daemon --world <world>` (0.63.0 — the version
  the Rust guests pin) and committed, with the command in a header comment and in `docs/`. The
  generated code references `__component_type_object_force_link_<world>`, normally satisfied by the
  generated `*_component_type.o`; **do not commit that object** — decide in 1.0, by trying it, between
  `--no-object-file` plus a one-line definition of that symbol in `Guest.cpp`, and generating the
  object at build time when `wit-bindgen` is present. A lint test regenerates and diffs when the tool
  is installed, and is skipped (not failed) when it is not.
- **Manifest.** `orchestrator_manifest` is a small *native* executable that prints the full manifest
  (`name`, `version = 2`, `placement daemon`, the roots, `read`, `inbox`, `serve`, `action` lines, and
  `manifestLines()`); the sub-build runs it and `Manifest.S` embeds the result with
  `.section ".custom_section.terminal-extension-manifest","",@` + `.incbin`. The `section` attribute
  does not work (Verified facts).
- **Produces:** the wasm guest. `load` builds the `StartContext` (every store key, the running tags
  from `procs()`), calls `Engine::start`, carries out its effects, feeds every finished result, one
  `PanesListed`, and `Recovered`; `update` maps hints to pulls and inputs; effects map to host calls
  exactly as the prototype maps them to processes. Timers are a guest-side list over the single
  `wake`. The engine lives in a function-local static created in `load` (a reactor has no `main`).
  Exports use `[[clang::export_name]]`; nothing in the guest is reachable before the host calls
  `_initialize`.
- **Tests:** `orchestrator_manifest`'s output equals the checked-in copy task 1.1's test parses (one
  CTest entry, so host and guest cannot drift); `Bundled_test.cpp` loads the real module against the
  host fakes, answers `gh-auth` and `gh-prs` with the phase-0 fixtures and expects the summary; a fault
  injected after `proc-start` proves the restarted guest picks the result up by tag; the portability
  lint now also covers `guest/`.

### Phase 1 gate

- **R2 measurement.** Inside the real guest on WAMR, time `update` handling a `gh-prs` result built
  from the largest recorded fixture scaled to 100 PRs (≈ 1 MiB — which is also the stream cap, so
  measure whether a full list fits at all). Record the numbers in the spec, with the module's size
  before and after `wasm-opt`. If it exceeds the daemon
  world's deadline or the cap: replace `--json …` with a brace-free, space-free `--jq` filter that
  emits arrays — for example
  `[.[]|[.number,.title,.url,.headRefName,.baseRefName,.isDraft,.isCrossRepository,.reviewDecision,[.closingIssuesReferences[].number],[.statusCheckRollup[]|[.__typename,.name,.status,.conclusion,.state]]]]`
  — and re-record the fixtures; or split parsing across wakes. Do not simply raise the deadline past
  what a daemon lane can justify without saying so in the spec.
- `bench-headless` with the orchestrator loaded and polling: no throughput regression.
- `ctest --preset=clang-asan` and `ctest --preset=clang-tsan` (the runner crosses threads) pass; a
  `CONTOUR_EXTENSIONS=OFF` build passes; a build with `CONTOUR_EXTENSIONS_ORCHESTRATOR=OFF` is
  byte-for-byte what it was before this phase as far as installed files go.
- The module loads under a **system** WAMR if any supported distribution ships one (R9); if it does
  not, say so in the docs and keep the option off there.
- The segment shows live counts with every window closed and reopened.

---

# Phase 2 — The served tab and the read-only dashboard

Usable result: "Open Agents" in the palette opens the dashboard tab, marked by Contour's chrome, listing
PRs, issues and problems. No dispatch yet.

### Task 2.0: The decision gate, then expand this phase

Read `src/vthost/handover/HandoverCapture.cpp:57-85` and the session capture it feeds, `SessionHost`'s
`PtyFactory`/`SessionSpawnRequest` (`SessionHost.hpp:101,107,823`), the snapshot codec, and
`vtpty/ChannelPty.hpp`. Write down how a served session crosses a live upgrade and a cold restart.
**If no representation is tractable, stop and take R1's fallback to the user** before writing any
phase-2 code. Otherwise expand tasks 2.1–2.7 to step level.

**Second gate: phase U.** Tasks 2.1–2.5 are host work and can proceed without it; 2.6 and 2.7 need
the re-vendored headless TUI (U.6). If U is not released, do 2.1–2.5 and stop there.

### Task 2.1: Served sessions in the daemon

- **Files:** `src/vthost/SessionHost.{hpp,cpp}`, `Daemon.cpp`, new `src/vthost/ServedSessions.{hpp,cpp}`
  (a `vthost`-owned interface plus implementation), new `src/vtextension/IServedTabs.hpp` (what the
  guest's imports call), the adapter in `DaemonExtensions`, tests.
- **Produces:** a way for `SessionSpawnRequest` to ask for a served scheme; the session's device is a
  host-owned `ChannelPty`; input from clients is buffered per tab (64 KiB; overflow sets a flag and
  drops) and posts the `Tabs` hint; `tab-open(scheme, origin)`, `tab-close`, `tabs()`, `tab-read`,
  `tab-write` (≤ 64 KiB per call); `tab-reveal(tab, origin)` is declared here and wired in 3.4; `servedBy` on the session; **restricted terminal settings** for a
  served session: no OSC 52 clipboard, no notifications, no hyperlinks, host-fixed title.
- **Tests:** bytes written by the guest reach the session's terminal; client input reaches `tab-read`
  in order; resize appears in `tabs()`; closing the tab removes it; `tab-open` places the tab in the
  origin's workspace; an OSC 52 write, a notification sequence and a title change written by the guest
  have no effect outside the grid; a guest fault leaves the tab open and blank, and the restarted guest
  can draw into it.

### Task 2.2: Refusals

- **Files:** `src/vthost/control/ControlService.cpp`, `ControlService_test.cpp`, `docs/control-api.md`.
- **Produces:** `pane.send`, `pane.capture`, `block.list`, `block.read`, `block.wait` on a served pane
  fail with a dedicated `ControlError`; the same through the extension control bridge.
- **Tests:** one row per verb, through the socket path and the extension path.
- **Docs:** state plainly what this does not cover (§7): a native-protocol client can still send input.

### Task 2.3: Live upgrade and restart

- **Files:** `src/vthost/handover/*`, `src/vtbackend/state/*` if the terminal state is captured, the
  snapshot codec, tests.
- **Produces:** a served session is captured as its scheme and extension name; after the exec it is
  re-created and the guest is told through the `Tabs` hint; a cold restart restores it the same way or
  drops the tab (decide in 2.0; pin with a test either way).
- **Tests:** the handover round trip with a served session present no longer returns
  `UnsupportedSession`.

### Task 2.4: Actions to and from windows

- **Files:** `src/vthost/proto/{Pdu.hpp,Pdu.cpp,PduTrace.cpp,Negotiation.hpp}`,
  `testing/PduExamples.hpp`, `NativeSession.{hpp,cpp}`, `client/NativeClient.{hpp,cpp}`,
  `src/contour/remote/NativeController.*`, `src/contour/config/Actions.hpp`
  (`ExtensionAction{extension, action}`), `src/contour/command/` (new `ExtensionCommandSource`),
  `IExtensionPresence` (actions half), `docs/internals/vthost.md`.
- **Produces:** capability `ExtensionPresence = 30`; PDU `ExtensionPresence` (tag 60, server→client:
  per extension, its action rows); PDU `InvokeExtensionAction` (tag 61, client→server: extension name
  and action id, **no arguments**); `actions-take()` returning each action with its `origin` (the
  invoking connection). Re-check that 60, 61 and 30 are still free when the task starts.
- **Tests:** both PDUs round-trip (`Pdu_test.cpp:247` picks them up from `PduExamples`); a client
  without the capability is never sent `ExtensionPresence`; the palette lists one row per declared
  action of each present extension and none without the capability; an invocation for an unknown
  extension or action is dropped; the YAML `ExtensionAction: { extension: orchestrator, action: open }`
  binds and round-trips. Follow the seven-step recipe traced from `SetWorkspaceLabel`/`WorkspaceMeta`.
- **Docs:** the `Ctrl+Shift+A` snippet (D2).

### Task 2.5: Chrome

- **Files:** the PDU body that describes a session to a window (append a field), `ScreenMirror`/window
  models, `src/contour/qml/{TabItem.qml,PaneHeader.qml}`, `contour_gui_test`. Follow `.agent/rules/ui-ux.md`.
- **Produces:** the ⬢ marker on a served tab and the header strip naming the extension and its version,
  drawn outside the grid.
- **Tests:** offscreen snapshots with and without the attribute.

### Task 2.6: The dashboard view, on `core::tui`

- **Files:** new `extensions/orchestrator/view/{Dashboard,Theme,Keys}.{hpp,cpp}` and `_test.cpp`
  (library `orchestrator_view`, linked into `orchestrator_test` natively and into the guest); the
  guest's CMake (link the headless TUI built for wasm).
- **Produces:** a view built from core-cpp's pieces, not a renderer of our own: `List` /
  `TreeTableView` for the three lists, a detail `Component`, `StatusBar` for the key hints,
  `KeyBindings` for navigation, `VtParser` for input, the headless frame driver from U.2 for focus,
  diff and bytes out. Interface:

```cpp
class Dashboard
{
  public:
    Dashboard(Engine const& engine, core::tui::Theme theme, Clock const& clock);
    /// Feeds input bytes from the tab; returns what the user asked for.
    [[nodiscard]] std::vector<UiAction> input(std::string_view bytes);
    /// Bytes that bring a tab of @p size up to date with the engine's state.
    [[nodiscard]] std::string frame(TabSize size);
};
```

  `UiAction` is a variant (`Refresh`, `OpenAgentTab{run}`, …; dispatch arrives in 3.3). The clock is
  the injected one (U.4). The bare-ESC timeout that `VtParser::timeout()` needs is driven from the
  guest's `wake`.
- **Tests (native, Catch2):** golden screens through `core::tui::test::canvasToString` at 80×24 and
  120×40 for: empty, the fixture lists, a problem line, a selected row, a title containing an escape
  sequence (must appear inert), a title of wide characters at the column edge; an input table for
  arrows, `j`/`k`, `/`, `esc`, and an escape sequence split across two `input` calls; a second
  `frame` with nothing changed emits no cell writes.
- **Gate (R2):** in the real guest on WAMR, time a full `frame` at 120×40 and an incremental one.
  Record both in the spec. If a full repaint does not fit the daemon world's deadline, the options are
  the deadline itself (a daemon lane does not render terminal frames) or painting in slices across
  wakes — decide with numbers, in the spec.

### Task 2.7: Opening the tab

- The `open` action calls `tab-open` with the action's origin, or `tab-reveal` if the tab exists
  (reveal is wired in 3.4; until then a second invocation is a no-op). `vthost_test` end to end with the
  real guest.

### Phase 2 gate

`MockTerm` snapshot of the dashboard through the daemon; live upgrade with the tab open succeeds; the
refusal tests pass; the R2 render numbers are recorded; `ctest -L daemon`, the GUI suite and tsan are green.

---

# Phase 3 — Dispatch, agent tabs, cleanup

Usable result: the dispatch form starts an agent in a fresh worktree; closing its tab cleans up.

### Task 3.0: Expand this phase to step level.

Read `src/vthost/RerunPolicy.hpp:79-104` and how the handover carries a session's child process
(`handoverChild`) before writing 3.1.

### Task 3.1: Pane templates and ownership

- **Files:** new `src/vtextension/IPaneService.hpp`; new `src/vthost/{PaneOwnership,OwnedPanes}.{hpp,cpp}`
  (`vthost`-owned interface plus implementation); the adapter in `DaemonExtensions`;
  `src/vthost/RerunPolicy.*`; `src/vthost/handover/*`; tests.
- **Produces:** `pane-open(template, args, cwd, title, tag, near)` → `SessionHost::createTab` with a
  `commandOverride` built from the declared template, its arguments checked by kind, its declared
  environment, a `cwd` inside the template's root, in the workspace of the served tab `near`,
  **without taking focus**; the tab is labelled with the sanitised
  title. An ownership registry `pane UUID → {extension, tag}` persisted beside the store, pruned when a
  pane closes, carried across the handover. `panes()` returns owned panes with tag, the integration
  program status and
  `enum class ProgramLiveness : uint8_t { Gone, Alive }` — `Alive` only while the process the host
  spawned from the template is still the session's process (it crosses a live upgrade with the
  session; a cold restart's fresh shell is `Gone`). Owned panes are exempt from
  `daemon.restore.rerun_programs`. `pane-close` on an owned pane only.
- **Tests:** the spawned `ExecInfo` has exactly the template's argv and environment; an argument
  failing its kind and a `cwd` outside the roots are refused; a foreign pane is invisible to `panes()`
  and refused by `pane-close`; ownership and `Alive` survive a simulated upgrade; a simulated cold
  restart reports `Gone` and does not re-type the program even when `rerun_programs` lists it; the
  registry file is rewritten atomically.

### Task 3.1b: `file-read`

- **Files:** new `src/vtextension/IFileReader.hpp` and implementation, the import in `DaemonWorld.hpp`,
  tests. Needed here, not in phase 4: the removal checks read the worktree's `.git` file (§6.3).
- **Produces:** `file-read(root, path)` — only for a root the manifest declares readable; opened
  beneath the root with no symlink followed out of it (`openat2(RESOLVE_BENEATH)` on Linux, a
  component-wise `O_NOFOLLOW` walk elsewhere), with `O_NONBLOCK` and an `fstat` check that it is a
  regular file, ≤ 64 KiB. The filesystem is reached through an injected interface.
- **Tests:** `..`, an absolute path, a symlinked file, a symlinked directory component, a FIFO (must
  return at once, not block the daemon) and a root not declared readable are refused; a regular file
  is read; an oversized file is cut at the cap with a flag.

### Task 3.2: Pane hints

- **Produces:** the `Panes` hint when an owned pane closes or its `ProgramStatusChangedSignal` fires
  (`SessionStreamEvents::sessionSignal`, `SessionHost.hpp:191,212`).
- **Tests:** both sources post the hint; a foreign pane's change does not.

### Task 3.3: The dispatch form and run views

- **Files:** `extensions/orchestrator/view/{DispatchForm,Runs}.{hpp,cpp}`, `core/Engine.cpp`, the guest.
- **Produces:** the form of screen 3 (task, model, note or custom prompt, and the exact prompt,
  checkout and command it will run), the agents list, the kept-worktree and orphan views (path, entries,
  "try removal again" — no shell action, §6.3), the failed-run view with dismiss, built from core-cpp's `SelectDialog`/`InputField`; session UUIDs
  (version 4) from WASI `random_get`, formatted by the guest driver — the engine still takes the UUID as
  an input.
- **Tests:** goldens; the form refuses at the agent limits and shows why; `enter` produces
  the `Dispatch` input with the values shown.

### Task 3.4: Reveal

- **Files:** `proto/*` (PDU `RevealPane`, tag 62, server→client), `NativeSession` (which connection
  sent the input that led to the request — the `origin`), `NativeController`/window controller,
  `pane-reveal(pane, origin)` for owned panes and `tab-reveal(tab, origin)` for served tabs.
- **Tests:** the reveal reaches only the origin connection; revealing a foreign pane or a tab the
  extension does not serve is refused; the GUI activates the tab.

### Task 3.5: End to end

- A `vthost_test` with the real guest, a fake `IProcessRunner` scripted as `gh`/`git`, and a real
  `SessionHost`: dispatch → pane with the right argv, environment and directory → status changes →
  pane closed → removal. One more each for: the cold-restart path; a guest fault between
  `git worktree add` and `pane-open` (the worktree is checked, not leaked, and no agent is orphaned);
  a template program that exits at once (the run shows as failed, not as vanished).

### Phase 3 gate

Phase-0 spike C repeated through the real extension; a kept worktree is never removed without the
user's action; `max_agents` holds under two quick dispatches; all suites and tsan are green.

---

# Phase 4 — Status boards, transcripts, more providers

### Task 4.0: Expand this phase to step level.

### Task 4.1: The inbox

- **Files:** new `src/vtextension/IInbox.hpp`; `src/vthost/` (a `vthost`-owned queue per owned pane,
  ≤ 8 messages, plus adapter); `src/vtcontrol/{Api.hpp,Catalog.hpp}` — a **new** method
  `pane.takeInbox`, `Method<TakeInboxRequest, TakeInboxReply>`, `Exposure::SocketOnly`, with
  `TakeInboxRequest { pane, event }` and `TakeInboxReply { std::optional<std::string> message; }`;
  `Catalog_test`; `src/vthost/control/ControlService.{hpp,cpp}`;
  `src/contour/cli/{AgentReport,AgentIntegrations}.{hpp,cpp}`; `docs/control-api.md`.
- **`pane.reportStatus` is not modified** (C20): its reply is a WIT import of shipped extensions, and a
  new request field would make a daemon that has not restarted yet reject every report — silently,
  since `runAgentReport` swallows errors (`AgentReport.cpp:198-199`). Being socket-only, the new verb
  does not enter `wit/control/world.wit`; assert that with `Wit_test`.
- **Produces:** `inbox-post(pane, name)` for owned panes and declared names only. `runAgentReport`,
  after `pane.reportStatus` and within the same 2 s budget, calls `pane.takeInbox` when the hook's
  event is `PostToolUse` (the event name must be carried out of `AgentIntegration::map`, which today
  funnels `PostToolUse`, `PostToolUseFailure` and `UserPromptSubmit` through one function —
  `AgentIntegrations.cpp:186-191`). It gains an injected output sink and, per integration, a row saying
  how a message is printed: Claude Code →
  `{"hookSpecificOutput":{"hookEventName":"PostToolUse","additionalContext":<text>}}`; Codex → nothing.
  It still exits 0 on every path.
- **Tests:** posting to a foreign pane or an undeclared name is refused; the message is delivered once;
  a request with another event does not consume it; `agent report` writes nothing to the sink when
  there is no message, valid JSON (text escaped) when there is, and nothing for Codex; against a daemon
  that answers "unknown method" the status report still lands and the command exits 0.
- **Accepted, and documented:** delivery is at most once (the core re-posts, §6.4); the reporting pane
  is self-declared, so a local process can take another pane's message (§7).

### Task 4.2: The status-board view

- **Produces:** the view of screen 4, sanitised, fed by the `STATUS.md` reads the engine already
  issues. (`file-read` itself is built in 3.1b, because cleanup needs it.)
- **Tests:** golden screen; a board containing escape sequences appears inert.

### Task 4.3: `contour agent transcript`

- **Files:** `src/contour/cli/AgentTranscript.{hpp,cpp}`, tests, fixtures recorded from real sessions
  with the Claude Code version in a `VERSION` file beside them; the command row `agent-transcript`
  (`contour agent transcript --session {session} --page {page} --json`, `Cwd::Repo`).
- **Produces:** `contour agent transcript --session <uuid> --page <n> [--json]`; finds
  `<session>.jsonl` under `$CLAUDE_CONFIG_DIR` or `~/.claude` in `projects/*/`; emits user and
  assistant text and one line per tool call, sanitised, 200 entries per page, with a count of entries
  it did not understand.
- **Tests:** fixtures; an unknown entry type is counted, not fatal; a truncated last line is skipped;
  a non-UUID `--session` is refused before any file is opened; a 50 MiB log is paged without being
  read whole.

### Task 4.4: The transcript view — goldens.

### Task 4.5: GitLab, then Jira

- **Precondition:** the CLI installed and real output recorded as fixtures. Verify every flag in §6.1's
  GitLab and Jira rows against `--help` first; correct the spec where they differ. No row may take a
  repository or directory as an argument.
- **Produces:** provider rows and mappings; a `JiraIssueSource` alternative in `IssueSource`; the checkout rule for Jira (a new
  branch from the default branch).

### Phase 4 gate

Spike B repeated through the real inbox; every screen of the spec exists; the security tests of §10
all exist and pass; the change summary reports performance, risk and coverage.

---

## Self-review of this plan against the spec

| Spec section | Covered by |
| --- | --- |
| §2 D1–D7 | 0.1, 0.4 (D1, D6), 2.4 (D2), 4.1 (D3, D4), phase order (D5), phase U and 2.6 (D7) |
| §5.8 the core-cpp work package | U.0–U.6 |
| §5.9 building a C++ guest | 1.10, 1.11 |
| §5.1 manifest v2, typed arguments, roots | 0.3, 1.1, 1.4, 1.7 |
| §5.2 daemon world | 1.2, 1.4–1.6, 2.1, 2.4, 3.1, 3.1b, 3.4, 4.1 |
| §5.3 engine, runner, limits, lifecycle, consent, where interfaces live | 1.2, 1.3, 1.7, 1.8, phase-1 gate; Global Constraints |
| §5.4 panes, ownership, liveness, inbox | 3.1, 3.2, 4.1 |
| §5.5 served tabs and their restricted settings | 2.0–2.3, 2.5 |
| §5.6 presence | 1.9 (summary), 2.4 (palette), 3.4 (reveal) |
| §5.7 CLI | 1.8, 4.3 |
| §6.1 sources, limits, back-off | 0.3, 0.4, 0.9 (12, 13), 4.5 |
| §6.2 dispatch, default branch, concurrency | 0.5, 0.6, 0.7, 0.9 (2–4, 14), 3.3 |
| §6.3 lifecycle, removal, orphans, restarts | 0.7, 0.8, 0.9 (5–10, 15), 3.5 |
| §6.4 boards and transcripts | 0.9 (11), 4.1–4.4 |
| §6.5 configuration | 0.5, 1.7 |
| §7 safety | Review Focus; 0.2, 0.3, 0.4, 0.6, 0.7 and 0.9 (the `.git` check), 1.1, 1.2, 1.4, 2.1, 2.2, 3.1, 3.1b, 4.1 |
| §9 risks | R1: 2.0 · R2: phase-1 gate and 2.6 · R3: 0.11 · R4: 4.3 · R5: 4.5 · R6: 0.9 (12) · R7, R8: phase U · R9: 1.10 and the phase-1 gate · R10: the lint (0.1) and Review Focus 8 |
| §10 testing | each task; `bench-headless` at the phase-1 gate |

Known limits of this plan, stated rather than hidden:

- Phases 1–4 are not at step level (see "How this plan is layered").
- Phase 0's C++ is specified by interfaces and test tables, not by implementation; none of it has
  been compiled. The interface blocks are a design, and the compiler has the last word: where a
  declaration order or an include is wrong, fix it and keep the names.
- Phase U is specified from a reading of core-cpp v0.20.0, and belongs to another repository: its
  tasks are proposals until U.0 is agreed there.
- A C++ guest was built and run only for the *status-line* world and only through a harness around
  Contour's WAMR library, never through `CompiledExtension`; and `core::tui` has never been compiled
  for wasm by anyone.
- The GitLab and Jira command lines have never been run.
- `gh pr checkout --branch` inside a detached linked worktree has not been run (it needs a real PR);
  spike C is where it is first exercised. If it fails, steps 3–4 of §6.2 change before phase 3.
- This plan was reviewed twice by an independent pass; the fixes from the second pass have had only
  the author's own consistency check.
- The development machine has no WASI C++ toolchain installed (Prerequisites); the experiment used a
  wasi-sdk unpacked in a scratch directory.
- The C++ rewrite of this plan (revision 4) has not been through an independent review.
