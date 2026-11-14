# Agent orchestrator — design

Date: 2026-11-14
Status: revision 4, awaiting approval. Supersedes revision 2 (the artifact of 2026-11-13) and revision
3 (which specified a Rust guest with its own renderer). Revision 4 writes the extension in C++23 and
draws its tab with core-cpp's `core::tui` — see D1, D6, D7 and §13.
Reviewed: audited against the code, dry-run for phase 0, and reviewed independently twice; §12 lists
what each review changed.
Plan: `docs/superpowers/plans/2026-11-14-agent-orchestrator.md`

## Intent

One extension per Contour daemon watches the user's repositories, lists their open pull/merge requests
and the issues that have none, and starts Claude Code agents for them — each in a fresh git worktree and
its own tab. The user drives it from a terminal tab the extension serves. It is built on Contour's
WebAssembly extension system and adds what that system lacks. The extension itself is C++23, and its
user interface is built with `core::tui` from core-cpp.

Revision 2 fixed the product decisions. This revision keeps all of them and corrects the engineering,
after an audit of what `src/vtextension`, `src/vthost` and Claude Code actually provide. Revision 2
assumed the host was much further along than it is; §3 lists every correction.

## 1. Product decisions (unchanged from revision 2)

| Decision | Choice |
| --- | --- |
| Scope | One instance per daemon. It keeps running with no window attached. |
| Agents | Interactive `claude` tabs, started from one declared command template. The extension cannot type into any pane. |
| Worktrees | One per run, always fresh, removed when the run ends. A worktree holding the user's changes is kept. Nothing is forced. |
| Status board | Requested after a time threshold, through an inbox the agent's own hook reads. Never typed into the agent's tab. |
| UI | Its own terminal tab, marked by Contour's chrome, accepting input only from a person. |
| Sources | `gh`, `glab`, Jira CLIs. Each is a table row of fixed declared commands. |
| Dispatch | Always confirmed by the user in the dispatch form. Nothing is dispatched automatically. |
| Platforms | Linux and macOS. Windows is out of scope for the first version. |

The six screens of revision 2 (palette, dashboard, dispatch form, status board, transcript, agent tab)
remain the target. Two visible details change: the agent state called "waiting" is Contour's `blocked`
(§3, C10), and the `Ctrl+Shift+A` chord becomes a documented configuration snippet rather than a
default binding (§2, D2).

## 2. Decisions this revision makes — please confirm

Each is a judgment call that departs from revision 2. The plan assumes the recommended option.

- **D1 — JSON in the guest is glaze.** Contour already depends on glaze 9.0.0 (header-only) for its
  control API. It was compiled into a wasm guest with exceptions and RTTI off and run under Contour's
  own WAMR build: it works, adds about 32 KiB, and adds no imports. No new dependency.
- **D2 — No key binding contributed by a manifest in v1.** Contour's bindings are compiled-in rows plus
  the user's YAML; a daemon-supplied default chord would need a third source and collision rules. v1
  ships an `ExtensionAction` action the user binds themselves; the docs give the `Ctrl+Shift+A` snippet.
  Palette rows are still contributed by the daemon.
- **D3 — The inbox rides the existing `contour agent report` hook.** Revision 2 passed a per-run
  `claude --settings <inbox hook>` and added a `contour agent inbox` verb. That lets the *extension*
  choose Claude's settings — including permission mode and hooks that run commands — which defeats the
  "no permission-skipping" rule. Instead: `contour agent setup` already installs a `PostToolUse` hook
  running `contour agent report`. After reporting status, that command asks the daemon — through one
  new control verb — whether the pane has an inbox message, and prints it as `PostToolUse`
  `additionalContext`. No `--settings`, no new hook, no second process per tool call, and no dependence
  on undocumented hook-merging behaviour.
- **D4 — Inbox messages are declared in the manifest**, like commands. The extension posts a message by
  *name*; it cannot put free text into an agent's context through the inbox. The user sees the texts in
  the grant dialog.
- **D5 — Phase order: dashboard before dispatch.** Revision 2's phase 2 promised "dispatch from the
  palette" while also ruling out per-item palette rows, so it had no way to choose an item. The
  read-only dashboard now comes first; dispatch lands on top of it. This also puts the riskiest host
  work (the extension-served tab) early, while changing course is cheap (§9, R1).
- **D6 — The extension is C++23, and its core is sans-IO.** Every guest shipped so far is Rust; this
  is the first C++ one. That was tried before being written down: a C++23 reactor module built with
  wasi-sdk 34 (`std::expected`, `std::format`, ranges, `std::variant`) loads and runs under Contour's
  WAMR build (§13). The orchestrator's logic is one library that maps inputs to effects and never
  performs I/O. It is compiled twice: natively, where Catch2 tests it and the phase-0 prototype drives
  it, and to `wasm32-wasip1`, where the guest drives it. The code follows `AGENT.md` like the rest of
  the tree, plus the limits a guest imposes: **no exceptions** (WAMR here has no exception-handling
  support, so a `throw` cannot even load — which suits `std::expected`), no `<iostream>` (it imports a
  WASI call the host refuses), no threads, no filesystem.
- **D7 — The tab is drawn with `core::tui`.** core-cpp's TUI module has the pieces a guest needs — an
  incremental input decoder (`VtParser`), a cell buffer and canvas, widgets (lists, tree tables,
  dialogs, input fields), key bindings, themes — and they perform no terminal I/O. But it cannot be
  used as it stands: `Screen`, which holds the diffing, focus and overlay logic, requires a `Terminal`
  that owns the tty; core-cpp has no WASI build; it throws in places; and Contour vendors core-cpp
  v0.19.0 *without* `tui`. So this design depends on a **core-cpp work package** (§5.8) that must be
  released and re-vendored before the dashboard can be built. Phases 0 and 1 do not need it.

## 3. What the audit found (corrections to revision 2)

"Today" means `master` at `6612b5048`.

