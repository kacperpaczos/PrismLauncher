# Initial independent Codex review — 2026-10-21

Historical snapshot: `e0e3d1839`. Findings below refer to that revision, before subsequent fixes.

My assessment: **this is a substantial, useful prototype, but I would request changes before treating it as a production CLI or proposing the whole branch upstream.** The command model is a reasonable foundation. The weak point is execution: synchronous command handlers sit over asynchronous, sometimes interactive APIs without sufficiently strong ownership, cancellation, or mutation guarantees.

Reviewed branch `spike/mod-cli` at `e0e3d1839`. The container build passed, and **all 33 configured tests passed**, including the offline domain and IPC smoke tests. I skipped the network smoke scripts. No source changes, commits, or pushes were made. Findings below are code-derived unless explicitly described as test results.

**1. Architecture**

The CLI has five main layers:

- **Command data:** `Commands.h` defines value-like command structures collected in a `std::variant`, plus `Args` and `CommandContext`.
- **Parsing:** CLI11 binds options into parser members; `flush()` methods construct commands afterward.
- **Execution:** `Application::processCommands()` visits commands and calls domain-specific runners. Runners perform work, print results, and return an exit code.
- **Task adaptation:** `runTaskSync()` starts or waits for a Prism `Task`, pumping a local `QEventLoop`; `waitForModel()` similarly waits for resource scans and parsing.
- **IPC:** secondary processes serialize commands through `ApplicationMessage`; the owning process executes them while `OutputCapture` collects their output for a response.

“Headless” currently means **a `QApplication` with its main window and setup wizard suppressed**, with offscreen platform selection when no display variables are present. It remains the full launcher application, including much of its initialization.

Several choices are sound:

- Parsing into typed command data is much better than passing CLI11 objects into business logic.
- Reusing Prism’s download, dependency, metadata, and instance tasks avoids implementing a separate launcher.
- Explicit UTF-8 JSON, shared exit codes, ambiguity reporting, and stdout/stderr separation are sensible foundations.
- Waiting for resource parsing, rather than assuming a directory scan immediately yields complete metadata, addresses a real Prism constraint.
- Request/response IPC is necessary if CLI mutations must cooperate with an already running launcher.

However, the execution boundary needs redesign.

**High priority: nested event loops allow commands to re-enter one another.**
[`TaskRunner.cpp:54`](/home/user/prism-cli-spike/launcher/cli/TaskRunner.cpp:54), [`CommandUtils.cpp:269`](/home/user/prism-cli-spike/launcher/cli/CommandUtils.cpp:269), and [`Application.cpp:1616`](/home/user/prism-cli-spike/launcher/Application.cpp:1616)

`messageReceived()` directly calls `processCommands()`. A command then enters a nested event loop while retaining raw instance, model, and resource pointers. That loop continues delivering GUI actions and additional IPC requests.

Consequently, another request or GUI action can delete an instance, rename it, alter its profile, or change its resources while the first command still holds pointers into that state. The initial `checkInstanceRunning()` check does not protect against launching the instance during the wait.

This is a concrete reentrancy risk; I did not reproduce a crash. There is no command queue or operation guard providing the serialization that the handlers implicitly assume.

There is another consequence: a headless process can itself receive IPC while its initial command is waiting. Its nested `processCommands()` reaches the unconditional `m_headlessRun` exit branch at [`Application.cpp:1527`](/home/user/prism-cli-spike/launcher/Application.cpp:1527), potentially terminating the outer operation prematurely.

**High priority: timeout means “stop waiting,” without a guarantee that work has stopped.**
[`TaskRunner.cpp:65`](/home/user/prism-cli-spike/launcher/cli/TaskRunner.cpp:65)

The timeout handler attempts `abort()` only when allowed, ignores its result, and immediately exits the loop. It neither waits for cancellation completion nor transfers ownership into a managed background operation. Callers can therefore return errors and discard local state while a task is still active.

This is particularly concerning for operations using raw model pointers or callbacks referencing caller-owned variables. A timeout needs a defined outcome: cancelled and quiescent, still running under an operation ID, or completed with an uncertain client response.

**High priority: unattended policy belongs to an operation, not the process.**
[`NetJob.cpp:177`](/home/user/prism-cli-spike/launcher/net/NetJob.cpp:177)

The global retry-dialog suppression checks `Application::isHeadlessRun()`. That works for standalone CLI processes, but commands forwarded to a normal GUI process see `false`.

The explicit `makeCliNetJob()` helper correctly disables prompts for jobs it creates. It does not cover nested jobs created by `ResourceDownloadTask`, dependency resolution, component metadata loading, or instance import. Those paths can still ask for user interaction through IPC.

