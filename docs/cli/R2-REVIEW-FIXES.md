# R2 unattended-task review follow-up

Review range: `b4250a419..1b57671ca`. All corrections are forward commits;
`b4250a419` is unchanged. No finding was rebutted. New work awaits independent
review; passing tests are not an approval claim.

## Finding map

| Finding | Fix commit(s) | Result |
| --- | --- | --- |
| H1 | `562666690` | Restore headless fallback alongside task policy; test the predicate used by emitFailed with a pre-existing job. |
| H2 | `b0119dc18` | Delete legacy metadata-result overloads; every caller uses typed codes; no CLI error-message contains matching remains. |
| M1 | See producer audit below | Repair missing acknowledgements and false acceptance; never infer completion from abort's bool. |
| M2 | `db70dd2e2` | MultipleOptionsTask stops advancement during abort; queued/active abort coverage also exercises SequentialTask. |
| M3 | `29eff1434` | Record the rejected child and PolicyConflict cause, drain owned work, leave unrelated work running; map task_policy_conflict/exit 1. |
| L1 | `c02c31c61` | Check active membership before consuming a stale abort snapshot entry; test sibling completion during abort. |
| L2 | `db140eb39` | Header documents inactive/terminal abort as a queue-preserving, signal-free no-op returning true; test inactive/successful/failed states. |
| L3 | `fcf79a262` | Check row membership before dereferencing or reporting a change; unattended model calls reject invalid indices. |
| L4 | `b1c3927aa` | Remove forwarding NetJob override; test inherited abort via Qt meta-object invocation. |
| T1 | `562666690`, `abbe9ed05` | Exercise the actual prompt predicate; response bound is one second against a ten-second operation deadline. |
| T2 | `f1760a71a`, `80f509b45` | Sequential/resource-style cleanup emits one failure; isolated inactive emission asserts in Debug or logs/rejects in Release. |
| T3 | `e95564705` | Move envelope coverage to CliTaskRunner tests and fix affected include ordering. |
| DOCS | `f1760a71a`, `b0119dc18`, this note | Typed child propagation is real; explain aggregate/legacy limits, no message matching, and Incomplete mapping in every mod-install network phase. |

## M1 producer audit

The audit covered out-of-line and inline Task abort overrides under `launcher/`,
including the local InstanceStaging class. A true return accepts a request; it
cannot safely prove that an asynchronous child has already stopped. A fake child
that accepts but never acknowledges cancellation makes the CLI return Incomplete
after its deadline/grace period while retaining ownership; the test explicitly
completes it afterwards for cleanup.

Corrections:

| Commit | Producer(s) | Correction |
| --- | --- | --- |
| `814f273e6` | AssetUpdateTask, LibrariesTask, LegacyFMLLibrariesTask, Java::ArchiveDownloadTask | A running wrapper with no download child now acknowledges cancellation. Tests use each as a composite child; CLI fake covers missing acknowledgement. |
| `2929990ad` | ComponentUpdateTask | Acknowledge idle cancellation and snapshot statuses before synchronous callbacks can clear them. |
| `47bd2ee11` | EnsureMetadataTask | Preserve outgoing completion connections; replace phase advancement with cancellation completion; acknowledge idle cancellation. |
| `ba5e501d0` | ModrinthCheckUpdate | Forward active cancellation completion instead of advancing/losing it; acknowledge idle cancellation. |
| `bd0592e56` | Technic::SingleZipPackInstallTask | Forward the download job's aborted signal. |
| `e0503149b` | LaunchController, LaunchCommand, LauncherPartLaunch | Refuse cancellation of running preparation when no launch task/process can accept it. Existing process callbacks handle accepted requests. |
| `2ac09c983` | ResourceFolderLoadTask | Use emitAborted rather than a bare finished signal that leaves State::Running; real worker/composite regression checks both states. |
| `99a8f4a21` | TaskStepWrapper | Cancel the wrapper waiting for proceed without starting its inactive child; ignore a later proceed. Test the real wrapper independently of the launch state machine. |
| `02edc37a7` | FlamePackExportTask, ModrinthPackExportTask | Return the child's actual abort acceptance rather than hiding refusal. |

Other routes checked:

| Producer(s) | Terminal reporting route |
| --- | --- |
| Task, ConcurrentTask, inherited NetJob | Base emitter or drained active set; inactive composite semantics are explicitly documented. |
| Net::Request | Reply completion or immediate aborted/finished signals; legacy code also manipulates state directly. |
| BaseEntityLoadTask | Base abort emitter plus delegated network cancellation. |
| InstanceCopyTask, ExtractZipTask, ExportToZipTask, Hashing::Hasher | Future watcher completion/cancellation callbacks. |
| LocalModParseTask, LocalShaderPackParseTask, LocalTexturePackParseTask, LocalWorldSaveParseTask | Abort flag is checked at worker completion; invalid-input paths also emit failure. |
| LocalResourceUpdateTask | Direct abort emitter. |
| AuthFlow, TextPrint, LookupServerAddress | Direct abort emitter. |
| InstanceTask, InstanceImportTask, InstanceStaging | Delegate to current work with existing terminal callbacks; non-abortable branches return false. Staging retry completion remains timer-driven. |
| MinecraftLoadAndCheck, AutoInstallJava | Delegate or use the base/direct emitter. |
| LaunchTask | Current step's completion/finalization callbacks. |
| Java::ManifestDownloadTask | Direct abort emitter with delegated request. |
| FlameCheckUpdate | Finished-driven phase chain has a terminal reporting path. |
| Flame::FileResolvingTask | Delegate then base emitter when accepted. |
| FlameCreationTask, ModrinthCreationTask, FTB PackInstallTask, legacy FTB PackInstallTask | Delegated phases plus InstanceTask/base terminal reporting. |
| ATLPackInstallTask, Technic::SolderPackInstallTask | Active network cancellation callbacks; non-abortable phases refuse requests. |

This audit checks notification/acceptance paths, not universal quiescence. Some
legacy wrappers can emit completion before all lower-level work has drained;
queued model callbacks and raw instance/model ownership also remain outside this
reduced contract. HANDOFF finding 1 is still partial. The retained process-global
retry bracket and unported interaction paths keep finding 2 partial as well.

## Verification and scope

Each functional/test correction was built with `./containers/spike-build.sh build`
and passed `./containers/spike-build.sh test` (37/37). Test emission rejection is
compatible with Debug/Release semantics; only the configured container build was
executed, not a separate Release build. The three network smoke scripts are run
separately, and final results are included in the worker completion report.

The separately queued metadata nits follow this R2 work in their own commit;
that follow-up adds build-independent start-attempt accounting and fake offline
metadata coverage, fixes offline dependency-check parity and the trivial index
bounds, and corrects the shared-loader comment. It does not amend the approved
metadata commit.