| # | Revision 2 assumed | Today | Consequence |
| --- | --- | --- | --- |
| C1 | Extensions follow `docs/drafts/extension-api.md` (`contour:host/*`, `extension.yml`) | One world ships: `terminal-extensions:statusline@0.1.0`, core wasm on WAMR's fast interpreter, hand-written Canonical ABI. The draft matches nothing shipped. | New work follows the shipped names: a second world, `terminal-extensions:daemon@0.1.0`. |
| C2 | A `daemon` placement is a small addition | `Placement` is `{Segment, TakeOver}`. Every instance is per pane (`Runner` "runs one extension for one pane") and owns a status-line `Surface`. `CompiledExtension::load` requires `load`/`update`/`render`. | A daemon-scoped runner, a world check, and a hosting slot beside `IExtensionHosting::createSession` are all new. |
| C3 | `proc` returns full output and exit code | `run-command` is synchronous, runs through `/bin/sh -c`, returns the **first line of stdout only**, ignores the exit status, discards stderr, and is capped at 5 s. | Async process service is new. No shell: argv is executed directly. |
| C4 | 4 MiB output, delivered in the `proc-finished` event | `maxHostCallBytes` is 64 KiB. The event queue gives each coalescing kind one slot (latest wins, never dropped); events that carry a payload share a 64-entry stack and are dropped when it is full. | Events are payload-less, coalescing hints; results are *pulled* in pages (§5.2). Nothing the orchestrator needs can be dropped. |
| C5 | Timers, store, `fs` exist or are trivial | Only one coalesced `request-frame` wake. No store, no `fs`. A guest fault destroys its memory. | Timers stay guest-side over one wake. Store is new and is the only state that survives a fault. |
| C6 | Guest work fits the callback budget | `callDeadline` is 50 ms; overrunning is a fault; three faults in 60 s disable the extension. The interpreter has no JIT. | Parsing a large `gh` reply may not fit. A measurement gates phase 1 (§9, R2). |
| C7 | Host functions are unconstrained | One host-function table, bound in `Engine::create` and identical for every runtime; 25 entries today against a first-party cap of 32. Memory and stack limits are per runtime. | The daemon world gets its own `Engine`, with its own table and limits. |
| C8 | The manifest grows `extension.yml` fields | The manifest is a line grammar in a custom wasm section, `version = 1` only, unknown lines rejected, argv words cannot contain spaces. | A `version = 2` grammar (§5.1). Version 1 modules are untouched. |
| C9 | Extensions call the control API as `extension:<id>` | A caller is a pane id. Extension calls are dispatched "as its pane". `events.subscribe` is socket-only. | The orchestrator uses **no** control verbs. Everything it needs is a dedicated host function scoped to panes it started. |
| C10 | Agent states are working / waiting / done | `ProgramState {Idle, Working, Done, Blocked, Error}` with `BlockedKind {None, Permission, Question, Auth}`. Reports need `$CONTOUR_DAEMON` and `$CONTOUR_PANE`, so they work in daemon mode only. The reporting pane is self-declared: any process with the socket can report for any pane. | The run model maps these. "Waiting for you" is `Blocked`. Status and inbox are not a security boundary (§7). |
| C11 | Panes can be tracked by label `agent:<run>` | `tab.create`'s label names the tab, not the pane. No pane metadata exists besides one free-form label. `tab.create` cannot set environment variables. | A host-side ownership registry (§5.4). Panes are opened through `SessionHost::createTab`'s `commandOverride`, which can carry environment. |
| C12 | An extension-served pane is `ChannelPty` plumbing | `ChannelPty` exists and `PtyFactory` could return one, but a `SessionSpawnRequest` has no way to ask for it, and a session whose device is not a real PTY makes the daemon's **live upgrade fail** (`HandoverError::UnsupportedSession`). | Served sessions need a way to be requested and a handover representation (§5.5). Highest-risk item. |
| C13 | The daemon can send palette rows and a summary to windows | No PDU or capability exists. Every palette row needs a compiled-in `actions::Action`. When the daemon hosts status-line extensions it draws the status row itself and windows show that row instead of their own template. | Palette rows: PDUs, one capability, one action, one command source. Summary: resolved in the daemon's terminal, no PDU (§5.6). |
| C14 | Opening an agent's tab from the dashboard is free | The control catalog has no "focus this pane" verb; tab switching is client-local. | A reveal request from daemon to the window that pressed the key (§5.6). |
| C15 | `STATUS.md` is excluded "in the worktree" via `.git/info/exclude` | In a linked worktree that path resolves to the **main repository's** exclude file (measured). Writing it also needs an `fs:write` permission revision 2 never listed. | No exclude write. Removal treats a lone untracked `STATUS.md` as clean and deletes it with one declared command (§6.3). |
| C16 | An agent can push from `agent/pr-<n>-…` | With git's default `push.default=simple`, `git push` refuses because the local and upstream branch names differ (measured). | The pane template sets `push.default=upstream` through `GIT_CONFIG_*` environment variables (measured to work). |
| C17 | `contour agent transcript` reads a stable log | Claude Code documents the session JSONL as internal and changing between versions. | The reader is tolerant, counts what it skipped, and is tested against fixtures tagged with the Claude Code version that wrote them. |
| C18 | Config nests under `daemon.extensions` | Extensions are configured per profile; `config` is a flat string map. `DaemonSettings` has no `extensions`. | `daemon.extensions` is new; its `config` subtree is handed to the guest as JSON. |
| C19 | `contour extension grant` exists | No `contour extension …` verb exists. Grants are revoked by editing a file. | `contour extension list|grant|revoke` are new. |
| C20 | The reply to a status report can carry an inbox message (D3) | `pane.reportStatus` is `Method<ReportStatusRequest, EmptyReply>` and is also a WIT import of shipped extensions, so its reply cannot change. Control JSON is read with unknown keys as errors, so a new request field would make an older daemon reject every report — silently, because `runAgentReport` swallows errors. `runAgentReport` has no output sink. | `pane.reportStatus` is left untouched. A new socket-only verb, `pane.takeInbox`, and an injected output sink (§5.4). |
| C21 | A cold restart leaves a plain shell in an agent's pane | True unless the user lists `claude` in `daemon.restore.rerun_programs`, in which case the daemon re-types it. No daemon "boot epoch" exists. When the spawned program exits, the session ends and the pane closes — so a `claude` that fails to start looks exactly like one that finished. | Liveness comes from the session's spawned program; owned panes are exempt from re-run (§5.4); a run that ends before its agent ever reported is shown as failed (§6.3). |
| C22 | A second world is a second host-function table | WAMR's native registry is process-wide, and the code relies on every live runtime providing each host function alike; the world check accepts a whole import module by name. | The daemon world uses its own import-module name, and the world check becomes an exact per-world table (§5.2). |

