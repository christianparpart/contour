# Agent Orchestrator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a daemon-scoped WebAssembly extension that lists the user's open PRs/MRs and unclaimed
issues, dispatches Claude Code agents into fresh git worktrees in their own tabs, and is driven from a
terminal tab it serves.

**Architecture:** All orchestrator logic lives in one sans-IO Rust crate, `orchestrator-core`, that maps
inputs to effects. Two thin drivers interpret the effects: a native prototype (phase 0, over today's
control socket) and the wasm guest (phase 1 on). The host grows a second extension world,
`terminal-extensions:daemon@0.1.0`, whose services are injected interfaces; its events are payload-less
hints and all data is pulled.

**Tech Stack:** Rust (edition 2021, `wasm32-wasip1`, `wit-bindgen 0.63`, `serde`/`serde_json`), C++23,
Catch2, WAMR fast interpreter, CMake presets (`clang-asan`), `gh` ≥ 2.95, git ≥ 2.40, Claude Code ≥ 2.1.

**Spec:** `docs/superpowers/specs/2026-11-14-agent-orchestrator-design.md`. Section and item numbers
(§5.2, C12, D3, R2) refer to it.

## How this plan is layered

The work is five phases, each usable on its own. **Phase 0 is specified to step level** below and can be
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
- **Every code change lands with tests.** C++: the module's Catch2 `*_test`. Rust: `cargo test`.
- **Never `co_await` inside a Catch2 assertion.** Await into a local, then assert.
- **Build with four jobs:** `cmake --build --preset clang-asan -j4`. Never unrestricted.
- **Formatting:** CI checks with clang-format **22**; the local pre-commit hook runs 23. Format with 22
  (or confirm the diff is identical) before pushing.
- **UI work** (chrome marker, header strip, palette rows) follows `.agent/rules/ui-ux.md`.
- **After each task's code changes** run `/simplify`. Each phase's change summary reports performance
  impact, a risk assessment and coverage.
- **Manifest grammar limits** (version 1 and 2): an argv word cannot contain a space; `{` and `}` may
  appear only as a whole-word `{name}` placeholder.
- **No declared command takes a directory as an argument.** No `git -C`. A command runs in a working
  directory the host validates. Every placeholder has a declared kind (§5.1).
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
| PDU tags 60–62 and capability number 30 are free (last used: 59 and 29) | `src/vthost/proto/Pdu.hpp`, `Negotiation.hpp` |
| Adding `orchestrator-core` with the task-0.1 manifests builds and tests natively; the lock gains one `orchestrator-core` block and a `"serde_derive"` line under `serde`, and no version changes | dry run in a scratch copy of `extensions/` |
| That dry run needed the network: `serde 1.0.229` is in the lock but was never downloaded, because no crate built so far enables it | `cargo test --offline` failed, online succeeded |
| The development machine has **no `wasm32-wasip1` Rust target**, and both local build trees (`out/clang-asan`, `out/clang-release`) are configured with `CONTOUR_EXTENSIONS_BUNDLED=OFF` | `rustc --print target-libdir`, `CMakeCache.txt` |
| `glab`, `acli`, `jira` are not installed on the development machine; their flags are unverified | `command -v` |

## Prerequisites

- **Before phase 0:** network access for one `cargo` run (task 0.1, step 3), `gh` logged in, a running
  Contour daemon (`$CONTOUR_DAEMON` set in the pane) with `contour agent setup` installed, and the
  `contour-workflows` Claude Code plugin the prompt templates name.
