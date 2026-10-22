# Independent Codex re-review — 2026-10-21

Archived final answer from the separate Codex review session. Reviewed snapshot: `9a84365d0`.
This is historical review evidence; later commits require their own verification.

**Verdict: changes requested.** At `9a84365d0`, **7 prior findings are FIXED and 9 are PARTIAL**. The branch builds and passes its tests, but it is not production-ready.

Reviewed `e0e3d1839331bd7e79ad7ec9b987118ce2a94d50..HEAD`: 17 fix commits plus documentation. Source remained unchanged; working tree is clean.

**Verification results**

| Check | Result |
|---|---|
| Required container build, with `-Werror` | PASS |
| Required container tests | **35/35 passed**, zero failures; 20.77 seconds |
| Mod smoke script, real Modrinth API | PASS—all steps |
| Pack smoke script, real Modrinth API | PASS—all steps |

There were nonfatal Ninja recovery and CMake configuration warnings, including unavailable `clang-format`; no compiler warning caused a build failure.

**The 16 prior findings**

| # | Prior finding | Rating | Verification |
|---|---|---|---|
| 1 | IPC reentrancy through nested event loops | **FIXED** | Dispatch marks the operation busy; nested IPC requests are rejected before execution. Normal completion clears the guard. [Application.cpp:1335](/home/user/prism-cli-spike/launcher/Application.cpp:1335), [Application.cpp:1590](/home/user/prism-cli-spike/launcher/Application.cpp:1590). |
| 2 | Timeout abandons still-running tasks | **PARTIAL** | Abort plus a grace period helps, but unfinished tasks are then retained while execution returns. Dispatch releases its busy guard, permitting subsequent mutations while the old task still runs. Retaining the task does not protect its referenced instance/model. [TaskRunner.cpp:139](/home/user/prism-cli-spike/launcher/cli/TaskRunner.cpp:139), [Application.cpp:1555](/home/user/prism-cli-spike/launcher/Application.cpp:1555). |
| 3 | Retry policy fails for nested CLI jobs | **PARTIAL** | Newly constructed jobs inherit the disabled default, but dependency project lookups explicitly override it with the API’s default `askRetry=true`. Existing jobs also retain their previous policy. IPC operations can therefore still show retry dialogs. [GetModDependenciesTask.cpp:187](/home/user/prism-cli-spike/launcher/minecraft/mod/tasks/GetModDependenciesTask.cpp:187), [ResourceAPI.h:129](/home/user/prism-cli-spike/launcher/modplatform/ResourceAPI.h:129), [ResourceAPI.cpp:168](/home/user/prism-cli-spike/launcher/modplatform/ResourceAPI.cpp:168). |
| 4 | Update metadata task never executed | **FIXED** | The sequence now includes both hashing and `EnsureMetadataTask`. [ModCommands.cpp:964](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:964), [ModCommands.cpp:971](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:971). |
| 5 | Update loses exit codes / prints duplicate envelopes | **FIXED** | Target resolution returns typed errors preserving exits 3 and 5. Phase errors return to the orchestrator, which reports each once. [ModCommands.cpp:892](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:892), [ModCommands.cpp:1272](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1272). |
| 6 | Updating disabled mods enables them | **PARTIAL** | Successful batches restore `.disabled`, confirmed by smoke testing. **P1 remains:** any download failure returns before restoration, so a successful disabled-mod update alongside another failed download leaves that mod enabled. Restoration failures also only warn. [ModCommands.cpp:1190](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1190), [ModCommands.cpp:1216](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1216). |
| 7 | Update resolves dependencies without installing them | **FIXED** | Resolved dependencies become `ResourceDownloadTask`s in the download batch. Real-API smoke testing passed the installation case. [ModCommands.cpp:1167](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1167). |
| 8 | Screenshot copy / icon installation deletes its own source | **FIXED** | Both operations check canonical file identity before replacement. [ScreenshotCommands.cpp:233](/home/user/prism-cli-spike/launcher/cli/ScreenshotCommands.cpp:233), [InstanceCommands.cpp:108](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:108). |
| 9 | Instance deletion escalates trash to permanent deletion / reports false success | **FIXED** | Trash failure returns an error; permanent deletion requires `--purge`, and the instance directory is checked afterward. [InstanceCommands.cpp:620](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:620). |
| 10 | Import remains interactive and ignores download flag | **PARTIAL** | Flags propagate into Modrinth/Flame tasks, but Flame’s optional-file and blocked-file dialogs remain unconditional. MultiMC/Technic import paths still finish without honoring the requested game download. [FlameInstanceCreationTask.cpp:563](/home/user/prism-cli-spike/launcher/modplatform/flame/FlameInstanceCreationTask.cpp:563), [FlameInstanceCreationTask.cpp:600](/home/user/prism-cli-spike/launcher/modplatform/flame/FlameInstanceCreationTask.cpp:600), [InstanceImportTask.cpp:348](/home/user/prism-cli-spike/launcher/InstanceImportTask.cpp:348). |
| 11 | Partial mutations report `ok:true`, exit 0 | **PARTIAL** | Shared mod/pack mutation handling now returns failure correctly. However, update metadata failures populate `failed` without setting an outcome flag; execution reaches success. **Reproduced:** the only mod appeared in `failed`, while the command returned `ok:true`, exit 0. [CommandUtils.cpp:554](/home/user/prism-cli-spike/launcher/cli/CommandUtils.cpp:554), [ModCommands.cpp:988](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:988), [ModCommands.cpp:1324](/home/user/prism-cli-spike/launcher/cli/ModCommands.cpp:1324). |
| 12 | Client-relative paths use launcher data directory | **PARTIAL** | Standalone execution remembers its startup directory. IPC sends unchanged commands without the requesting client’s directory, so the receiving GUI resolves relative paths against **its own** startup directory. [main.cpp:71](/home/user/prism-cli-spike/launcher/main.cpp:71), [Ipc.cpp:34](/home/user/prism-cli-spike/launcher/cli/Ipc.cpp:34), [CommandUtils.cpp:92](/home/user/prism-cli-spike/launcher/cli/CommandUtils.cpp:92). |
| 13 | `servers.dat` GUI coordination / unknown NBT loss | **PARTIAL** | Running-instance checks and root-tag preservation were added. Per-server unknown tags are still discarded, and pending GUI edits remain uncoordinated. **Reproduced:** CLI add preserved a custom root tag but deleted the existing server’s `hideAddress`. [ServersDat.cpp:134](/home/user/prism-cli-spike/launcher/minecraft/ServersDat.cpp:134), [ServersDat.cpp:74](/home/user/prism-cli-spike/launcher/minecraft/ServersDat.cpp:74), [ServersPage.cpp:385](/home/user/prism-cli-spike/launcher/ui/pages/instance/ServersPage.cpp:385). |
| 14 | Raw IPC differs from standalone output | **PARTIAL** | Empty raw output and truncation notification are fixed. Unicode truncation can split a surrogate pair; see new issue below. [Output.cpp:200](/home/user/prism-cli-spike/launcher/cli/Output.cpp:200), [Application.cpp:363](/home/user/prism-cli-spike/launcher/Application.cpp:363). |
| 15 | Log read errors look empty / upload ignores gzip | **PARTIAL** | Open/decompression errors now propagate, and gzip uploads decompress first. Plain-file read/seek errors remain unchecked. **Reproduced:** a log symlink to `/proc/self/mem`, which fails reading at offset zero, returned successful empty output, exit 0. [LogReader.cpp:193](/home/user/prism-cli-spike/launcher/logs/LogReader.cpp:193), [LogReader.cpp:297](/home/user/prism-cli-spike/launcher/logs/LogReader.cpp:297), [LogCommands.cpp:367](/home/user/prism-cli-spike/launcher/cli/LogCommands.cpp:367). |
| 16 | Unsafe writable settings / weak validation | **FIXED** | Managed identity/state keys are blocked for set and unset. Boolean/integer validation, memory bounds/order, window dimensions, and JSON-object validation are checked before mutation. [SettingsCommands.cpp:204](/home/user/prism-cli-spike/launcher/cli/SettingsCommands.cpp:204), [SettingsCommands.cpp:402](/home/user/prism-cli-spike/launcher/cli/SettingsCommands.cpp:402), [SettingsCommands.cpp:580](/home/user/prism-cli-spike/launcher/cli/SettingsCommands.cpp:580). |