Facts about Claude Code that are **documented** and relied on: `--session-id <uuid>`, `--model <alias>`
(`opus`, `sonnet`, `haiku`, `fable`), a positional initial prompt, `PostToolUse` `additionalContext`,
hooks inheriting the launching pane's environment, session logs at
`~/.claude/projects/<escaped-cwd>/<session-id>.jsonl`. Facts that are **not documented** and are
checked by a spike before anything depends on them (plan, phase 0): that a positional prompt starting
with `/plugin:skill` runs that skill, and that the model reliably acts on injected context mid-turn.

## 4. Architecture

```
contour daemon
├─ orchestrator.wasm            world terminal-extensions:daemon@0.1.0   (C++23, wasm32-wasip1)
│    orchestrator_core          pure: Input → effects; also built natively for tests
│    view (core::tui)           state → cell buffer → bytes; bytes → input events
│    guest driver               turns effects into host calls
│        │  host calls (pull)          ▲  hints (payload-less events)
│        ▼                             │
├─ DaemonExtensionRunner         own lane; no Surface; restarts the guest after a fault
│    ├─ IProcessService          async argv exec; exit code, stdout, stderr; timeout; cap
│    ├─ IExtensionStore          key/value, survives faults and restarts
│    ├─ IPaneService             open from template · close · list — owned panes only
│    ├─ IInbox                   named messages for owned panes
│    ├─ IServedTabs              bytes to and from the tabs the extension serves
│    └─ IExtensionPresence       summary text · palette rows · action invocations
├─ SessionHost                   agent tabs (real PTYs) and served tabs (ChannelPty)
└─ NativeSession ── PDUs ──► windows: palette rows, summary, chrome marker, reveal
```

Each `I…` is an interface injected into the runner, so `vtextension_test` drives a guest against fakes
and `vthost_test` drives the real services against a fake guest.

**Hints and pulls.** The host's event queue keeps one slot per event kind (latest wins) and may drop
anything else. So the daemon world has only payload-less events — `wake`, `procs`, `panes`, `tabs`,
`actions` — each meaning "something of this kind changed; ask me". The guest then pulls: finished
processes, the owned-pane list, served-tab input, pending actions. A coalesced or repeated hint is
harmless, and the guest also pulls everything on a slow timer, so a lost hint only delays.

**The core is sans-IO.** `orchestrator_core` exposes `Engine::step(Input) -> std::vector<Effect>`. It
holds the provider table, the JSON mapping, the run state machine, reconciliation, naming, prompt
building and sanitising. The view sits beside it: it turns the engine's state into a `core::tui`
cell buffer and input bytes into actions, again with no I/O. Both are ordinary C++ libraries tested
with Catch2 on the build machine, with no wasm and no daemon.

## 5. What gets built in Contour

### 5.1 Manifest, version 2

Same custom section, same line grammar, `version = 2`. New line kinds; version 1 modules keep parsing
as today. The digest still covers the whole section, so a grant stays bound to exactly what was shown.

```
name = orchestrator
version = 2
placement daemon
root worktrees = config:worktree_root
root repos = config:repos/*/path
read worktrees
arg remote = name
arg path = path:worktrees
arg prompt = text
command-env GIT_TERMINAL_PROMPT = 0
command-env GIT_ALLOW_PROTOCOL = https:ssh:git:file
command-env GIT_CONFIG_COUNT = 2
command-env GIT_CONFIG_KEY_0 = core.fsmonitor
command-env GIT_CONFIG_VALUE_0 = false
command-env GIT_CONFIG_KEY_1 = core.hooksPath
command-env GIT_CONFIG_VALUE_1 = /dev/null
command gh-prs long @repos = gh pr list --author @me --state open --limit 100 --json number,title,…
command git-fetch long @repos = git fetch --quiet {remote}
command git-status short @worktrees = git status --porcelain
pane agent @worktrees = claude --model {model} --session-id {session} {prompt}
pane-env agent GIT_CONFIG_COUNT = 2
pane-env agent GIT_CONFIG_KEY_0 = push.default
pane-env agent GIT_CONFIG_VALUE_0 = upstream
pane-env agent GIT_CONFIG_KEY_1 = push.autoSetupRemote
pane-env agent GIT_CONFIG_VALUE_1 = true
inbox status-board = <one line of factual text>
serve orchestrator
action open = Open Agents
```

- `command <name> <short|long> @<root> = argv…` — timeout class: short 30 s, long 10 min. Executed
  directly, never through a shell. **No command takes a directory as an argument**, so `git -C
  {anything}` never appears, and **each command is bound to one root**: the host starts it only in a
  working directory inside that root. Without the binding a guest could run the worktree-only
  `gh pr checkout` in the user's main checkout and switch its branch.
- `arg <name> = <kind>` — every placeholder is typed, and the host checks the value before it starts
  anything. An undeclared placeholder is a manifest error.

  | Kind | Accepts |
  | --- | --- |
  | `number` | 1–10 decimal digits |
  | `uuid` | a canonical UUID |
  | `name` | `[A-Za-z0-9][A-Za-z0-9._-]{0,63}` |
  | `ref` | `[A-Za-z0-9][A-Za-z0-9._/@#+-]{0,199}`, no `..`, no `//`, no `@{` |
  | `path:<root>` | an absolute path that, once normalised and resolved, lies inside that root |
  | `text` | up to 8 KiB, no NUL, not starting with `-` |

  This is what keeps a guest from passing `ext::sh -c …` as a git remote, or a path outside its roots.
- `root <label> = config:<pointer>` — directories taken from the extension's own configuration. The
  pointer is slash-separated keys, with `*` for every element of a list. The grant dialog shows the
  resolved directories.
- `read <root>` — the roots `file-read` may read from. Only `worktrees`: nothing under the user's
  repositories is readable.
- `command-env KEY = value` — a fixed environment added to every declared command. The git entries
  switch off the two configuration keys that would make a plain `git status` run a program (§7).