- **Before phase 1:** the `wasm32-wasip1` Rust target (`rustup target add wasm32-wasip1`, or the
  distribution's package), then reconfigure with `-DCONTOUR_EXTENSIONS_BUNDLED=ON`. Without it no guest
  is built, `orchestrator_core_test` is not registered with CTest, and `Bundled_test.cpp` has nothing
  to load. Confirm with `cmake --build --preset clang-asan -j4 --target contour_bundled_extensions`.
- **Packagers** who build with `CONTOUR_CARGO_OFFLINE` and a vendored crate directory must re-vendor
  once task 0.1 lands: `serde`, `serde_derive` and `serde_json` become built crates. Say so in the
  release note of the first phase that ships the guest (phase 1).

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
6. **Untrusted text never reaches a prompt, a path or the terminal unsanitised.** Pinned in phase 0.
7. **The 50 ms budget** (R2). Measured in phase 1 before the guest's polling is trusted.

## File Structure

```
extensions/
  Cargo.toml                       modify: members, workspace deps serde / serde_json
  CMakeLists.txt                   modify: cargo-test CTest entry; later the bundled guest
  orchestrator/
    core/                          crate orchestrator-core (rlib) — all logic, no I/O
      src/lib.rs                   module list and re-exports
      src/sanitize.rs              control-character stripping, truncation by character count
      src/model.rs                 ChangeRequest, Issue, CiState, ReviewState, ItemRef
      src/commands.rs              the declared-command and argument-kind tables (single source of truth)
      src/provider.rs              provider rows; src/provider/github.rs JSON mapping
      src/config.rs                the config JSON → Config, with validation
      src/naming.rs                branch, worktree and run names; Jira key check
      src/prompt.rs                prompt building
      src/run.rs                   Run, RunState, the per-run state machine
      src/reconcile.rs             stored runs × panes → decisions; orphan worktrees
      src/core.rs                  Core::step(Input) -> Vec<Effect>
      src/view.rs                  (phase 2) state → screen bytes
      tests/fixtures/              recorded CLI output, one directory per provider
    proto/                         crate orchestrator-proto (native bin) — phase 0 driver, never shipped
  bundled/orchestrator/            (phase 1) the wasm guest: effects → host calls
wit/daemon/world.wit               (phase 1) terminal-extensions:daemon@0.1.0
src/vtpty/                         (phase 1) IProcessRunner: raw spawn with captured output
src/vtextension/                   (phase 1+) Manifest v2, DaemonWorld.hpp, the daemon Engine and
                                   instance, DaemonExtensionRunner, the guest-facing service
                                   interfaces and the declared-command policy
src/vthost/                        (phase 1+) vthost-owned host interfaces, DaemonExtensions adapters,
                                   ownership registry, served sessions, handover, PDUs, refusals
src/vtbackend/screen/              (phase 1) the {DaemonExtension:<name>} status-line item
src/contour/config/                (phase 1) daemon.extensions
src/contour/cli/                   (phase 1) extension verbs; (phase 4) agent transcript, inbox print
src/contour/command/, window/, qml/ (phase 2) ExtensionCommandSource, marker, header strip
```

---

# Phase 0 — Core, prototype and spikes

No host changes. Ends with a tested core, a fixture corpus, a prototype the user has tried, and the
undocumented behaviours answered.

**Build and test commands used by every phase-0 task**

- Tests: `cargo test --locked --manifest-path extensions/Cargo.toml -p orchestrator-core`
- Lint: `cargo clippy --locked --manifest-path extensions/Cargo.toml -p orchestrator-core -- -D warnings`
- Format: `cargo fmt --manifest-path extensions/Cargo.toml -p orchestrator-core -- --check`
- The wasm build must keep working: `cmake --build --preset clang-asan -j4 --target contour_bundled_extensions`
  (only on a tree with `CONTOUR_EXTENSIONS_BUNDLED=ON`; see Prerequisites)

Every `pub` item needs a doc comment (`#![deny(missing_docs)]`); the interface blocks below omit them
for brevity.

### Task 0.1: The crate, and `cargo test` under CTest

**Files:**
- Modify: `extensions/Cargo.toml`
- Create: `extensions/orchestrator/core/Cargo.toml`, `extensions/orchestrator/core/src/lib.rs`
- Modify: `extensions/CMakeLists.txt`
- Modify: `extensions/Cargo.lock` (by cargo, not by hand)

**Interfaces:**
- Produces: crate `orchestrator-core`; CTest test `orchestrator_core_test`.

- [ ] **Step 1: Register the crate and its dependencies.** In `extensions/Cargo.toml` add
  `"orchestrator/core"` to `members`, and to `[workspace.dependencies]`:

```toml
serde = { version = "1.0.229", default-features = false, features = ["derive", "alloc"] }
serde_json = { version = "1.0.154", default-features = false, features = ["alloc"] }
```

- [ ] **Step 2: Create the crate.** `extensions/orchestrator/core/Cargo.toml`:

```toml
[package]
name = "orchestrator-core"
version = "0.1.0"
edition.workspace = true
license.workspace = true
publish.workspace = true
description = "The agent orchestrator's logic: inputs in, effects out, no I/O."

[dependencies]
serde.workspace = true
serde_json.workspace = true
```

  `src/lib.rs`:

```rust
//! The agent orchestrator's logic. Nothing in this crate performs I/O: a driver feeds
//! [`core::Input`]s and carries out the [`core::Effect`]s that come back.
#![forbid(unsafe_code)]
#![deny(missing_docs)]

#[cfg(test)]
mod tests {
    #[test]
    fn the_crate_builds_and_tests_run() {
        assert_eq!(2 + 2, 4);
    }
}
```

  (The `[`core::Input`]` doc links dangle until task 0.9; if rustdoc lints complain, write them as
  plain text until then.)

- [ ] **Step 3: Let cargo extend the lock** (needs the network once; see "Verified facts").
  Run: `cargo test --manifest-path extensions/Cargo.toml -p orchestrator-core` — without `--locked`.
  Then `git diff extensions/Cargo.lock`. Expected: exactly one new
  `[[package]] name = "orchestrator-core"` block and one added `"serde_derive",` line in `serde`'s
  dependencies; **no changed `version =` line**. If any existing version moved, revert the lock and
  pin the workspace dependency to the locked version.

- [ ] **Step 4: Run the test.** Run the phase's test command (with `--locked`). Expected: `1 passed`.

- [ ] **Step 5: Wire it into CTest.** Append to `extensions/CMakeLists.txt`:

```cmake
# The orchestrator's logic is tested natively: it performs no I/O and needs no wasm runtime.
# CONTOUR_WASM_GUESTS is what says cargo was found (cmake/ContourCargo.cmake).
if(CONTOUR_TESTING AND CONTOUR_WASM_GUESTS)
    set(_cargo_test_flags --locked)
    if(CONTOUR_CARGO_OFFLINE)
        list(APPEND _cargo_test_flags --offline)
    endif()
    add_test(NAME orchestrator_core_test
        COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${CONTOUR_CARGO_TARGET_DIR}"
                ${CARGO_EXECUTABLE} test ${_cargo_test_flags}
                --manifest-path "${CONTOUR_CARGO_WORKSPACE}/Cargo.toml" -p orchestrator-core
        WORKING_DIRECTORY "${CONTOUR_CARGO_WORKSPACE}")
    set_tests_properties(orchestrator_core_test PROPERTIES LABELS daemon TIMEOUT 600)
endif()
```

  `CONTOUR_TESTING` is defined at `CMakeLists.txt:153`, `include(ContourCargo)` is at `:418` and
  `add_subdirectory(extensions)` at `:428`, so all four variables are in scope. Read lines 410–430
  first to see which condition guards the `add_subdirectory`.

- [ ] **Step 6: Verify.** On a tree configured with `CONTOUR_EXTENSIONS_BUNDLED=ON` (see
  Prerequisites): `cmake --preset clang-asan && ctest --preset=clang-asan -R orchestrator_core_test`.
  Expected: `100% tests passed`; then the wasm-build command above succeeds unchanged. On a tree with
  it `OFF`, expected: configuration succeeds and the test is simply not registered — say which of the
  two was verified in the commit message.

- [ ] **Step 7: Commit.** `git commit -m "extensions: add the orchestrator-core crate and run its tests under CTest"`

### Task 0.2: Sanitising untrusted text

**Files:** Create `extensions/orchestrator/core/src/sanitize.rs`; modify `src/lib.rs` (`pub mod sanitize;`).

**Interfaces:**
- Produces: `pub fn display_text(input: &str, max_chars: usize) -> String`
  — drops every C0 and C1 control character and DEL, including ESC and CR (a TAB, LF, U+2028 or U+2029
  becomes one space); drops the bidi and direction controls U+061C, U+200E, U+200F, U+202A–U+202E and
  U+2066–U+2069; collapses runs of spaces; trims; truncates to `max_chars` *characters* (not display
  columns — the view measures width separately in phase 2) with a trailing `…` when it cut.
- Produces: `pub fn single_line_note(input: &str) -> Result<String, NoteError>` with
  `pub enum NoteError { StartsWithDash, TooLong }` — `display_text(input, 500)`, refused when the
  result starts with `-` or the *input* exceeded 500 characters.

- [ ] **Step 1: Write the failing table test** in `sanitize.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn display_text_removes_everything_a_terminal_would_interpret() {
        let rows: &[(&str, usize, &str)] = &[
            ("plain title", 40, "plain title"),
            ("\x1b[31mred\x1b[0m", 40, "[31mred[0m"),
            ("title\x1b]0;pwned\x07", 40, "title]0;pwned"),
            ("a\r\nb\tc", 40, "a b c"),
            ("a\u{2028}b\u{2029}c", 40, "a b c"),
            ("  spaced   out  ", 40, "spaced out"),
            ("\u{9b}31m csi", 40, "31m csi"),
            ("abc\u{202e}def", 40, "abcdef"),
            ("abc\u{200f}\u{061c}def", 40, "abcdef"),
            ("abcdefghij", 5, "abcd…"),
            ("ünïcödé", 3, "ün…"),
            ("", 5, ""),
        ];
        for (input, max, expected) in rows {
            assert_eq!(display_text(input, *max), *expected, "input {input:?}");
        }
    }

    #[test]
    fn a_note_may_not_look_like_an_option() {
        assert_eq!(single_line_note("keep the order"), Ok("keep the order".into()));
        assert_eq!(single_line_note("-p evil"), Err(NoteError::StartsWithDash));
        assert_eq!(single_line_note("\x1b-p"), Err(NoteError::StartsWithDash));
        assert_eq!(single_line_note(&"x".repeat(501)), Err(NoteError::TooLong));
    }
}
```

- [ ] **Step 2: Run** the test command. Expected: FAIL, `cannot find function display_text`.
- [ ] **Step 3: Implement** both functions with one pass over `input.chars()`; no regex crate.
- [ ] **Step 4: Run.** Expected: PASS. Run clippy and fmt. Expected: clean.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: sanitise text that came from a forge"`

### Task 0.3: The shared model, argument kinds and the declared-command table

**Files:** Create `src/model.rs`, `src/commands.rs`; modify `src/lib.rs`.

**Interfaces:**
- Produces (`model.rs`):

```rust
pub enum Forge { GitHub, GitLab }
pub enum CiState { None, Pending, Passing, Failing }
pub enum ReviewState { None, Required, Approved, ChangesRequested }
pub enum ItemRef { ChangeRequest { number: u64 }, Issue { number: u64 }, Jira { key: String } }
pub struct ChangeRequest {
    pub number: u64, pub title: String, pub url: String,
    pub head: String, pub base: String, pub is_draft: bool, pub is_fork: bool,
    pub ci: CiState, pub failing_checks: Vec<String>, pub review: ReviewState,
    pub closes_issues: Vec<u64>,
}
pub struct Issue { pub item: ItemRef, pub title: String, pub url: String }
```

  `title` and `failing_checks` hold text already passed through `sanitize::display_text(_, 200)`.
  `head` and `base` are kept only if they pass `check_arg(ArgKind::Ref, _)`; a PR whose `base` does not
  is dropped from the list with a problem line (a hostile branch name must not reach a command).

- Produces (`commands.rs`):

```rust
pub enum Timeout { Short, Long }                 // 30 s, 10 min
pub enum Cwd { Root, Repo, Worktree }            // the worktree root, a configured repository, a run's worktree
pub enum ArgKind { Number, Uuid, Name, Ref, Path, Text }
pub struct CommandDecl {
    pub name: &'static str, pub timeout: Timeout, pub cwd: Cwd, pub argv: &'static [&'static str],
}
pub const ARGS: &[(&str, ArgKind)];
pub const COMMANDS: &[CommandDecl];
pub fn command(name: &str) -> Option<&'static CommandDecl>;
pub fn arg_kind(name: &str) -> Option<ArgKind>;
pub fn check_arg(kind: ArgKind, value: &str) -> bool;   // the host's rule, mirrored; Path is checked
                                                        // syntactically (absolute, no `.`/`..` component)
pub fn manifest_lines() -> Vec<String>;                 // every `arg …` and `command …` line, in table order
```

  Argument kinds (§5.1): `number`, `page` → Number; `session` → Uuid; `remote`, `model` → Name;
  `branch`, `base` → Ref; `path` → Path; `query`, `prompt` → Text.

  Command rows for phase 0 (GitHub and git only). **No row contains `-C`.**

| name | timeout | cwd | argv |
| --- | --- | --- | --- |
| `gh-auth` | Short | Root | `gh auth status` |
| `agent-hooks` | Short | Root | `contour agent setup --dry-run --json` |
| `gh-default-branch` | Short | Repo | `gh repo view --json defaultBranchRef` |
| `gh-prs` | Long | Repo | `gh pr list --author @me --state open --limit 100 --json number,title,url,headRefName,baseRefName,isDraft,isCrossRepository,reviewDecision,statusCheckRollup,closingIssuesReferences` |
| `gh-issues` | Long | Repo | `gh issue list --state open --limit 100 --search {query} --json number,title,url` |
| `gh-checkout` | Long | Worktree | `gh pr checkout {number} --branch {branch}` |
| `git-fetch` | Long | Repo | `git fetch --quiet {remote}` |
| `git-worktree-add` | Long | Repo | `git worktree add --detach {path} {base}` |
| `git-worktree-add-branch` | Long | Repo | `git worktree add -b {branch} {path} {base}` |
| `git-worktree-list` | Short | Repo | `git worktree list --porcelain` |
| `git-status` | Short | Worktree | `git status --porcelain` |
| `git-clean-status-board` | Short | Worktree | `git clean -f -- STATUS.md` |
| `git-worktree-remove` | Long | Repo | `git worktree remove {path}` |

- [ ] **Step 1: Write the failing tests** in `commands.rs`:

```rust
#[test]
fn every_command_obeys_the_manifest_grammar() {
    for decl in COMMANDS {
        assert!(!decl.argv.is_empty(), "{}", decl.name);
        assert!(!decl.argv[0].starts_with('{'), "{}: program is a placeholder", decl.name);
        for word in decl.argv {
            assert!(!word.contains(' '), "{}: {word:?} has a space", decl.name);
            let braces = word.contains('{') || word.contains('}');
            let whole = word.starts_with('{') && word.ends_with('}')
                && word[1..word.len() - 1].chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-');
            assert!(!braces || whole, "{}: {word:?} misuses braces", decl.name);
            if whole {
                assert!(arg_kind(&word[1..word.len() - 1]).is_some(), "{}: {word} has no kind", decl.name);
            }
        }
    }
}

#[test]
fn command_names_are_unique() {
    let mut names: Vec<_> = COMMANDS.iter().map(|d| d.name).collect();
    names.sort_unstable();
    names.dedup();
    assert_eq!(names.len(), COMMANDS.len());
}

#[test]
fn no_command_names_a_directory_or_forces_anything() {
    for decl in COMMANDS {
        if decl.name == "git-clean-status-board" {
            // The one exception: git refuses `clean` without -f. Pinned to one fixed path.
            assert_eq!(decl.argv, ["git", "clean", "-f", "--", "STATUS.md"]);
            continue;
        }
        for word in decl.argv {
            assert!(!matches!(*word, "-C" | "--git-dir" | "--work-tree" | "-R"
                | "--force" | "-f" | "--force-with-lease" | "--hard"),
                "{} carries {word}", decl.name);
        }
    }
}

#[test]
fn argument_kinds_refuse_what_they_must() {
    let rows: &[(ArgKind, &str, bool)] = &[
        (ArgKind::Number, "2334", true), (ArgKind::Number, "", false),
        (ArgKind::Number, "-1", false), (ArgKind::Number, "12345678901", false),
        (ArgKind::Name, "origin", true), (ArgKind::Name, "claude-opus-5-5", true),
        (ArgKind::Name, "ext::sh", false), (ArgKind::Name, "-x", false),
        (ArgKind::Name, ".hidden", false), (ArgKind::Name, "a/b", false),
        (ArgKind::Name, "https://evil", false), (ArgKind::Name, "", false),
        (ArgKind::Ref, "origin/master", true), (ArgKind::Ref, "agent/pr-1-20261113-141207", true),
        (ArgKind::Ref, "a..b", false), (ArgKind::Ref, "a//b", false), (ArgKind::Ref, "-a", false),
        (ArgKind::Ref, "a b", false), (ArgKind::Ref, "a:b", false), (ArgKind::Ref, "a~1", false),
        (ArgKind::Path, "/home/u/worktrees/contour/pr-1", true),
        (ArgKind::Path, "relative/x", false), (ArgKind::Path, "/a/../b", false),
        (ArgKind::Path, "/a/./b", false), (ArgKind::Path, "", false),
        (ArgKind::Uuid, "550e8400-e29b-41d4-a716-446655440000", true),
        (ArgKind::Uuid, "550e8400", false), (ArgKind::Uuid, "550E8400-E29B-41D4-A716-446655440000", false),
        (ArgKind::Text, "assignee:@me", true), (ArgKind::Text, "-label:x", false),
        (ArgKind::Text, "a\0b", false), (ArgKind::Text, "/x:fix-ci https://h/p\nNote: n", true),
    ];
    for (kind, value, ok) in rows { assert_eq!(check_arg(*kind, value), *ok, "{value:?}"); }
    assert!(!check_arg(ArgKind::Text, &"x".repeat(8193)));
}
```

- [ ] **Step 2: Run.** Expected: FAIL to compile. **Step 3: Implement** the tables and functions
  (`Uuid` accepts lower-case canonical form only). **Step 4: Run.** Expected: PASS; clippy, fmt clean.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: the model, typed arguments and the declared-command table"`

### Task 0.4: Record GitHub fixtures and map them

**Files:** Create `src/provider.rs`, `src/provider/github.rs`,
`tests/fixtures/github/{prs-open.json,prs-empty.json,prs-mixed-checks.json,issues-open.json,default-branch.json,auth-failed.stderr,VERSION}`.

**Interfaces:**
- Consumes: `model::*`, `sanitize::display_text`, `commands::check_arg`.
- Produces:

```rust
pub const LIST_LIMIT: usize = 100;                       // matches `--limit 100` in the command table
pub enum ParseError { NotJson, WrongShape { field: &'static str } }
pub struct Listed<T> { pub items: Vec<T>, pub skipped: usize, pub at_limit: bool }
pub fn change_requests(stdout: &str) -> Result<Listed<ChangeRequest>, ParseError>;   // provider::github
pub fn issues(stdout: &str) -> Result<Listed<Issue>, ParseError>;
pub fn default_branch(stdout: &str) -> Result<String, ParseError>;                   // checked as Ref
pub fn unclaimed(issues: Vec<Issue>, mine: &[ChangeRequest]) -> Vec<Issue>;          // provider
```

- [ ] **Step 1: Record fixtures from the real CLI,** from the repository root:

```bash
F=extensions/orchestrator/core/tests/fixtures/github
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
  with gh's "not logged in" text.

- [ ] **Step 2: Write the failing tests.** The CI mapping is a table:

```rust
#[test]
fn ci_state_follows_the_worst_check() {
    // (typename, status, conclusion, state) per check → expected
    let rows: &[(&[(&str, &str, &str, &str)], CiState)] = &[
        (&[], CiState::None),
        (&[("CheckRun", "COMPLETED", "SUCCESS", "")], CiState::Passing),
        (&[("CheckRun", "COMPLETED", "SKIPPED", "")], CiState::Passing),
        (&[("CheckRun", "COMPLETED", "NEUTRAL", "")], CiState::Passing),
        (&[("CheckRun", "IN_PROGRESS", "", "")], CiState::Pending),
        (&[("CheckRun", "QUEUED", "", "")], CiState::Pending),
        (&[("StatusContext", "", "", "PENDING")], CiState::Pending),
        (&[("StatusContext", "", "", "SUCCESS")], CiState::Passing),
        (&[("CheckRun", "COMPLETED", "FAILURE", "")], CiState::Failing),
        (&[("CheckRun", "COMPLETED", "TIMED_OUT", "")], CiState::Failing),
        (&[("CheckRun", "COMPLETED", "CANCELLED", "")], CiState::Failing),
        (&[("CheckRun", "COMPLETED", "ACTION_REQUIRED", "")], CiState::Failing),
        (&[("StatusContext", "", "", "ERROR")], CiState::Failing),
        (&[("CheckRun", "COMPLETED", "SUCCESS", ""), ("CheckRun", "IN_PROGRESS", "", "")], CiState::Pending),
        (&[("CheckRun", "IN_PROGRESS", "", ""), ("CheckRun", "COMPLETED", "FAILURE", "")], CiState::Failing),
        (&[("CheckRun", "COMPLETED", "SOMETHING_NEW", "")], CiState::Pending),
    ];
    for (checks, expected) in rows { assert_eq!(ci_state_of(checks), *expected, "{checks:?}"); }
}
```

  An unknown conclusion maps to `Pending`, never `Passing`: an unknown value must not look green.
  Further tests: `change_requests(include_str!(".../prs-open.json"))` returns one item per element with
  the fixture's numbers and `skipped == 0`; `prs-empty.json` → no items; `prs-mixed-checks.json` → the
  failing check's name is in `failing_checks`, `review == ChangesRequested`, `is_fork`,
  `closes_issues == [2301]`, the title contains no `\u{1b}`, and the PR with the hostile base is not in
  `items` while `skipped == 1`; a list of exactly `LIST_LIMIT` generated entries → `at_limit`;
  `"not json"` → `Err(NotJson)`; `{"a":1}` → `Err(WrongShape{..})`; the first half of `prs-open.json` →
  `Err(NotJson)` (a cut-off reply must be an error, not a short list); `reviewDecision` rows
  `"" → None`, `"REVIEW_REQUIRED" → Required`, `"APPROVED" → Approved`,
  `"CHANGES_REQUESTED" → ChangesRequested`, anything else → `Required`; `default_branch` of the fixture
  is `"master"`, and of `{"defaultBranchRef":{"name":"-x"}}` is `Err(WrongShape{..})`. `unclaimed` drops
  exactly the issues whose number is in some PR's `closes_issues`.

- [ ] **Step 3: Run.** Expected: FAIL. **Step 4: Implement** with `serde` structs that
  `#[serde(default)]` every optional field and ignore unknown ones, so a newer `gh` adding fields does
  not break parsing. **Step 5: Run.** Expected: PASS; clippy and fmt clean.
- [ ] **Step 6: Commit.** `git commit -m "orchestrator-core: map gh's JSON to the shared model"`

### Task 0.5: Configuration

**Files:** Create `src/config.rs`.

**Interfaces:**
- Produces:

```rust
pub enum Task { FixCi, AddressReview, Issue, Custom }
pub enum IssueSource { Forge { query: String } }               // Jira arrives in phase 4
pub struct Repo { pub name: String, pub path: String, pub forge: Forge, pub remote: String,
                  pub base: Option<String>, pub issues: Vec<IssueSource> }
pub struct Config {
    pub worktree_root: String, pub models: Vec<String>,
    pub max_agents: u32, pub max_agents_per_repo: u32,
    pub poll_every_ms: u64, pub status_board_after_ms: u64,     // 0 = never
    pub repos: Vec<Repo>,
    /* private: default models and prompt templates per Task */
}
pub enum ConfigError { NotJson, Missing(&'static str), Invalid { key: &'static str, why: &'static str } }
impl Config {
    pub fn parse(json: &str, home: &str) -> Result<Config, ConfigError>;
    pub fn default_model(&self, task: Task) -> &str;           // Custom → models[0]
    pub fn prompt_template(&self, task: Task) -> Option<&str>; // Custom → None
    pub fn repo(&self, name: &str) -> Option<&Repo>;
}
pub fn parse_duration_ms(text: &str) -> Option<u64>;            // "90s", "2m", "1h", "0"
```

- [ ] **Step 1: Write failing tests** covering: the §6.5 example (as JSON) parses, `~/worktrees` and
  `~/repo/contour` are expanded against `home = "/home/u"`, `repo.name == "contour"` (last path
  component), `remote` defaults to `"origin"`, `base` is `None` when omitted; defaults when omitted
  (`max_agents 4`, `max_agents_per_repo 2`, `poll_every 2m`, `status_board_after 15m`, models
  `[opus, sonnet, haiku]`, the three prompt templates of §6.5); and one rejection row each for: a
  relative `worktree_root`; a `worktree_root` or repo `path` that fails `check_arg(Path, _)` after
  expansion; an empty `repos`; two repos with the same `name`; a repo `name` that fails
  `check_arg(Name, _)`; `max_agents == 0`; `poll_every` below `30s`; a model, `remote` or `base` that
  fails its kind; an issue `query` that fails `check_arg(Text, _)` (so `-label:x …` is refused with
  `why` telling the user to start with another term); a prompt template without `{url}`; a prompt
  template starting with `-`; `forge: bitbucket`; `"2 minutes"` as a duration.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); clippy, fmt.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: parse and validate the configuration"`