I would keep the asynchronous core and make the CLI wait only at its outermost boundary:

1. An operation owns its task graph, result, progress, cancellation, and interaction policy.
2. GUI and CLI submit the same operations.
3. CLI startup uses the normal application event loop and exits on operation completion.
4. IPC submits operations through a queue or explicit conflict policy.
5. Formatting happens after execution, from a structured result.

For example, a result should carry typed errors and per-item outcomes, rather than having helpers print an error and return `nullopt`. This requires extracting selected operations from GUI workflows; it does **not** require rewriting Prism into an entirely new client/server application.

**2. Completeness**

The command surface is already broad enough to be useful. I would prioritize reliability over adding more verbs.

The principal production gaps are:

- **A stable output contract:** schema version, capabilities, partial success rules, output cardinality, truncation, cancellation, and timeout semantics.
- **Reliable mutation results:** success must mean the requested operation actually succeeded, including persistence failures.
- **Consistent standalone and IPC behavior:** paths, stdin, prompts, errors, batches, and raw output currently differ.
- **Operation coordination:** concurrent CLI requests, GUI edits, running games, and multiple processes sharing a data directory.
- **Explicit import policy:** optional files, blocked downloads, trust decisions, download requirements, and selection of the resulting instance.
- **Recovery:** failed creation or version changes can leave committed or partially modified state without sufficiently useful recovery information.

For a genuinely terminal-oriented workflow, important surface gaps include unattended account/authentication handling, Java discovery/configuration, predictable launch completion/error reporting, instance export and managed-pack updates, local resource installation, datapacks, and broader provider support. These need not all be release blockers if the supported scope is explicit.

The existing tests are valuable but disproportionately test parsing and serialization. The four `CliTaskRunner` cases cover null, success, failure, and timeout; they do not establish cancellation safety or reentrancy safety. The IPC smoke test exercises ordinary requests sequentially.

I would require deterministic tests for:

- Concurrent requests and instance deletion/launch during an operation.
- Non-abortable tasks and abort failures.
- Network failure inside nested tasks, through both execution modes.
- Updating a disabled mod and installing newly required dependencies.
- Metadata discovery for an unindexed mod.
- Partial permission failures and failed trash operations.
- Relative paths and source/destination aliases.
- Empty and oversized raw output.
- Optional-file imports and imports without `--name`.
- Malformed IPC frames and semantically invalid command arguments.

Use a local HTTP fixture for most network behavior. Live Modrinth smoke tests are useful compatibility checks, but cannot reliably exercise these failure paths.

Documentation is also unfinished. `SPIKE-ROADMAP.md` contains useful engineering history, but a production interface needs a concise user reference and a separate machine-readable contract. In particular, document grouped setting overrides, destructive fallback behavior, provider limitations, `--yes` semantics, and what happens after an IPC timeout.

**3. Direction and upstreamability**

The maintainers’ responses support a narrower conclusion than either “this will be accepted” or “CLI work is unwanted.”