- `pane <name> @<root> = argv…` and `pane-env <name> KEY = value` — the only programs the extension
  can put in a tab, each in a directory inside its root, with a fixed environment.
- `inbox <name> = text` — D4.
- `serve <scheme>` — the extension serves `<scheme>://` tabs.
- `action <id> = title` — a palette row.

### 5.2 The daemon world

`wit/daemon/world.wit`, package `terminal-extensions:daemon@0.1.0`. The guest exports
`load(config-json, host-info)` and `update(event)`; there is no `render`. Imports, all synchronous and
none blocking longer than a memory copy:

| Import | Purpose |
| --- | --- |
| `log`, `wake(after-ms)`, `monotonic-ms()` | One wake; the guest keeps its own timer list. Intervals use the monotonic clock. |
| `proc-start(command, args, cwd, tag) -> handle` | Starts a declared command in a validated directory. `tag` is the guest's own label. |
| `procs() -> list<proc-info>` · `proc-take(handle, stream, offset) -> chunk` | Running and finished processes with their tags. Results are pulled 64 KiB per call, 1 MiB per stream, with exit code, timed-out and truncated flags. |
| `store-get/set/remove/keys` | Values up to 64 KiB. |
| `pane-open(template, args, cwd, title, tag, near) -> pane` · `pane-close(pane)` · `panes() -> list<pane-info>` | Owned panes only. `near` is a served tab: the new tab opens in its workspace. `pane-info` carries the tag, the program status and whether the spawned program is still the pane's program. |
| `inbox-post(pane, message-name)` | Owned panes only. |
| `tab-open(scheme, origin) -> tab` · `tab-close(tab)` · `tab-reveal(tab, origin)` · `tabs() -> list<tab-info>` · `tab-read(tab) -> bytes` · `tab-write(tab, bytes)` | Served tabs. `origin` places or shows the tab in the window that asked. |
| `actions-take() -> list<action>` · `summary-set(text)` · `pane-reveal(pane, origin)` | Presence. An action carries its id and its `origin` (the invoking connection), never arguments. |
| `file-read(root, path) -> bytes` | Inside a root declared readable; regular files only; no `..`, no symlink leaving the root; never blocks. |

Tags are what make a guest restart safe: the host keeps running processes and panes while the guest is
re-instantiated, and the new instance recognises them by tag instead of assuming they failed.

The daemon world has its own `Engine` (C7): its own host-function table (24 imports) and its own
limits. Its imports live under their own module name and each world is checked against an exact list
of its imports (C22), so a module can never link against the other world's functions.

### 5.3 Runner, limits, lifecycle

`DaemonExtensionRunner` is the daemon-world sibling of `Runner`: no `Surface`, no render, a lane of its
own, instances created from the daemon world's own `Engine`. A fault re-instantiates the guest after the existing back-off; the guest rebuilds its state from
the store in `load`. The daemon world has its own `ExtensionLimits` (deadline and memory chosen by the
phase-1 measurement, R2). Configured under `daemon.extensions.<name>: { path, autostart, config }`.
It starts at daemon start when `autostart: true`, otherwise on the first action or served-tab open.
Consent uses the existing grant store and consent queue: asked in the next attached window, or headless
through `contour extension grant`. One answer covers commands, pane templates, inbox messages, roots
and serving, and the question names the combination.

**Where the interfaces live.** `vtextension` defines the services a guest's imports call
(`IProcessService`, `IExtensionStore`, `IPaneService`, …). `vtextension` cannot see `vthost`, and
`vthost` must still build with extensions switched off, so `SessionHost` exposes its side through
narrow `vthost`-owned interfaces, as `IExtensionHosting` does today, and `DaemonExtensions` — compiled
only when extensions are on — adapts one to the other. Spawning itself is a raw `vtpty::IProcessRunner`
(argv, directory, environment, timeout, with the clock injected); the declared-command policy sits on
top of it in `vtextension`.

### 5.4 Panes, ownership, inbox

`pane-open` creates a tab through `SessionHost::createTab` with `commandOverride` built from the
template, so the agent *is* the pane's program: when `claude` exits, the pane closes. The tab does not
take focus. The host records `pane UUID → {extension, tag}` in an ownership registry persisted beside
the store. Pane UUIDs survive both a live upgrade and a cold restart.

`pane-info.program` says whether the program the host spawned for that pane is still its program. It
is derived from the session's own spawn (which already crosses a live upgrade as the handed-over
child), not from a clock or counter. After a cold restart the pane comes back as a shell, so it reads
`gone` — that is how the guest learns the run ended (program status is not in the snapshot). Owned
panes are exempt from `daemon.restore.rerun_programs`, so the daemon never re-types `claude` into one
(C21). The orchestrator closes that leftover pane before touching the worktree.

Inbox: a bounded queue per owned pane, read through one new control verb, `pane.takeInbox {pane,
event}`, reachable from the socket only. `pane.reportStatus` is not touched (C20). `contour agent
report` calls it after reporting, only for a `PostToolUse` hook, and prints the message in the shape
the reporting integration defines (Claude Code: `PostToolUse` `additionalContext`; Codex: nothing in
v1) through an injected output sink. A daemon that predates the verb answers "unknown method", which
the command ignores, as it ignores every error. Delivery is at most once — a reply lost to the hook's
2 s budget loses the message — so the orchestrator re-posts until it sees the board (§6.4). The texts
are factual ("A status board has been requested for this run: …"), because Claude Code's own guidance
is that imperative injected text can trip its prompt-injection defences.

### 5.5 Served tabs

A served session is a daemon session whose device is a `ChannelPty` owned by the host: keyboard and
mouse bytes from attached clients are buffered per tab (bounded) and pulled by the guest; the guest's
`tab-write` feeds the terminal. Requires a seam so `SessionSpawnRequest` can name a served scheme
instead of a program. The session carries a `servedBy` attribute that:

- reaches windows, which draw the ⬢ tab marker and the header strip in chrome, outside the grid;
- makes `ControlService` refuse `pane.send`, `pane.capture` and `block.read` on that pane — which also
  covers MCP's `pane_type`, since that is built on `pane.send`;
- is captured across a live upgrade as "re-open this scheme", not as a PTY, so the upgrade no longer
  fails; the guest redraws after the upgrade.