### Task 0.6: Names and prompts

**Files:** Create `src/naming.rs`, `src/prompt.rs`.

**Interfaces:**
- Produces:

```rust
pub struct Stamp { pub year: u16, pub month: u8, pub day: u8, pub hour: u8, pub minute: u8, pub second: u8 }
pub fn stamp_from_unix_ms(ms: u64, utc_offset_minutes: i32) -> Stamp;
pub fn is_jira_key(text: &str) -> bool;                        // ^[A-Z][A-Z0-9]+-[0-9]+$
pub fn item_slug(item: &ItemRef) -> String;                    // "pr-2334" | "issue-2301" | "ct-418"
pub fn run_id(item: &ItemRef, stamp: &Stamp) -> String;        // "pr-2334-20261113-150847"
pub fn branch_name(run_id: &str) -> String;                    // "agent/pr-2334-20261113-150847"
pub fn worktree_path(root: &str, repo: &str, run_id: &str) -> String;
pub fn build_prompt(template: &str, url: &str, note: Option<&str>) -> Result<String, PromptError>;
pub fn build_custom_prompt(text: &str, url: &str) -> Result<String, PromptError>;   // "<text>\n<url>"
pub enum PromptError { StartsWithDash, UrlNotHttps, TooLong, Note(NoteError) }
```