**New issues introduced by the fixes**

1. **P1 — URL imports are corrupted by filesystem path normalization.**
   `instance create --from https://…` unconditionally calls `resolveClientPath()`. HTTPS and `file://` URLs are treated as relative filenames and prefixed with the startup directory before `QUrl` construction. Preserve URL sources and normalize only filesystem paths. The behavioral probe hung until its 40-second timeout. [InstanceCommands.cpp:302](/home/user/prism-cli-spike/launcher/cli/InstanceCommands.cpp:302), [CommandUtils.cpp:88](/home/user/prism-cli-spike/launcher/cli/CommandUtils.cpp:88).

2. **P2 — UTF-8 truncation can split supplementary Unicode characters.**
   The truncator counts each UTF-16 surrogate as three bytes rather than treating the pair as one four-byte character. Executing the current `Output.cpp` with an ASCII prefix followed by an emoji produced a captured string ending in a lone high surrogate, `0xd83d`. [Output.cpp:157](/home/user/prism-cli-spike/launcher/cli/Output.cpp:157), [Output.cpp:183](/home/user/prism-cli-spike/launcher/cli/Output.cpp:183).

3. **P2 — CLI dispatch changes retry behavior of unrelated GUI jobs.**
   During a CLI nested event loop, GUI jobs constructed elsewhere inherit `false` permanently. Restoring the global default afterward does not restore those jobs’ behavior. The policy needs operation ownership. [Application.cpp:1341](/home/user/prism-cli-spike/launcher/Application.cpp:1341), [NetJob.cpp:62](/home/user/prism-cli-spike/launcher/net/NetJob.cpp:62).