The guest draws with ordinary terminal output, so a served session runs with restricted settings: no
clipboard access (OSC 52), no notifications, no hyperlinks, and a title fixed by the host. Without
that, `tab-write` would quietly be a clipboard and notification permission.

### 5.6 Presence in windows

**Summary.** The status-line item `{DaemonExtension:<name>}` is resolved in the daemon's terminal, the
way `{Extension:<name>}` is today, from a provider `SessionHost` injects. Windows already mirror the
row the daemon draws, so the summary needs no PDU. A summary change redraws each session's status row,
rate-limited by the host.

**Palette and reveal.** One capability, `ExtensionPresence`, and three PDUs: `ExtensionPresence`
(server→client: per extension, its action rows), `InvokeExtensionAction` (client→server: extension and
action id, no arguments) and `RevealPane` (server→client, sent to the connection named by the
`origin`). In the GUI: an `ExtensionCommandSource` for the palette and an
`ExtensionAction{extension, action}` in `actions::Action`.

### 5.7 CLI

`contour extension list|grant|revoke` (C19) and `contour agent transcript --session <uuid> --page <n>`
(C17). The transcript verb finds the log by its session UUID under Claude's projects directory and
prints a stable, sanitised, paged format. It is a declared command of the orchestrator, so the
extension never opens `~/.claude` itself.

### 5.8 The core-cpp work package (upstream)

Done in core-cpp, released, then re-vendored — `vendor/core-cpp` is never edited here. What the guest
needs, from a reading of core-cpp v0.20.0:

1. **A headless target.** Split out of `tui` a target holding `VtParser`, `InputEvent`,
   `KeyBindings`, `Cell`, `Buffer`, `Canvas`, `Component`, `Theme`, `Unicode` and the widgets, that
   depends on `base` and `tui_output` only — not `platform`, `async` or `net` — and builds on any
   platform. Today widgets include `Screen.hpp`, which drags in `Terminal`.
2. **`Screen` without a tty.** `Screen` takes a `Terminal&`; it needs only a size and a byte sink
   (the inline viewport alone asks the terminal a question).
3. **An output with no operating system under it.** `TerminalOutput`'s byte sink is already virtual,
   but its default behaviour exists only in POSIX and Windows sources.
4. **No `dynamic_cast`.** `Terminal` detects a headless output by RTTI; make it an explicit property.
5. **No `throw`.** `Buffer::at` throws.
6. **A WASI row in the build.** Only Emscripten is recognised today, and it switches `tui` off.
7. **An injected clock** in `HoverState`, `TreeTableView`, `InputField` and `Screen` — they read
   `steady_clock::now()` directly, which makes screens untestable against time.
8. **The diff as a function**: two buffers in, bytes out (today private to `Screen`).
9. **Width tables.** `tui` measures text with libunicode. Either confirm libunicode builds for
   `wasm32-wasip1`, or let the caller supply the width function (R8).

Acceptance: a core-cpp release whose headless TUI target builds for `wasm32-wasip1` with
`-fno-exceptions -fno-rtti` in core-cpp's own CI. Then Contour re-vendors it with the modules `tui`
and `codec` added (`codec` became mandatory in v0.20.0), which also moves the other vendored modules
from v0.19.0 forward.

### 5.9 Building a C++ guest

- **Toolchain:** clang with the wasm32 target, `wasm-ld`, and a WASI sysroot with libc, libc++ and
  compiler-rt (the wasi-sdk, or the distribution's `wasi-libc`, `wasi-libc++`, `wasi-libc++abi`,
  `wasi-compiler-rt`). The Rust toolchain stays, for the existing guests.
- **Shape:** the guest is cross-compiled as its own CMake sub-build with a `wasm32-wasip1` toolchain
  file, because from phase 2 it links libraries (the headless TUI) that must be built for wasm too.
- **Flags that are not optional:** `-mexec-model=reactor`, `-fno-exceptions`, `-fno-rtti`; no start
  section; the manifest is emitted through the assembler (`.section ".custom_section.…"`) — the
  `section` attribute silently produces nothing.
- **Bindings:** the import and export glue for the daemon world is generated by `wit-bindgen c`
  (0.63.0, the version the Rust guests use) and committed; its names and signatures were checked
  against the host for the status-line world and match.
- **The manifest is generated**, at build time, from the same command table the engine uses, so the
  two cannot drift.
- **Opt-in.** Building the orchestrator is its own option, off by default until phase 4, so a
  distribution without a WASI C++ toolchain keeps building Contour unchanged.

## 6. The orchestrator

### 6.1 Sources

Each provider is a row: its declared commands and a mapping from its JSON to the shared shape
(`ChangeRequest`, `Issue`, `CiState`, `ReviewState`). Forge commands run with the repository as working
directory, so no `-R` slug is needed. Where a checkout has several remotes, `gh` chooses its base
repository by its own rules (`gh repo set-default`), independently of the configured `remote`; the
documentation says so.

| Provider | List change requests | List issues | Check out |
| --- | --- | --- | --- |
| GitHub `gh` (flags verified against gh 2.95) | `gh pr list --author @me --state open --limit 100 --json …` | `gh issue list --state open --limit 100 --search {query} --json …` | `gh pr checkout {number} --branch {branch}` |
| GitLab `glab` (flags **unverified**) | `glab mr list --author @me -F json` | `glab issue list --assignee {assignee} --label {labels} -F json` | `glab mr checkout {number} --branch {branch}` |
| Jira `acli` / `jira-cli` (flags **unverified**) | — | `--jql {jql}` | new branch from the default branch |

"Issues without a PR" is computed: open issues minus those named in `closingIssuesReferences` of the
user's own open PRs. An issue fixed by someone else's PR still shows; that is accepted for v1. A list
that comes back with exactly 100 entries, or truncated at the 1 MiB cap, is shown with a notice saying
it is incomplete. After a failed poll the interval doubles, up to 30 minutes, and `gh auth status` is
re-checked only then — not on every poll. GitLab
and Jira rows are added only once real output has been recorded as fixtures; their flags above come
from revision 2 and have not been run.

### 6.2 Dispatch

1. The user confirms the form. The core checks `max_agents` and `max_agents_per_repo`, and that the
   item has no run already, then stores the run as `Preparing`.