- [ ] **Step 1: Write failing tests:** `stamp_from_unix_ms(1794578927000, 60)` is
  2026-11-13 15:08:47 (`date -u -d @1794578927` prints `2026-11-13 14:08:47`; plus one hour — keep that
  command in a comment), and with offset `-480` it is 2026-11-13 06:08:47; a leap day
  (`date -u -d '2028-02-29 12:00:00' +%s` → use the printed value); `is_jira_key` accepts `CT-418`,
  `AB2-1`, rejects `ct-418`, `C-1`, `CT-`, `CT-1a`, `CT-1 `, `-CT-1`; the four name functions give
  exactly the strings in the comments above for that stamp; for a table of item refs, `branch_name`
  passes `check_arg(Ref, _)`, `run_id` passes `check_arg(Name, _)` and `worktree_path` passes
  `check_arg(Path, _)`; `build_prompt("/x:fix-ci {url}", "https://github.com/o/r/pull/1", None) ==
  "/x:fix-ci https://github.com/o/r/pull/1"`; with a note the result is `"<prompt>\nNote: <note>"`; a
  non-`https://` URL is refused; a URL containing a space, a control character or a quote is refused; a
  template or custom text that yields something starting with `-` is refused; every accepted prompt
  passes `check_arg(Text, _)`; custom text is sanitised to one line and capped at 2000 characters.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement** — civil-from-days arithmetic by hand, no date
  crate. **Step 4: Run** (PASS); clippy, fmt.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: names and prompts, built from identifiers only"`

### Task 0.7: The run state machine

**Files:** Create `src/run.rs`.

**Interfaces:**
- Consumes: `model`, `naming`, `config::Task`, `sanitize`.
- Produces:

```rust
pub enum PrepareStep { Fetch, WorktreeAdd, Checkout, OpenPane }
pub enum EndStep { Status, CleanStatusBoard, Remove }
pub enum RunState {
    Preparing(PrepareStep), Running, Blocked, Done,
    Ending(EndStep), Kept { entries: Vec<String> }, Failed { step: PrepareStep, why: String },
}
pub enum AgentStatus { Idle, Working, Done, Blocked, Error }     // contour's ProgramState
pub struct Run {
    pub id: String,                      // also the pane tag and the prefix of every command tag
    pub repo: String, pub repo_path: String, pub remote: String,
    pub base: String,                    // "<remote>/<branch>", a Ref
    pub item: ItemRef, pub url: String, pub task: Task, pub model: String, pub prompt: String,
    pub session: String, pub branch: String, pub worktree: String,
    pub pane: Option<String>, pub state: RunState, pub last_status: AgentStatus,
    pub failure: Option<(PrepareStep, String)>,   // set when preparation failed; carried through Ending
    pub started_ms: u64, pub board_posts: u8, pub board_posted_ms: u64, pub board_seen: bool,
}
pub enum RunInput<'a> {
    CommandOk { stdout: &'a str },
    CommandFailed { why: &'a str },      // non-zero exit, timed out, truncated output, refused start
    CwdMissing,                          // the command could not start: its directory is gone
    Lost,                                // the guest restarted and this step's command or pane is unknown
    PaneOpened { pane: &'a str }, PaneOpenFailed { why: &'a str },
    Status(AgentStatus), PaneGone, RetryRemoval, Dismiss,
}
pub enum RunEffect {
    Command { name: &'static str, args: Vec<(&'static str, String)> },   // cwd comes from the table
    OpenPane, ClosePane, Persist, Forget,
}
impl Run {
    pub fn advance(&mut self, input: RunInput<'_>) -> Vec<RunEffect>;
    pub fn step_tag(&self) -> Option<String>;   // "<id>/fetch", "<id>/status", …; None when no command is expected
    pub fn occupies_a_slot(&self) -> bool;      // Preparing, Running, Blocked, Done, Ending
    pub fn to_json(&self) -> String;  pub fn from_json(text: &str) -> Option<Run>;
}
pub fn state_for(status: AgentStatus) -> RunState;   // Idle|Working → Running, Blocked|Error → Blocked, Done → Done
```

  There is no `Removed` state: a run whose worktree was removed cleanly is forgotten (`Forget`).
  A failed preparation always passes through `Ending`, so nothing is left behind unnoticed; it ends in
  `Failed` (worktree gone) or `Kept` (worktree still there, `failure` shown beside its entries).

- [ ] **Step 1: Write the failing transition table.** One test drives rows of
  `(start state, failure before, input, expected state, expected effects)`. "fail(X)" means
  `failure = Some((X, why))`. `CommandFailed` and `Lost` behave alike unless a row says otherwise.