4. **P2 — Timed-out tasks are retained permanently.**
   The supposed reaper is a deliberately leaked vector with no completion-based removal. Every task retained after the grace period remains referenced even after finishing, potentially retaining downloads and associated resources for the GUI process’s lifetime. [TaskRunner.cpp:42](/home/user/prism-cli-spike/launcher/cli/TaskRunner.cpp:42), [TaskRunner.cpp:142](/home/user/prism-cli-spike/launcher/cli/TaskRunner.cpp:142).

5. **P3 — Documentation contradicts the implemented failure contract.**
   The roadmap says mixed failures return exit 0; the implementation returns exit 1. It also claims every failure has a machine code, although update metadata/check failures lack one. [SPIKE-ROADMAP.md:786](/home/user/prism-cli-spike/SPIKE-ROADMAP.md:786), [CommandUtils.cpp:554](/home/user/prism-cli-spike/launcher/cli/CommandUtils.cpp:554).

**Focused regression assessment**

- **Mod-update phase split:** typed error propagation and dependency downloads are improvements. Disabled-state cleanup must run for successful individual downloads even when the batch fails. Metadata failures must participate in the final outcome.
- **CommandContext:** consistently passed through dispatch; no concrete regression found. It remains an application reference, without operation-lifetime or retry-policy ownership.
- **Retry restoration/thread safety:** all normal dispatch returns reach restoration; no normal early-return hole found. The static Boolean is unsynchronized and is not generally thread-safe, although I did not establish an actual worker-thread race in current call paths.
- **Timeout handling:** keeping tasks alive prevents their immediate destruction, but does not establish completion, retain referenced models safely, or prevent subsequent conflicting operations.

**Recommendation**

Keep this as a spike. Before production use, finish timeout lifecycle protection, remove remaining interactive import/retry paths, fix disabled-state restoration on partial failure, carry client path context through IPC, preserve per-server NBT fields, and coordinate GUI server edits.

Add focused regression coverage for those failure paths; the green smoke tests primarily validate successful operations.

Technically, further fixes are needed before treating this as upstream-ready. Independently, [CONTRIBUTING.md:5](/home/user/prism-cli-spike/CONTRIBUTING.md:5) states that AI-created, documented, or aided contributions are typically closed and locked, which is a separate upstream acceptance constraint.