2. In the repository: `git fetch --quiet {remote}`.
3. In the repository: `git worktree add --detach {path} {base}`, where `{path}` is
   `<worktree_root>/<repo>/<item>-<yyyymmdd>-<hhmmss>` and `{base}` is `<remote>/<base branch>`.
4. In the worktree: `gh pr checkout {number} --branch agent/<item>-<yyyymmdd>-<hhmmss>` — or, for an
   issue, step 3 is `worktree add --no-track -b {branch} {path} {base}` and this step is skipped.
   `--no-track` matters: without it the new branch tracks the default branch, and with
   `push.default=upstream` a plain `git push` would push the agent's work **to the default branch**
   (measured). With `--no-track` and `push.autoSetupRemote=true`, `git push` creates a remote branch of
   the same name (measured).
5. `pane-open(agent, {model, session, prompt}, cwd = worktree, tag = run id, near = the dashboard)`.
6. Store the run with its pane. State `Running`.

A PR's base branch comes from the PR. For an issue it is the repository's default branch, read once
per repository with `gh repo view --json defaultBranchRef` (the git-only way,
`symbolic-ref refs/remotes/origin/HEAD`, fails on clones where that ref was never set — measured); an
optional per-repository `base` overrides it.

A failure in 2–5 always goes through the removal checks before the run is shown as failed, so no step
can leave a worktree behind unnoticed. Branch and directory names are built from numbers, Jira keys
(`^[A-Z][A-Z0-9]+-[0-9]+$`) and timestamps only — never from titles. The prompt is the task's template
with `{url}`, plus an optional one-line note; it never contains the item's title or body. Seconds are
in the names so two dispatches within a minute cannot collide (revision 2 used minutes). The core
starts at most four commands at a time and queues the rest.

### 6.3 Lifecycle and cleanup

States: `Preparing → Running ⇄ Blocked ⇄ Done`; any of those `→ Ending` when the pane is gone or its
program is no longer alive; then the run is forgotten (worktree removed) or `Kept`. A preparation
that fails also goes through `Ending` and finishes as `Failed` (nothing left behind) or `Kept`.

- **Ending is decided by reconciliation**, not by one event: on the `panes` hint, on a slow timer and
  at start, the core compares stored runs with `panes()`.
- **Removal:** first the worktree's `.git` file is read and must still say
  `gitdir: <main checkout>/.git/worktrees/<directory name>`; if an agent rewrote it, no git command is
  run there and the worktree is kept (§7). Then `git status --porcelain` in the worktree. Empty →
  `git worktree remove` (no `--force`). Exactly one entry `?? STATUS.md` → `git clean -f -- STATUS.md`,
  then remove. Anything else → `Kept`, shown with its path, its entries and the action "try removal
  again"; a kept worktree is also re-checked on every poll, so cleaning it up by hand is enough.
  (Revision 2 offered "open a shell there"; that would need a second pane template and grant, so v1
  shows the path instead.)
- **An agent that never started.** When the pane's program exits the pane closes, so a `claude` that
  failed to start is indistinguishable from one that finished. A run whose pane goes away before its
  agent ever reported a status is therefore shown as failed ("the agent exited before reporting")
  after the usual removal checks, instead of vanishing silently. The branch is always kept, so
  commits the agent made are never lost. Ignored files (build output) are deleted with the worktree —
  git does this silently (measured).
- **A worktree the user removed by hand** is noticed when a command cannot start there because the
  directory is gone; only then is the run forgotten. Paths are never compared with
  `git worktree list`: git prints resolved paths, so a symlinked `worktree_root` would make every run
  look removed (measured). A run whose pane is alive is never forgotten.
- **Orphans.** `git worktree list --porcelain` is used for one thing: a worktree on an `agent/…` branch
  that no run names, or any worktree under `worktree_root` that no run names (a run lost before its
  branch existed), is listed as an orphan with its path. `agent/*`
  branches themselves accumulate; pruning them is the user's call and out of scope.
- **Guest restart.** Processes and panes keep running in the host while a faulted guest restarts. The
  new instance reads its runs from the store and matches them to `procs()` and `panes()` by tag. A run
  mid-preparation whose command is neither running nor finished is treated as failed and goes through
  the removal checks; one whose pane exists is adopted.
- **Daemon restart:** the store is the source of truth. After a cold restart the pane's program reads
  `gone`, the run ends, the leftover shell pane is closed, and removal runs.
- **Extension fault or disable:** agents are ordinary tabs and keep running; cleanup resumes when the
  extension is back.

### 6.4 Status boards and transcripts

At `status_board_after` (0 disables) the core posts the `status-board` inbox message, and posts it
again every five minutes, at most three posts in total, until `STATUS.md` exists — inbox delivery is at most
once (§5.4). A `Blocked` or `Done` agent makes no tool calls, so it receives it only if it resumes;
that is fine, since a blocked agent already shows what it waits for. The board is `STATUS.md` in the worktree root,
read through `file-read`. The dashboard strips control characters before drawing it.

Transcripts are read page by page through `contour agent transcript`. The extension can read any
Claude session whose UUID it knows; it only knows the UUIDs it generated. That is weaker than revision
2's "only sessions this extension started" and is stated as such.

### 6.5 Configuration

```yaml
daemon:
  extensions:
    orchestrator:
      autostart: true
      config:
        worktree_root: ~/worktrees
        models: [opus, sonnet, haiku]
        default_model: { fix_ci: sonnet, address_review: opus, issue: opus }
        max_agents: 4
        max_agents_per_repo: 2
        poll_every: 2m
        status_board_after: 15m
        repos:
          - path: ~/repo/contour
            forge: github
            remote: origin            # default
            base: master              # optional; default: the forge's default branch
            issues:
              - { source: forge, query: "assignee:@me" }
        prompts:
          fix_ci:         "/contour-workflows:fix-ci {url}"
          address_review: "/contour-workflows:address-review {url}"
          issue:          "/contour-workflows:work-issue {url}"
```

The `config` subtree is converted to JSON by the host and passed to `load`. A leading `~/` is expanded
by the core using the home directory the host supplies. Commands run with the daemon's environment, so
the daemon must see the user's `PATH` and CLI logins. Model names must be plain aliases or ids
(`opus`, `claude-opus-5-5`); bracketed variants are not accepted in v1. Also: the dashboard's first line reports a failing
`gh auth status` and missing agent hooks (`contour agent setup --dry-run --json`). An issue query may
not start with `-` (the argument rule); write `assignee:@me -label:x`, not `-label:x assignee:@me`.