- [Issue #103](https://github.com/PrismLauncher/PrismLauncher/issues/103#issuecomment-1285766873) supports expanded CLI functionality. The objection to replacing Qt should not be interpreted as rejecting CLI tooling.
- [PR #6193](https://github.com/PrismLauncher/PrismLauncher/pull/6193) explicitly establishes typed commands and CLI11 as groundwork for a better CLI.
- [Ryex’s response on #6316](https://github.com/PrismLauncher/PrismLauncher/issues/6316#issuecomment-6312347620) welcomes collaboration on good CLI tooling, while explicitly rejecting a hacked synchronous workflow and asking for unattended task execution and error paths.
- [Issue #6315](https://github.com/PrismLauncher/PrismLauncher/issues/6315) has no maintainer response establishing a preferred retry-policy design.
- [PR #6314](https://github.com/PrismLauncher/PrismLauncher/pull/6314) received Trial97’s comment and approval, but now also has a changes-requested review questioning reproduction. That is not settled consensus.

The hostile non-member comment on #6316 is not an upstream technical decision. Ryex’s response is the relevant signal.

My recommendation:

| Area | Recommendation |
|---|---|
| Independent build fixes | Good candidates for small PRs with clean reproduction evidence. |
| `ServersDat` extraction | Good direction, after addressing coordination and preservation concerns below. |
| Typed command/output primitives | Discuss a small contract proposal; avoid committing upstream to the entire current schema. |
| Simple inspection commands | Plausible early additions once startup and error behavior are dependable. |
| Unattended task/error handling | Make this the first substantive design effort with maintainers. |
| Mutation commands and IPC execution | Redesign the execution boundary before proposing broad adoption. |
| Entire current branch | Keep as an experimental fork while extracting smaller changes. |

I would **not** classify mods, JSON output, or IPC as inherently fork-only. The current synchronous execution mechanism and any agent-specific wrapper policy should remain fork-only unless redesigned or explicitly accepted.

**Basing the experiment on #6193 was reasonable; treating that branch as a stable product dependency is a problem.** Ryex explicitly warns that it will be repeatedly rebased. The resulting conflicts will affect precisely the central files this spike expands: parsing, startup, dispatch, and IPC.

Keep the conceptual dependency, but maintain a clearly identified base and a separable patch series. Independent fixes should target upstream directly. Coordinate the operation interface before expanding the patch stack further.

For #6313/#6314 specifically, provide the exact upstream SHA, build configuration, relevant variable scopes, and generated XML. The current branch has the local assignment at [`program_info/CMakeLists.txt:23`](/home/user/prism-cli-spike/program_info/CMakeLists.txt:23), and its generated XML contains the summary. That verifies this checkout’s output; it does not independently settle reproduction on every upstream configuration.

**4. C++/Qt correctness and code quality**

These are the additional findings I would act on, in priority order.

**High: file replacement can delete its own source.**
[`ScreenshotCommands.cpp:218`](/home/user/prism-cli-spike/launcher/cli/ScreenshotCommands.cpp:218), [`InstanceCommands.cpp:79`](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:79)

`screenshot copy --force` removes the destination before copying. If the destination is the screenshot itself—or resolves to it—the source is deleted and the copy fails.

`resolveIconKey()` has the same pattern: remove the installed target, then install from the supplied source. Supplying that installed icon as the source deletes it first. Installation success is not checked before reporting the target path.

Reject or safely handle identical files and aliases, and replace files atomically.

**High: instance deletion silently escalates from trash to permanent deletion.**
[`InstanceCommands.cpp:571`](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:571)

Without `--purge`, a failed trash operation falls through to `deleteInstance()`. Thus the absence of desktop trash support changes a reversible request into permanent deletion.

Furthermore, `deleteInstance()` returns `void` and only logs filesystem failure; the CLI subsequently returns success. See [`InstanceList.cpp:428`](/home/user/prism-cli-spike/launcher/InstanceList.cpp:428).

The world command already has the better behavior: fail and require `--permanent`. Apply the same principle to instances and return an actual deletion result.

**High: metadata discovery in `mod update` never runs.**
[`ModCommands.cpp:952`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:952)

`ensureUpdateMetadata()` creates `EnsureMetadataTask`, adds its hashing task to a sequence, and runs the sequence. It never adds `metadataTask` itself.

The GUI correctly runs both at [`ResourceUpdateDialog.cpp:385`](/home/user/prism-cli-spike/launcher/ui/dialogs/ResourceUpdateDialog.cpp:385).

Consequently, unindexed mods remain without metadata. They are appended to `report.failed`, but that path does not set `hasCheckFailures`, so the overall command can still return success.

**High: updates resolve new dependencies but do not install them.**
[`ModCommands.cpp:1063`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1063), [`ModCommands.cpp:1121`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1121)

Dependency resolution produces JSON entries, but `applyUpdates()` downloads only `updateDownloads`. A mod update that introduces a required dependency can leave the instance broken while reporting success.

The comment explicitly describes this choice; I disagree with the semantics. Reporting dependencies is appropriate for a dry run. An ordinary update should install required dependencies, or refuse with a clear incomplete-plan result.

**High: updating disabled mods loses their disabled state.**
[`ModCommands.cpp:881`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:881), [`ModCommands.cpp:1044`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1044)

`--all` includes disabled resources. The checker preserves the previous enabled state in `CheckUpdateTask::Update::enabled`, but the CLI ignores it and schedules the download directly.

`ResourceDownloadTask` writes the normal filename. The command should either skip disabled mods by explicit policy or preserve their disabled state through replacement.

**High: instance import is still an interactive workflow.**
[`InstanceCommands.cpp:280`](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:280)

`setConfirmUpdate(false)` suppresses one question, not all interaction. For example, Modrinth optional files still reach `OptionalModDialog::exec()` at [`ModrinthInstanceCreationTask.cpp:418`](/home/user/prism-cli-spike/launcher/modplatform/modrinth/ModrinthInstanceCreationTask.cpp:418). Other import paths include blocked-file and warning dialogs.

The import branch also ignores `cmd.download`. The new task-level download switch is used for vanilla creation, but not propagated through import into its child creation tasks.

Those child tasks retain the behavior at [`InstanceTask.cpp:165`](/home/user/prism-cli-spike/launcher/InstanceTask.cpp:165): show a download error and then emit success. This is exactly the kind of existing GUI-oriented API that needs a stronger unattended contract.

**Medium: imports without a name lack the promised name-discovery workflow.**
[`Parser.cpp:517`](/home/user/prism-cli-spike/launcher/cli/Parser.cpp:517), [`InstanceCommands.cpp:281`](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:281)

The parser permits `--from` without `--name`, claiming the name comes from the pack. Execution passes the empty name to the import task and predicts the resulting ID before processing the archive.

The GUI supplies original-name information separately; the CLI does not reproduce that preparation. More generally, predicting an ID is fragile even with a supplied name because commit-time collision resolution can differ. The staging operation should return its actual committed instance ID.

**Medium: partial mutation failures can produce `ok:true` and exit zero.**
[`ModCommands.cpp:350`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:350), [`ModCommands.cpp:823`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:823), [`PackCommands.cpp:306`](/home/user/prism-cli-spike/launcher/cli/PackCommands.cpp:306)

The code distinguishes “nothing succeeded” and “some targets were unresolved,” but misses “one filesystem mutation succeeded and another failed.”

For example, removing an unlocked mod alongside a locked mod can return success with a nonempty `failed` array. Similar behavior exists for enable/disable and pack removal.

Partial results are useful. A successful top-level status for failed requested work is not. Define the policy centrally and calculate it from all item outcomes.

**Medium: the `mod update` refactor loses specific errors and can print multiple envelopes.**
[`ModCommands.cpp:890`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:890), [`ModCommands.cpp:1176`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1176)

`resolveUpdateTargets()` prints `Ambiguous` or `NotFound`, returns `nullopt`, and its caller returns `Generic`. An entirely missing target therefore loses exit code 3; an ambiguous target loses exit code 5.

`checkUpdatableMods()` can print a failure at line 1036, after which the outer runner prints another failure at line 1209. Direct stdout then contains multiple documents, while IPC capture keeps only the last.

Return structured errors from helpers and render exactly once.

**Medium: relative filesystem arguments use the launcher’s working directory.**
[`Application.cpp:306`](/home/user/prism-cli-spike/launcher/Application.cpp:306)

Application startup changes the working directory to the data directory. New commands retain relative strings and later construct `QFileInfo` objects from them.

This affects imports, icon paths, notes files, world imports, and screenshot destinations. A command run from a download directory should not interpret `./pack.mrpack` relative to Prism’s data directory. IPC introduces the same problem across processes.

Resolve client-local paths before changing directories or serializing commands. Keep explicitly game-relative paths, such as `log --file`, as a separate concept.

**Medium: sharing `ServersDat` does not coordinate GUI and CLI state.**
[`ServerCommands.cpp:169`](/home/user/prism-cli-spike/launcher/cli/ServerCommands.cpp:169), [`ServersPage.cpp:385`](/home/user/prism-cli-spike/launcher/ui/pages/instance/ServersPage.cpp:385)

CLI commands load and save a separate `ServersDat`; the GUI model keeps its own copy and schedules delayed saves. Pending GUI edits can be missed by a CLI read, or later overwrite a CLI mutation.

Server mutations also lack the running-instance check used elsewhere, although the GUI model has locking behavior.

The extraction is worthwhile, but both frontends need a coordinated mutation service. Atomic file writes prevent torn writes; they do not prevent lost updates.

The serializer also reconstructs only recognized fields at [`ServersDat.cpp:156`](/home/user/prism-cli-spike/launcher/minecraft/ServersDat.cpp:156), discarding unknown NBT fields. Preserve them if this is intended as a general editor.

**Medium: raw IPC output is not equivalent to standalone output.**
[`Output.h:116`](/home/user/prism-cli-spike/launcher/cli/Output.h:116), [`Output.cpp:156`](/home/user/prism-cli-spike/launcher/cli/Output.cpp:156), [`Application.cpp:358`](/home/user/prism-cli-spike/launcher/Application.cpp:358)

An empty raw log produces no text, so capture classifies it as non-raw and the client prints `{}`. Standalone execution prints nothing.

Large raw output is capped, but the client prints only `text`, discarding the `truncated` indication. The caller receives incomplete output with success and no visible warning.

Also, the advertised byte cap counts `QString` code units rather than encoded bytes. Output type and truncation should be explicit result properties.

**Medium: log failures can look like successful empty reads.**
[`LogReader.cpp:295`](/home/user/prism-cli-spike/launcher/logs/LogReader.cpp:295), [`LogCommands.cpp:287`](/home/user/prism-cli-spike/launcher/cli/LogCommands.cpp:287)

An open failure returns an empty `Content`; gzip errors are logged without becoming command errors. The runner returns success.

`log upload` separately reads raw bytes with `QFile::readAll()` at [`LogCommands.cpp:359`](/home/user/prism-cli-spike/launcher/cli/LogCommands.cpp:359), so selecting a `.log.gz` file does not use the decompression supported by `log read`.

Use one reader with explicit I/O/decompression errors and bounded output.

**Medium: settings expose more authority than their validation supports.**
[`SettingsCommands.cpp:135`](/home/user/prism-cli-spike/launcher/cli/SettingsCommands.cpp:135), [`SettingsCommands.cpp:181`](/home/user/prism-cli-spike/launcher/cli/SettingsCommands.cpp:181)

The “extra” schema is described as additional listing information, but `findSetting()` also makes it writable. That includes UUID, instance type, and name, bypassing dedicated instance operations and their checks.

Integer validation allows negative memory and window dimensions. Whole-`env` assignment accepts arbitrary strings without JSON validation.

Grouped overrides also need an explicit contract: unsetting `memory.max` resets `OverrideMemory`, changing the effective behavior of sibling settings. The implementation acknowledges this, but the command name does not make that consequence apparent.

Separate readable and writable schema entries, validate domain constraints, and report all effective changes caused by a grouped override.

Other C++/Qt observations:

- `CommandContext{Application&}` is a convenient dependency bundle, but currently grants every runner access to the entire application. It is not yet a domain interface.
- `loadProfile()` and `loadComponentProfile()` have different completion/error semantics. Several callers ignore failure. Consolidate them around an explicit loaded/resolved result.
- Mod enable/disable directly calls `Resource::enable()`, bypassing the model index updates and `dataChanged` notifications used by `setResourceEnabled()`. See [`ModCommands.cpp:330`](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:330) versus [`ResourceFolderModel.cpp:294`](/home/user/prism-cli-spike/launcher/minecraft/mod/ResourceFolderModel.cpp:294).
- `VersionChangeMc` calls `setComponentVersion(..., true)`, which already starts resolution, then calls `resolveProfile()`, which starts another resolution. See [`PackProfile.cpp:991`](/home/user/prism-cli-spike/launcher/minecraft/PackProfile.cpp:991).
- IPC string lists and settings values use newline delimiters inside JSON. This is lossy; the limitation is even pinned by [`ApplicationMessage_test.cpp:734`](/home/user/prism-cli-spike/tests/ApplicationMessage_test.cpp:734). Use JSON arrays and typed values.
- IPC parsing is permissive about types, numeric conversions, and unknown enum values; it does not apply the CLI parser’s semantic validation.
- The transport lacks frame-size limits. Its inherited receive loop can also wait indefinitely for an incomplete header at [`LocalPeer.cpp:367`](/home/user/prism-cli-spike/libraries/LocalPeer/src/LocalPeer.cpp:367). This deserves attention as the endpoint gains mutation capabilities.
- The IPC client exits after its first headless response at [`Application.cpp:366`](/home/user/prism-cli-spike/launcher/Application.cpp:366), while local dispatch supports a vector. Either prohibit batching or define consistent ordering and completion semantics.

The style broadly fits Prism: Qt types, `makeShared`, `Result`, overloads, and explicit ownership contexts are familiar. I would not object to QObject allocations merely because they use `new`.

The maintainability issue is duplication and responsibility: parser backing state, command structures, serializers, deserializers, visitors, JSON builders, and tests all repeat the command surface. The duplicated mod/pack error handling has already produced duplicated bugs. Fix the common result and execution abstractions before doing another cosmetic style pass.

**5. Other material advice**

The branch’s greatest value is that it has exposed where Prism’s existing GUI-oriented operations lack reliable unattended behavior. Preserve those discoveries as small, testable requirements.

I would pause surface expansion and build one exemplary operation end to end: install or update a mod, with deterministic dependency resolution, explicit interaction policy, cancellation, structured failures, instance coordination, and identical standalone/IPC results. Get agreement on that design, then migrate the other commands.

The passing tests establish that many ordinary workflows work. They do not yet establish the properties automation depends on most: **that success is truthful, failure is actionable, and a returned command has a well-defined relationship to any work still running.**