| From | Input | To | Effects |
| --- | --- | --- | --- |
| `Preparing(Fetch)` | `CommandOk` | `Preparing(WorktreeAdd)` | PR: `git-worktree-add {path, base}`; issue or Jira: `git-worktree-add-branch {branch, path, base}`; Persist |
| `Preparing(Fetch)` | `CommandFailed` / `Lost` / `CwdMissing` | `Failed{Fetch}` | Persist (no worktree exists yet) |
| `Preparing(WorktreeAdd)` | `CommandOk`, PR | `Preparing(Checkout)` | `gh-checkout {number, branch}`, Persist |
| `Preparing(WorktreeAdd)` | `CommandOk`, issue | `Preparing(OpenPane)` | OpenPane, Persist |
| `Preparing(WorktreeAdd)` | `CommandFailed` / `Lost` | `Ending(Status)`, fail(WorktreeAdd) | `git-status`, Persist |
| `Preparing(Checkout)` | `CommandOk` | `Preparing(OpenPane)` | OpenPane, Persist |
| `Preparing(Checkout)` | `CommandFailed` / `Lost` / `CwdMissing` | `Ending(Status)`, fail(Checkout) | `git-status`, Persist |
| `Preparing(OpenPane)` | `PaneOpened` | `Running`, `pane` set | Persist |
| `Preparing(OpenPane)` | `PaneOpenFailed` / `Lost` | `Ending(Status)`, fail(OpenPane) | `git-status`, Persist |
| `Running`/`Blocked`/`Done` | `Status(s)`, `s != last_status` | `state_for(s)`, `last_status = s` | Persist |
| `Running`/`Blocked`/`Done` | `Status(s)`, `s == last_status` | unchanged | none |
| `Running`/`Blocked`/`Done` | `PaneGone` | `Ending(Status)`, `pane` cleared | `git-status`, Persist |
| `Ending(Status)` | `CommandOk{""}` | `Ending(Remove)` | `git-worktree-remove {path}`, Persist |
| `Ending(Status)` | `CommandOk{"?? STATUS.md\n"}` | `Ending(CleanStatusBoard)` | `git-clean-status-board`, Persist |
| `Ending(Status)` | `CommandOk{" M src/a.cpp\n?? STATUS.md\n"}` | `Kept{[" M src/a.cpp","?? STATUS.md"]}` | Persist |
| `Ending(Status)` | `CwdMissing`, no failure | — | Forget (the user removed the worktree by hand) |
| `Ending(Status)` | `CwdMissing`, fail(X) | `Failed{X}` | Persist |
| `Ending(Status)` | `CommandFailed` | `Kept{["status failed: …"]}` | Persist |
| `Ending(CleanStatusBoard)` | `CommandOk` | `Ending(Remove)` | `git-worktree-remove {path}`, Persist |
| `Ending(CleanStatusBoard)` | `CommandFailed` / `CwdMissing` | `Kept{["clean failed: …"]}` | Persist |
| `Ending(Remove)` | `CommandOk`, no failure | — | Forget |
| `Ending(Remove)` | `CommandOk`, fail(X) | `Failed{X}` | Persist |
| `Ending(Remove)` | `CommandFailed` / `CwdMissing` | `Kept{["remove failed: …"]}` | Persist |
| `Ending(any)` | `Lost` | `Ending(Status)` | `git-status`, Persist |
| `Kept` | `RetryRemoval` | `Ending(Status)` | `git-status`, Persist |
| `Failed` | `Dismiss` | — | Forget |
| any state | any input not listed for it | unchanged | none |

  The last row is tested explicitly for: `Status` and `PaneGone` while `Preparing` or `Ending`;
  `Dismiss` on `Kept` (a kept worktree can only be removed, never dismissed); `RetryRemoval` on
  `Running`. Entries in `Kept`, and every `why`, pass through `sanitize::display_text(_, 200)` (file
  names and error text are not ours). Also test: `Run::from_json(run.to_json())` round-trips every
  state with and without `failure`; `from_json` of garbage is `None`; `step_tag` per state;
  `occupies_a_slot` per state; `state_for` for all five statuses; every `Command` effect's argument
  names equal the placeholders of that command's argv, and every value passes its `check_arg`.

- [ ] **Step 2: Run** (FAIL). **Step 3: Implement** as one `match (&self.state, input)`.
  **Step 4: Run** (PASS); clippy, fmt.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: the run lifecycle as a state machine"`

### Task 0.8: Reconciliation and orphans

**Files:** Create `src/reconcile.rs`.

**Interfaces:**
- Produces:

```rust
pub enum Liveness { Gone, Alive }
pub struct PaneFact { pub pane: String, pub tag: String, pub status: AgentStatus, pub program: Liveness }
pub enum Decision { Adopt { run: String, pane: String }, Status { run: String, status: AgentStatus },
                    ClosePane { pane: String }, PaneGone { run: String } }
pub fn reconcile(runs: &[Run], panes: &[PaneFact]) -> Vec<Decision>;

pub struct WorktreeEntry { pub path: String, pub branch: Option<String> }   // branch without "refs/heads/"
pub fn worktree_entries(porcelain: &str) -> Vec<WorktreeEntry>;
pub fn orphans(runs: &[Run], repo: &str, entries: &[WorktreeEntry]) -> Vec<WorktreeEntry>;
```

  `reconcile` never compares paths and never forgets a run (Review Focus 3).

- [ ] **Step 1: Write failing tests,** one per rule:
  1. A `Running`/`Blocked`/`Done` run whose pane is listed, `Alive`, with a status different from
     `last_status` → `Status`. Same status → nothing.
  2. Such a run whose pane is absent → `PaneGone`.
  3. Such a run whose pane is listed with `program == Gone` → `ClosePane` then `PaneGone` (a cold
     restart left a shell behind; close it before the worktree is touched).
  4. A `Preparing(OpenPane)` run and a listed pane whose `tag == run.id` → `Adopt` (the guest restarted
     between `pane-open` and storing the pane).
  5. A pane whose tag names no run → no decision (it stays a visible tab; nothing is closed blindly).
  6. A run in `Preparing(Fetch|WorktreeAdd|Checkout)`, `Ending`, `Kept` or `Failed` → no decision.
  7. **Idempotence:** apply the decisions (`Status` → `advance(Status)`, `PaneGone` →
     `advance(PaneGone)`, `Adopt` → `advance(PaneOpened)`), remove closed panes, reconcile again → empty.
  8. `worktree_entries` on the real shape (`worktree <path>\nHEAD <sha>\nbranch refs/heads/<name>\n\n`,
     a `detached` entry, a path containing a space) returns paths and branches in order.
  9. `orphans` returns exactly the entries whose branch starts with `agent/` and equals no run's
     `branch` in that repository; the main checkout and the user's own worktrees are never orphans; a
     run whose stored `worktree` differs textually from the listed path (symlinked root) is **not**
     reported, because the match is by branch.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); clippy, fmt.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: reconcile runs with panes, and find orphan worktrees"`

### Task 0.9: `Core::step`

**Files:** Create `src/core.rs`.

**Interfaces:**
- Consumes: everything above.
- Produces:

```rust
pub const MAX_IN_FLIGHT: usize = 4;                      // commands running at once; the rest queue
pub const BOARD_REPOST_MS: u64 = 300_000;  pub const BOARD_MAX_POSTS: u8 = 3;
pub const RECONCILE_EVERY_MS: u64 = 30_000;  pub const MAX_BACKOFF_MS: u64 = 1_800_000;

pub enum StartResult { Started, CwdMissing, Refused(String) }
pub struct ProcOutcome { pub start: StartResult, pub exit: Option<i32>, pub timed_out: bool,
                         pub truncated: bool, pub stdout: String, pub stderr: String }
pub enum Input {
    Started { mono_ms: u64, home: String, config_json: String,
              stored: Vec<(String, String)>,              // every store key and value
              in_flight: Vec<String> },                   // tags of commands still running in the host
    Recovered,                                            // the driver has fed every finished result and one Panes
    Wake { mono_ms: u64 },
    ProcFinished { tag: String, outcome: ProcOutcome },
    Panes { panes: Vec<PaneFact> },
    PaneOpened { run: String, pane: String }, PaneOpenFailed { run: String, why: String },
    Dispatch { repo: String, item: ItemRef, url: String, task: Task, model: String,
               note: Option<String>, custom_prompt: Option<String>, session: String,
               mono_ms: u64, wall_ms: u64, utc_offset_minutes: i32 },
    BoardSeen { run: String },
    RetryRemoval { run: String }, Dismiss { run: String }, RefreshNow,
}
pub enum Effect {
    Run { tag: String, command: &'static str, args: Vec<(&'static str, String)>, cwd: String },
    OpenPane { run: String, template: &'static str, args: Vec<(&'static str, String)>, cwd: String, title: String },
    ClosePane { pane: String },
    StoreSet { key: String, value: String }, StoreRemove { key: String },
    PostInbox { pane: String, message: &'static str },
    Summary(String), WakeAt { mono_ms: u64 }, Redraw,
}
pub enum DispatchRefusal { NotConfigured, AgentLimit, RepoLimit, UnknownRepo, UnknownModel,
                           AlreadyRunning, BaseUnknown, Prompt(PromptError) }
pub struct Core { /* private */ }
impl Core {
    pub fn new() -> Core;
    pub fn step(&mut self, input: Input) -> Vec<Effect>;
    pub fn last_refusal(&self) -> Option<&DispatchRefusal>;
    pub fn runs(&self) -> &[Run];
    pub fn change_requests(&self, repo: &str) -> &[ChangeRequest];
    pub fn unclaimed_issues(&self, repo: &str) -> &[Issue];
    pub fn orphans(&self, repo: &str) -> &[WorktreeEntry];
    pub fn problems(&self) -> &[String];        // "gh: not logged in", "agent hooks not installed", …
}
```

  Conventions the tests pin:
  - **Clocks.** `mono_ms` is monotonic and drives every interval. `wall_ms` appears only in `Dispatch`,
    for the names. The core has no randomness: the driver supplies the session UUID.
  - **Tags.** A run's command is tagged `Run::step_tag()`; polls are tagged `poll/<repo>/prs`,
    `poll/<repo>/issues/<n>`, `poll/<repo>/default-branch`, `poll/<repo>/worktrees`, `poll/auth`,
    `poll/hooks`. A `ProcFinished` whose tag is unknown or stale is ignored.
  - **`cwd`** comes from the command's `Cwd` row: `Root` → `worktree_root`, `Repo` → the repository's
    path, `Worktree` → the run's worktree.
  - **Outcome → `RunInput`:** `start == CwdMissing` → `CwdMissing`; `Refused`, a timeout, a non-zero or
    missing exit, or `truncated` → `CommandFailed`; otherwise `CommandOk`.
  - **Store.** `run/<id>` holds `Run::to_json()`. `Persist` → `StoreSet`, `Forget` → `StoreRemove`.
  - **Summary.** `⬢ agents <slots used>/<max> · ✗<failing PRs> · ●<blocked> waiting`, omitting a part
    whose count is 0.
  - **Driver contract.** After every `Wake`, the driver pulls panes and feeds `Panes`. After `Started`,
    it feeds every already-finished result, one `Panes`, then `Recovered`.