## 7. Safety

| Granted | Why |
| --- | --- |
| Declared commands (`gh`, `git`, `contour agent …`) | List work, manage worktrees, read transcripts. The dialog says these reach the network as the user. |
| Pane template `agent` | Start `claude` in new tabs with prompts the extension writes. |
| Inbox message `status-board` | Ask a running agent for a status board. |
| Root `worktrees` | Run the worktree commands in, start agents in, and read files from, the worktree root. |
| Root `repos` | Run the repository commands (`gh` lists, `git fetch`, `git worktree …`) in the configured repositories. Nothing there is readable through `file-read`. |
| Serve `orchestrator://` | Draw its own tab. |

- **No typing, no screen reading.** The extension imports no control verbs at all (C9).
- **It cannot choose Claude's settings or flags** (D3). The template has no permission flags; agents run
  in the user's configured permission mode.
- **Untrusted text.** Titles and bodies are sanitised before drawing and never reach a prompt, a branch
  name or a path. A custom prompt or note that starts with `-` is refused.
- **Agents read untrusted PR and issue text with the user's credentials.** That risk is inherent to the
  feature; it is why dispatch is always a confirmed act and `max_agents` bounds the cost.
- **Every argument is typed, every command bound to a root, every directory validated** (§5.1). A
  guest cannot name a path outside its roots, pass a git transport that runs a program, or run a
  worktree command in the user's own checkout.
- **Cleanup runs git in a directory an agent controlled.** An agent that can only edit files could
  otherwise get a program run by the orchestrator's own `git status`: by pointing the worktree's
  `.git` file at a directory with a hostile config (measured: `core.fsmonitor` ran a command). Three
  defences: the `.git` file is verified before any git command runs there; the command environment
  forces `core.fsmonitor=false` and `core.hooksPath=/dev/null`, which outrank any config file; and
  the host enters the validated directory by handle, not by re-resolving its name. **Residual risk:**
  git has other configuration that runs programs (clean filters, for one), and the main repository's
  `.git/config` is a file an agent with broad edit rights can reach. An agent that can run shell
  commands needs none of this. The orchestrator does not make an agent safer than Claude Code's own
  permission mode makes it.
- **The served tab cannot be faked**: the marker is chrome, outside the grid (§5.5). **It can be driven
  only by something that speaks the daemon's native protocol.** `pane.send`, `pane.capture`, MCP's
  `pane_type` and `InvokeExtensionAction` (which carries no arguments) cannot dispatch. But the
  daemon's socket path is in every pane's environment, and a process that attaches to it as a client
  can send keystrokes to any session, the dashboard included — exactly as it can already type into any
  of the user's shells. Dispatch confirmation is therefore a guard against accidents and against
  prompt-injected agents using the ordinary tools, not a boundary against a local process acting as
  the user. `max_agents` bounds the damage either way. Revision 2 claimed more than this.
- **Status and inbox are not authenticated** (C10). A process with the socket can report a status for
  another pane or take its inbox message. The cost is a wrong badge or a missed status-board request.
- **`file-read` follows no symlink out of its root**: an agent controls its worktree and could plant
  `STATUS.md` as a link to a private file.

## 8. Phasing

| Phase | Builds | Usable result |
| --- | --- | --- |
| 0 | `orchestrator_core` (sans-IO, C++) with fixtures; a native prototype driver over today's control socket; four spikes | Try the workflow from a shell. Not shipped. |
| U | The core-cpp work package (§5.8), released and re-vendored. Runs **in parallel** with 0 and 1 | `core::tui` usable from a guest. |
| 1 | Manifest v2, the daemon world and its engine, runner, process service, store, `daemon.extensions`, grant verbs, the summary status-line item, the C++ guest toolchain, the guest | A "my PRs" status-line segment that polls with no window attached. |
| 2 | Served tabs, chrome marker, input refusal, live-upgrade support, palette action, the dashboard view on `core::tui`. **Needs U** | The read-only dashboard tab. |
| 3 | Pane templates, ownership, pane hints, `file-read`, reveal, dispatch form, lifecycle, reconciliation, cleanup | Dispatch, agent tabs, worktree cleanup. |
| 4 | Inbox through `agent report` and `pane.takeInbox`, status-board view, `contour agent transcript`, transcript view; GitLab and Jira rows | Everything in revision 2's screens. |

## 9. Risks

- **R1 — Served tabs and live upgrade (C12).** The deepest change to the daemon. Decision gate at the
  start of phase 2: if representing a served session across a handover is not tractable, fall back to a
  native `contour agents` terminal program running in an ordinary pane and talking to the extension
  through `InvokeExtensionAction`-style PDUs. The fallback loses the chrome marker and the input
  refusal; it would need its own review.
- **R2 — The 50 ms callback budget (C6).** Gate in phase 1: parse the largest recorded `gh pr list`
  fixture inside the real guest on WAMR and measure. Gate again in phase 2: a full repaint and diff of
  a 120×40 screen through `core::tui` in the interpreter. If it does not fit a deadline the daemon world can
  justify, shrink the reply at the source with `gh … --jq`, or parse across several wakes.
- **R3 — Undocumented Claude Code behaviour.** Covered by phase-0 spikes; the fallback for a positional
  slash command that does not run is a plain-language prompt that names the skill.
- **R4 — The session log format (C17)** can change under us. The reader degrades to "N entries not
  understood" instead of failing, and fixtures carry the version that wrote them.
- **R5 — `glab`, `acli`, `jira-cli` are unverified.** They are phase-4 rows gated on recorded fixtures.
- **R7 — The core-cpp work package (§5.8) is on the critical path of the dashboard**, and it is in
  another repository with its own review and release. It starts with phase 0, not after phase 1. If
  it is not released when phase 2 is due, the host half of phases 2 and 3 can proceed; the views
  cannot. Re-vendoring also moves Contour's other core-cpp modules past v0.19.0, which may need
  adaptations in Contour.
- **R8 — libunicode in a guest.** `core::tui` needs character widths from libunicode. Building it for
  wasm is unverified, its tables add size, and a distribution that takes libunicode as a system
  package has no source to cross-compile. Decided inside the work package (item 9).
- **R9 — The C++ guest toolchain** is new to Contour's CI and to packagers (§5.9). The guest's
  libraries need bulk-memory, sign-extension and saturating float conversion in the runtime; Contour's
  bundled WAMR has them, but a *system* WAMR built without them could not load the guest, and no guest
  flag fixes that. The orchestrator stays opt-in until that is settled.
- **R10 — A fault is cheap to cause in C++ without exceptions.** `vector::at`, a bad format string or
  `expected::value()` on an error abort, which the host sees as a guest fault; three in a minute
  disable the extension. The code rules (§10) forbid those calls, and a lint test enforces it.
- **R6 — The daemon's environment** may lack the user's `PATH` or keyring access when started by a
  service manager. Detected and shown; not solved.

## 10. Testing

- **Core and view:** a Catch2 target, `orchestrator_test`, built natively. Table tests over recorded
  CLI output, including errors and truncation; state-machine tests for lost hints, a cold restart, a
  worktree removed by hand, kept worktrees, and both agent limits; golden screens through
  `core::tui`'s own test helpers.
- **Guest portability:** a lint test fails on `throw`, `try`, `.at(`, `.value()`, `<iostream>`,
  `<thread>`, `<filesystem>` and `dynamic_cast` in the orchestrator's sources, so a mistake is caught
  on a machine without the wasm toolchain. From phase 1 the wasm build is the second check, and the
  built module is run against the host's fakes.
- **Host:** `vtextension_test` for manifest v2, the world check, the runner, and each service against
  fakes; `vthost_test` for ownership, served sessions, control refusals and the handover; byte-level
  PDU round-trips in `vthost/proto`.
- **Security rules as tests:** a served pane refuses `pane.send`, `pane.capture`, `block.read`; an
  extension cannot close, list or post to a pane it did not start; `file-read` refuses `..` and an
  escaping symlink; a placeholder value starting with `-` is refused; `InvokeExtensionAction` cannot
  carry arguments.
- **Screens end to end:** snapshots of the dashboard through `MockTerm`, in the daemon.
- **Performance:** `bench-headless` with the orchestrator loaded and polling shows no throughput
  regression.

## 11. Out of scope for the first version

Windows. Automatic dispatch of any kind. Palette rows per PR or issue. Key bindings contributed by a
manifest (D2). Jira's REST API. A remote daemon works, but worktrees, CLIs and logins are then on the
daemon's machine.

## 12. What the review changed

The first draft of this revision was reviewed against the code by an independent pass. It found, and
this text now fixes:

1. Commands took directories as arguments (`git -C {repo}`), which bypassed root confinement, and
   untyped placeholders allowed a command-running git transport as a "remote". → typed arguments,
   validated working directories, a fixed command environment (§5.1).
2. Reconciliation compared stored paths with `git worktree list`, which forgets every run behind a
   symlinked root. → existence is learned from a failing start; listing is used only for orphans (§6.3).
3. A guest restart mid-dispatch could remove a worktree under a running agent. → tags (§5.2, §6.3).
4. The inbox did not fit `pane.reportStatus` as it exists, and was at-most-once. → C20, re-posting.
5. "Input only from a person" overstated what the daemon can enforce. → §7 says what holds.
6. The summary was resolved on the window side, where it would never show. → §5.6.
7. Process spawning was placed in a layer that cannot implement the interface, and host-facing
   interfaces in a module `vthost` must build without. → §5.3.
8. The host-function limit was misdescribed; the real constraint is one table per `Engine`. → C7.
9. Liveness by "boot epoch" had nothing to stand on and broke with `rerun_programs`. → C21, §5.4.
10. The default branch was read in a way that fails on common clones. → §6.2.
11. Start-up could exceed the process cap; long lists were silently cut; failures had no back-off. →
    §6.1, §6.2.

A second independent review, of the text as revised after the first, found and this text now fixes:

12. An issue run's branch tracked the default branch, so a plain `git push` went there. → `--no-track`
    and `push.autoSetupRemote` (§6.2).
13. Commands were not bound to a root on the host side, and `file-read` reached the repositories. →
    `@<root>` on every command and pane, `read <root>`, `path:<root>` (§5.1, §7).
14. The host's own `git status` could run a program an agent configured. → §6.3, §7.
15. A fresh setup could never start, because the first commands ran in a directory that did not exist
    yet. → they run in a repository.
16. Changing `pane.reportStatus` would have broken older daemons and a shipped WIT import. → a new
    verb (C20).
17. Two worlds in one process could cross-link host functions. → C22.
18. An agent that failed to start vanished without a trace. → §6.3.
19. "Open a shell there" had no mechanism. → dropped for v1.

## 13. What changed for C++ and `core::tui` (revision 4), and what was measured

Requested after revision 3: write the extension in C++ and use core-cpp for the TUI. Before changing
the text, both were tried.

**A C++23 guest — measured.** A hand-written reactor for the existing status-line world, built with
wasi-sdk 34 (clang 23.1, `-std=c++23 -Os -flto -fno-exceptions -fno-rtti -mexec-model=reactor`),
loaded and ran correctly under Contour's own WAMR 2.4.5 build through a small harness: `load`,
`update`, `render` and the host calls in both directions. Not exercised: Contour's own world check,
which was compared by reading. Findings that shaped §5.9 and D6:

- With exceptions on, the module needs WebAssembly exception handling and WAMR refuses to load it.
- `<iostream>` imports `fd_read`, which the host's WASI allow-list refuses.
- The `section` attribute does not create a custom section; module-level assembly does.
- `std::format` costs about 400 KiB of module (about 240 KiB after `wasm-opt`); glaze about 32 KiB.
  Both are far inside the 16 MiB limit.
- `wit-bindgen c` output for the status-line world matches the host's names and signatures.
- The same sources build natively with the system compiler.

**`core::tui` — read, not built.** §5.8 lists what stands between it and a guest. Nothing in it was
compiled for wasm, and libunicode's wasm build is unverified (R8).

What this revision does not change: every product decision in §1, the host design in §5.1–§5.7, the
lifecycle in §6, and the safety model in §7.