- [ ] **Step 1: Write failing scenario tests.** A helper
  `fn finish(core, effects, command, outcome) -> Vec<Effect>` finds the `Effect::Run` with that command
  name and feeds `ProcFinished` with its tag. Scenarios:
  1. **Start.** `Started` with a one-repo config → `poll/auth` and `poll/hooks` are issued first, plus
     a `WakeAt`. No list command is issued until `gh-auth` succeeds; then `gh-default-branch`,
     `gh-prs`, one `gh-issues` per source and `git-worktree-list`. Feeding the `prs-open.json` fixture
     fills `change_requests("contour")` and emits `Summary` and `Redraw`.
  2. **A full PR dispatch:** `Dispatch` → `StoreSet run/…` + `git-fetch {remote: origin}` with
     `cwd == "/home/u/repo/contour"`; then `git-worktree-add` with `path` under
     `/home/u/worktrees/contour/` and `base == "origin/master"`; then `gh-checkout` with
     `cwd == worktree` and `branch == "agent/<run id>"`; then
     `OpenPane { template: "agent", args: [model, session, prompt], cwd: worktree }`; `PaneOpened` →
     state `Running`.
  3. **An issue dispatch** uses the default branch learned in scenario 1 as `base`, issues
     `git-worktree-add-branch`, and never `gh-checkout`. Before `gh-default-branch` has answered and
     with no `base` configured, it is refused with `BaseUnknown`; with `base: main` configured it
     proceeds without waiting.
  4. **Limits.** With `max_agents: 1`, a second `Dispatch` emits nothing and
     `last_refusal() == Some(AgentLimit)`; with `max_agents_per_repo: 1` and two repos, the second
     dispatch in the same repo is `RepoLimit` and one in the other repo proceeds; a second dispatch of
     the same item while the first occupies a slot, or is `Kept`, is `AlreadyRunning`.
  5. **Clean ending.** `Panes` without the run's pane → `git-status`; empty → `git-worktree-remove`;
     ok → `StoreRemove run/…`, and the slot is free again.
  6. **Kept, then cleaned by hand.** Status ` M f` → `Kept`, and no remove command is ever emitted. On
     the next poll the core re-issues `git-status` for the kept run; empty this time → removed.
     `RetryRemoval` does the same without waiting for the poll.
  7. **Removed by hand.** A `Kept` run whose `git-status` comes back `start: CwdMissing` → forgotten.
     A `Running` run is never forgotten while its pane is listed, whatever `git-worktree-list` says.
  8. **Lost hints.** As scenario 5 but the pane's disappearance is only ever seen in the `Panes` that
     follows a later `Wake`: the ending still proceeds. `WakeAt` never lies further ahead than
     `RECONCILE_EVERY_MS` while any run occupies a slot.
  9. **Cold restart.** A new `Core` given `Started { stored: [("run/…", <Running run JSON>)] }`, then
     `Panes` with that pane `program: Gone` → `ClosePane`, then the ending.
  10. **Guest restart mid-dispatch.** For a stored run in each of `Preparing(Fetch)`,
      `Preparing(WorktreeAdd)`, `Preparing(Checkout)`, `Ending(Status)`, `Ending(Remove)`:
      (a) its tag is in `in_flight` → nothing happens until its `ProcFinished` arrives, then it
      continues normally; (b) its result arrives before `Recovered` → continues normally; (c) neither,
      at `Recovered` → `Lost` per the 0.7 table (so a possibly-created worktree is always checked).
      For `Preparing(OpenPane)`: a pane with the run's tag in `Panes` → adopted, `Running`; none at
      `Recovered` → `Lost` → ending. **No path removes a worktree while a pane with the run's tag is
      listed.**
  11. **Status board.** A run `Running` for longer than `status_board_after` gets
      `PostInbox { message: "status-board" }` on the first `Wake` past the threshold; again every
      `BOARD_REPOST_MS` while still `Running`, at most `BOARD_MAX_POSTS` times; never after
      `BoardSeen`; never while `Blocked`/`Done`; never when the setting is `0`. The counters survive
      `to_json`/`from_json`.
  12. **Problems and back-off.** `gh-auth` exiting 1 → `problems()` has a `gh` line, no list command
      is issued, and the next poll is `2 × poll_every` away, doubling to at most `MAX_BACKOFF_MS`;
      a success resets it and `gh-auth` is not run again until a list command fails.
      `agent-hooks` reporting hooks not installed → a second problem line. (Run
      `contour agent setup --dry-run --json` first and record its real output as a fixture in
      `tests/fixtures/contour/`; write the parser against that fixture.)
  13. **Bad lists.** `gh-prs` with `truncated: true`, with non-JSON, or with a non-zero exit → the
      previous list is kept, a problem line is added, nothing panics. A list `at_limit` or with
      `skipped > 0` → shown, plus a problem line saying it is incomplete.
  14. **Concurrency.** `Started` with three repos never has more than `MAX_IN_FLIGHT` commands
      unanswered; the rest are issued as results arrive; a run's commands are queued ahead of polls.
  15. **Orphans.** A `git-worktree-list` result naming an `agent/…` branch no run owns →
      `orphans("contour")` has it and `problems()` mentions it.
  16. **Every effect is well-formed:** a helper asserts, for every `Effect::Run` and `Effect::OpenPane`
      produced across all scenarios, that the command or template is declared, that the argument names
      equal its placeholders, that every value passes `check_arg` for its kind, and that `cwd` passes
      `check_arg(Path, _)`.
  17. **A bad configuration** (`Started` with JSON that fails `Config::parse`) → one problem line, no
      commands, and every `Dispatch` is refused with `NotConfigured`.
- [ ] **Step 2: Run** (FAIL). **Step 3: Implement.** **Step 4: Run** (PASS); clippy, fmt.
- [ ] **Step 5: Commit.** `git commit -m "orchestrator-core: step inputs into effects"`

### Task 0.10: The prototype driver

**Files:** Create `extensions/orchestrator/proto/Cargo.toml`, `extensions/orchestrator/proto/src/main.rs`;
modify `extensions/Cargo.toml` (member `"orchestrator/proto"`).

**Interfaces:**
- Consumes: `orchestrator_core::core::{Core, Input, Effect}`, `commands::{command, check_arg, arg_kind}`.
- Produces: a native binary, run by hand with
  `cargo run --manifest-path extensions/Cargo.toml -p orchestrator-proto -- <config.json>`. It is not
  built by CMake, not installed, and not part of any package.

  The driver is deliberately dumb — it is the model for the wasm guest's effect loop:
  - `Effect::Run` → check every argument with `check_arg`, then `std::process::Command` with the
    declared argv, placeholders substituted as whole arguments, `current_dir(cwd)`, the environment
    `GIT_TERMINAL_PROMPT=0` and `GIT_ALLOW_PROTOCOL=https:ssh:git` added, stdout and stderr captured
    (cap 1 MiB each, set `truncated`), a timeout thread that kills on expiry. A `cwd` that does not
    exist → `StartResult::CwdMissing` without spawning. The result is fed back as `ProcFinished`.
  - `Effect::OpenPane` → `contour api call tab.create` with `cwd`, `label: <title>`, the focus value
    that keeps focus where it is, and
    `"command": ["env","GIT_CONFIG_COUNT=1","GIT_CONFIG_KEY_0=push.default","GIT_CONFIG_VALUE_0=upstream","claude","--model",<model>,"--session-id",<session>,<prompt>]`;
    the reply's `pane` is fed back as `PaneOpened`. (`env` stands in for the host's `pane-env`, which
    does not exist yet. Run `contour api describe tab.create` first for the exact request and the
    spelling of the focus values.)
  - `Effect::ClosePane` → `contour api call pane.close`.
  - Panes → `contour api call workspace.list`, flattened to `PaneFact`s for the panes the driver
    opened (it keeps its own pane → tag map in the store directory); `program` is `Alive` while the
    pane is listed. A cold restart is out of the prototype's reach — say so in `--help`.
  - Store → one file per key under `$XDG_STATE_HOME/contour/orchestrator-proto/`.
  - `PostInbox` → printed as "would post"; `BoardSeen` when `<worktree>/STATUS.md` exists;
    `Summary`/`Redraw` → reprint a plain list.
  - A line-mode prompt: `list`, `dispatch <index> <fix-ci|review|issue> [model] [note…]`, `runs`,
    `retry <run>`, `dismiss <run>`, `refresh`, `quit`. A background thread sends `Wake` every second
    and `Panes` after it. `Started` is followed by one `Panes` and `Recovered` (`in_flight` is empty:
    the prototype's processes die with it).
  - Session UUIDs from `/proc/sys/kernel/random/uuid`, falling back to `uuidgen`.

- [ ] **Step 1: Write the driver** (≈300 lines; no tests of its own — it has no logic the core does not
  own, and anything that turns out to need a test is moved into the core).
- [ ] **Step 2: Check it does not disturb the wasm build:** the bundled-extensions build command (on a
  tree that builds guests). Expected: succeeds; `orchestrator-proto` is not compiled (CMake builds per
  package with `-p`).
- [ ] **Step 3: Try it against a real daemon** with a config naming one repository. Expected: `list`
  shows the user's open PRs; `dispatch` on a PR with failing CI opens a tab running `claude` in a new
  worktree under `worktree_root` without stealing focus; closing that tab leads to `runs` no longer
  showing it, and `git worktree list` no longer naming it.
- [ ] **Step 4: Commit.** `git commit -m "extensions: a prototype driver for the orchestrator core"`

### Task 0.11: Spikes

Each spike ends by updating the spec's "Facts about Claude Code" paragraph (§3) — moving the item from
*not documented* to *measured, with the version* — in the same commit as any fixture.

- [ ] **Spike A — a slash command as the initial prompt.** In a scratch repository run
  `claude --model haiku --session-id "$(uuidgen)" "/contour-workflows:sloc"` in a daemon pane.
  Expected: the skill runs without further input. If it does not, change the default prompt templates
  in `config.rs` to plain language naming the skill ("Use the contour-workflows:fix-ci skill on {url}")
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
  fixture under `tests/fixtures/contour/`. Also record whether Claude Code shows its folder-trust
  prompt in a never-seen worktree path, and what status the pane reports while it does — every
  dispatch creates a new path, so if it asks every time, raise it with the user before phase 3.
- [ ] **Commit.** `git commit -m "docs: record what the orchestrator spikes measured"`

### Phase 0 gate

- `cargo test`, clippy and fmt are clean; `orchestrator_core_test` passes under CTest on a tree that
  builds guests.
- The four spikes are answered in the spec.
- The user has dispatched at least one real PR with the prototype and confirmed the workflow is the one
  they want. **Anything they want changed is changed in the spec first**, then in the core.

---

# Phase 1 — The daemon world

Usable result: a `{DaemonExtension:orchestrator}` status-line segment showing the user's PR counts,
polled by a guest that runs with no window attached.

### Task 1.0: Expand this phase to step level

Read, at minimum: `src/vtextension/{Manifest,CompiledExtension,Engine,Runner,Instance,HostBindings,StatuslineWorld,ExtensionLimits,EventQueue,CommandPolicy,ExtensionComposition,ExtensionConsent}.{hpp,cpp}`,
`src/vtextension/wasm/{WasmRuntime.hpp,wamr/WamrRuntime.hpp,wamr/WamrInstance.cpp}`,
`src/vtextension/testing/{Harness,RunnerHarness,TestModules}.hpp`,
`src/vthost/{DaemonExtensions,ExtensionHosting,CommandRunner,StdioChild,WorkspaceGit}.hpp`,
`src/vtpty/CommandCapture.hpp`, `src/vtbackend/screen/{StatusLineBuilder,StatusLineExtensions}.hpp`,
`src/vtbackend/screen/Terminal.cpp` around `statusLineExtensionText`,
`src/vthost/client/ScreenMirror.hpp:323-326`, `src/contour/cli/ContourApp.cpp:1035-1090,1386-1500`,
`docs/extensions.md`. Write the steps for tasks 1.1–1.10 into this file, each with its failing test
first, and have the result reviewed before coding.

### Task 1.1: Manifest version 2

- **Files:** `src/vtextension/Manifest.{hpp,cpp}`, `Manifest_test.cpp`, `extensions/manifest/src/lib.rs`,
  `docs/extensions.md`.
- **Produces:** `SupportedVersions = {1, 2}`; `Manifest` gains
  `enum class ManifestPlacement : uint8_t { StatusLine, Daemon }`, `std::vector<DeclaredRoot> roots`
  (label + config pointer), `std::vector<DeclaredArgument> arguments` (name +
  `enum class ArgumentKind : uint8_t { Number, Uuid, Name, Ref, Path, Text }`), command environment,
  `std::vector<DeclaredPane> panes` (argv template + environment),
  `std::vector<DeclaredInboxMessage> inbox`, `std::vector<std::string> servedSchemes`,
  `std::vector<DeclaredAction> actions`; `DeclaredCommand` gains
  `enum class CommandTimeout : uint8_t { Short, Long }`. A free function
  `[[nodiscard]] bool argumentAccepts(ArgumentKind, std::string_view)` — one table, the C++ twin of
  `check_arg`.
- **Tests (table):** every §5.1 line kind parses; each is rejected under `version = 1`; a version-1
  manifest parses byte-for-byte as before and keeps its digest; duplicates, a placeholder with no
  `arg` line, a `pane-env` for an undeclared pane, a `root` whose pointer is malformed, an `inbox`
  text with a control character, an `action` id outside `[a-z0-9-]`, a `command-env` key outside
  `[A-Z0-9_]`, and `placement daemon` together with a `control` line are each `ManifestInvalid`.
  **`argumentAccepts` runs the same rows as `argument_kinds_refuse_what_they_must` in task 0.3** — copy
  the table; the two implementations must not drift.

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
- **Tests:** the existing suite unchanged; a daemon test guest loads on the daemon engine; the two
  mismatches are refused; a daemon module is refused by the status-line engine and the reverse; the WIT
  file matches the codec rows (model: the existing `Wit_test.cpp`); the backend contract suite passes
  for a second runtime in one process.
- **Watch:** the `static_assert(StatuslineHostFunctions.size() == 4)`; the stale "five" in
  `WasmRuntime.hpp:120` (the code says 17) — fix the comment while there; whether two WAMR runtimes can
  coexist in one process (if not, this task instead makes the one table a union and the limits
  per-instance — decide in 1.0 by reading `wamr/WamrBackend.cpp`).

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
  with `argumentAccepts`, resolves the working directory and refuses one outside every root (after
  normalisation **and** symlink resolution, through an injected filesystem interface), applies the
  class timeout from a constructor-supplied `ProcessTimeouts { short, long }`, adds the manifest's
  command environment, enforces at most 8 running and 32 untaken results, drops a result once its last
  chunk is taken, and posts the `Procs` hint.
  `enum class ProcessError : uint8_t { NotDeclared, Pending, Denied, ArgumentRejected, CwdOutsideRoots, CwdMissing, TooMany, Unsupported }`.
- **Tests:** raw — exit codes 0 and 3; stderr captured; a 2 MiB writer is truncated at the cap and
  reaped; a sleeper is killed when the manual clock passes the timeout; a child that ignores SIGTERM is
  killed after the grace period; destruction with a child running kills and reaps it. Policy (against a
  fake runner) — an argument failing its kind, a `cwd` outside every root, a `cwd` that is a symlink
  leaving a root, an undeclared command, a ninth concurrent start; paging returns contiguous 64 KiB
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
  absent; a daemon whose only extension is daemon-scoped still draws the indicator row (the mirror
  shows the host's row only "when the host has status-line extensions" — make that condition include
  this case, and pin it); `vtbackend`'s layering holds (the provider is an interface in `screen/`).
- **Watch:** in a GUI-hosted (non-daemon) session the item resolves to nothing; say so in the docs.

### Task 1.10: The guest

- **Files:** new `extensions/bundled/orchestrator/{Cargo.toml,src/lib.rs}`; `extensions/Cargo.toml`;
  `extensions/CMakeLists.txt` (add to `_bundled_extensions`); `src/vtextension/Bundled_test.cpp`.
- **Produces:** the wasm guest: `load` builds `Core`, reads every store key, feeds `Started` with the
  running tags from `procs()`, then every finished result, one `Panes`, and `Recovered`; `update` maps
  hints to pulls and inputs; effects map to host calls exactly as the prototype maps them to
  processes. Timers are a guest-side list over the single `wake`.
- **Tests:** a Rust test asserts the declared manifest's `arg` and `command` lines equal
  `commands::manifest_lines()`; `Bundled_test.cpp` loads the real module against fakes, answers
  `gh-auth` and `gh-prs` with the phase-0 fixtures and expects the summary; a fault injected after
  `proc-start` proves the restarted guest picks the result up by tag.

### Phase 1 gate

- **R2 measurement.** Inside the real guest on WAMR, time `update` handling a `gh-prs` result built
  from the largest recorded fixture scaled to 100 PRs (≈ 1 MiB — which is also the stream cap, so
  measure whether a full list fits at all). Record the numbers in the spec. If it exceeds the daemon
  world's deadline or the cap: replace `--json …` with a brace-free, space-free `--jq` filter that
  emits arrays — for example
  `[.[]|[.number,.title,.url,.headRefName,.baseRefName,.isDraft,.isCrossRepository,.reviewDecision,[.closingIssuesReferences[].number],[.statusCheckRollup[]|[.__typename,.name,.status,.conclusion,.state]]]]`
  — and re-record the fixtures; or split parsing across wakes. Do not simply raise the deadline past
  what a daemon lane can justify without saying so in the spec.
- `bench-headless` with the orchestrator loaded and polling: no throughput regression.
- `ctest --preset=clang-asan` and `ctest --preset=clang-tsan` (the runner crosses threads) pass; a
  `CONTOUR_EXTENSIONS=OFF` build passes.
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

### Task 2.1: Served sessions in the daemon

- **Files:** `src/vthost/SessionHost.{hpp,cpp}`, `Daemon.cpp`, new `src/vthost/ServedSessions.{hpp,cpp}`
  (a `vthost`-owned interface plus implementation), new `src/vtextension/IServedTabs.hpp` (what the
  guest's imports call), the adapter in `DaemonExtensions`, tests.
- **Produces:** a way for `SessionSpawnRequest` to ask for a served scheme; the session's device is a
  host-owned `ChannelPty`; input from clients is buffered per tab (64 KiB; overflow sets a flag and
  drops) and posts the `Tabs` hint; `tab-open(scheme, origin)`, `tab-close`, `tabs()`, `tab-read`,
  `tab-write` (≤ 64 KiB per call); `servedBy` on the session; **restricted terminal settings** for a
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

### Task 2.6: The dashboard view

- **Files:** `extensions/orchestrator/core/src/view.rs`, the guest.
- **Produces:** `pub fn render(core: &Core, ui: &UiState, cols: u16, rows: u16) -> Vec<u8>` (full
  repaint in VT sequences; synchronized output; alternate screen; display width measured here) and
  `pub fn key(ui: &mut UiState, bytes: &[u8]) -> Vec<UiAction>` for navigation, filter and refresh.
- **Tests:** golden screens at 80×24 and 120×40 for: empty, the fixture lists, a problem line, a
  selected row, a title containing an escape sequence (must appear inert), a title of wide characters
  at the column edge; `key` table for arrows, `j`/`k`, `/`, `esc`, an incomplete escape sequence split
  across two reads.

### Task 2.7: Opening the tab

- The `open` action calls `tab-open` with the action's origin, or `pane-reveal` if the tab exists
  (reveal arrives in 3.4; until then a second invocation is a no-op). `vthost_test` end to end with the
  real guest.

### Phase 2 gate

`MockTerm` snapshot of the dashboard through the daemon; live upgrade with the tab open succeeds; the
refusal tests pass; `ctest -L daemon`, the GUI suite and tsan are green.

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
- **Produces:** `pane-open(template, args, cwd, title, tag)` → `SessionHost::createTab` with a
  `commandOverride` built from the declared template, its arguments checked by kind, its declared
  environment, a `cwd` inside a root, **without taking focus**; the tab is labelled with the sanitised
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

### Task 3.2: Pane hints

- **Produces:** the `Panes` hint when an owned pane closes or its `ProgramStatusChangedSignal` fires
  (`SessionStreamEvents::sessionSignal`, `SessionHost.hpp:191,212`).
- **Tests:** both sources post the hint; a foreign pane's change does not.

### Task 3.3: The dispatch form and run views

- **Files:** `view.rs`, `core.rs`, the guest.
- **Produces:** the form of screen 3 (task, model, note or custom prompt, and the exact prompt,
  checkout and command it will run), the agents list, the kept-worktree and orphan resolution views,
  the failed-run view with dismiss; session UUIDs from WASI `random_get`.
- **Tests:** goldens; the form refuses at the agent limits and shows why; `enter` produces
  `Input::Dispatch` with the values shown.

### Task 3.4: Reveal

- **Files:** `proto/*` (PDU `RevealPane`, tag 62, server→client), `NativeSession` (which connection
  sent the input that led to the request — the `origin`), `NativeController`/window controller,
  `pane-reveal(pane, origin)`.
- **Tests:** the reveal reaches only the origin connection; revealing a foreign pane is refused; the
  GUI activates the tab.

### Task 3.5: End to end

- A `vthost_test` with the real guest, a fake `IProcessRunner` scripted as `gh`/`git`, and a real
  `SessionHost`: dispatch → pane with the right argv, environment and directory → status changes →
  pane closed → removal. One more each for: the cold-restart path; a guest fault between
  `git worktree add` and `pane-open` (the worktree is checked, not leaked, and no agent is orphaned).

### Phase 3 gate

Phase-0 spike C repeated through the real extension; a kept worktree is never removed without the
user's action; `max_agents` holds under two quick dispatches; all suites and tsan are green.

---

# Phase 4 — Status boards, transcripts, more providers

### Task 4.0: Expand this phase to step level.

### Task 4.1: The inbox

- **Files:** new `src/vtextension/IInbox.hpp`; `src/vthost/` (a `vthost`-owned queue per owned pane,
  ≤ 8 messages, plus adapter); `src/vtcontrol/{Api.hpp,Catalog.hpp}` — `ReportStatusRequest`
  (`Api.hpp:648`) gains `event`, and `pane.reportStatus` (`Catalog.hpp:372`) changes from `EmptyReply`
  to a new `ReportStatusReply { std::optional<std::string> inbox; }`; regenerate
  `wit/control/world.wit` with `contour api wit`; `Wit_test`; `Catalog_test`;
  `src/vthost/control/ControlService.cpp:810-838`;
  `src/contour/cli/{AgentReport,AgentIntegrations}.{hpp,cpp}`.
- **Produces:** `inbox-post(pane, name)` for owned panes and declared names only. The agent report
  forwards the hook's event name (`StatusReport` drops `hook_event_name` today, and `PostToolUse`,
  `PostToolUseFailure` and `UserPromptSubmit` all map through one function —
  `AgentIntegrations.cpp:186-191`). The daemon hands out the oldest message only when
  `event == "PostToolUse"`. `runAgentReport` gains an injected output sink and, per integration, a row
  saying how an inbox message is printed: Claude Code →
  `{"hookSpecificOutput":{"hookEventName":"PostToolUse","additionalContext":<text>}}`; Codex → nothing.
  It still exits 0 on every path.
- **Tests:** posting to a foreign pane or an undeclared name is refused; the message is delivered once;
  a non-`PostToolUse` report does not consume it; `agent report` writes nothing to the sink when the
  reply has no inbox, valid JSON (text escaped) when it has, and nothing for Codex; an older client
  that sends no `event` and ignores the reply still works (append-only compatibility).
- **Accepted, and documented:** delivery is at most once (the core re-posts, §6.4); the reporting pane
  is self-declared, so a local process can take another pane's message (§7).

### Task 4.2: `file-read` and the status-board view

- **Produces:** `file-read(root, path)` — opened beneath the root with no symlink followed out of it
  (`openat2(RESOLVE_BENEATH)` on Linux, a component-wise `O_NOFOLLOW` walk elsewhere), ≤ 64 KiB; the
  guest feeds `BoardSeen` when `STATUS.md` reads; the view of screen 4, sanitised.
- **Tests:** `..`, an absolute path, a symlinked file and a symlinked directory component are refused;
  a regular file is read; golden screen.

### Task 4.3: `contour agent transcript`

- **Files:** `src/contour/cli/AgentTranscript.{hpp,cpp}`, tests, fixtures recorded from real sessions
  with the Claude Code version in a `VERSION` file beside them; the command row `agent-transcript`
  (`contour agent transcript --session {session} --page {page} --json`, `Cwd::Root`).
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
- **Produces:** provider rows and mappings; `IssueSource::Jira`; the checkout rule for Jira (a new
  branch from the default branch).

### Phase 4 gate

Spike B repeated through the real inbox; every screen of the spec exists; the security tests of §10
all exist and pass; the change summary reports performance, risk and coverage.

---

## Self-review of this plan against the spec

| Spec section | Covered by |
| --- | --- |
| §2 D1–D6 | 0.1 (D1, D6), 2.4 (D2), 4.1 (D3, D4), phase order (D5) |
| §5.1 manifest v2, typed arguments, roots | 0.3, 1.1, 1.4, 1.7 |
| §5.2 daemon world | 1.2, 1.4–1.6, 2.1, 2.4, 3.1, 3.4, 4.1, 4.2 |
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
| §7 safety | Review Focus; 0.2, 0.3, 0.4, 0.6, 1.4, 2.1, 2.2, 3.1, 4.1, 4.2 |
| §9 risks | R1: 2.0 · R2: phase-1 gate · R3: 0.11 · R4: 4.3 · R5: 4.5 · R6: 0.9 (12) |
| §10 testing | each task; `bench-headless` at the phase-1 gate |

Known limits of this plan, stated rather than hidden:

- Phases 1–4 are not at step level (see "How this plan is layered").
- Phase 0's Rust is specified by interfaces and test tables, not by implementation; nothing in it has
  been compiled except the task-0.1 scaffold.
- Task 1.2 assumes two WAMR runtimes can coexist in one process; 1.0 must confirm it.
- The GitLab and Jira command lines have never been run.
- The wasm guest build could not be exercised on the machine this plan was written on (Prerequisites).
