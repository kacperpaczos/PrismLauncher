# SPIKE: CLI zarządzania modami — rozpiska

> Cel spiKA: **pełne zarządzanie modami z konsoli** — `prism mod list|search|info|install|remove|enable|disable|update`
> — działające headless (bez GUI, platforma offscreen), z wyjściem JSON i kodami wyjścia.
> Baza: gałąź `cli_rebirth` (PR #6193, Ryex), pin **`059625c8b89509fe5180384fd9db8432512e93a7`**.
> To jest SPIKE — poza zakresem: resource/shader/world/server/log/screenshot/notes/version/settings,
> IPC v2, pełny split initCore/initGui, CurseForge. Pełny plan: sesja Plan (Faza 0–6).

## Kolejność prac (sekwencyjnie, per podagent)

### T0 — Zielony build [subagent: build]
- [x] `git submodule update --init libraries/libnbtplusplus` (+ wszystkie wymagane submoduły)
- [x] Java target: `libraries/launcher/CMakeLists.txt` `-target 7 -source 7` → `-target 8 -source 8`
      (Fedora nie ma JDK<21 — build bez tego pada)
      + `libraries/javacheck/CMakeLists.txt` (ten sam fix, inaczej build pada na tym module)
- [x] Deps dnf: cmake, ninja-build, gcc-c++, extra-cmake-modules, qt6-qtbase-devel,
      qt6-qtnetworkauth-devel, cmark-devel, qrencode-devel, libarchive-devel,
      tomlplusplus-devel, zlib-ng-compat-devel, cli11-devel
      (`java-25-openjdk-devel` — w Fedorze 44 nie ma `java-openjdk-devel`)
- [x] `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
      -DLauncher_BUILD_PLATFORM=dev -DLauncher_ENABLE_JAVA_DOWNLOADER=ON` + build
- [x] `QT_QPA_PLATFORM=offscreen ctest --test-dir build` — zielone (24/24 passed)
- [x] Commit: `chore(build): bump embedded launcher java target 7 -> 8`

**Środowisko (gotowe, `containers/`)**
- `./containers/spike-build.sh image` — obraz `localhost/prism-spike-dev` (Fedora 44 + Qt6 + JDK 25)
- `./containers/spike-build.sh submodules|configure|build|test|all` — stages w rootless podman
- SELinux na hoście blokuje montowanie repo → `podman run --security-opt label=disable` (już w skrypcie)
- Build w `build/` (w .gitignore); pliki należą do użytkownika hosta
- Smoke: `QT_QPA_PLATFORM=offscreen ./build/prismlauncher --help` działa headless

### T1 — Moduł wyjścia CLI [subagent: cli-core]
- [ ] `launcher/cli/Output.h/.cpp`: `outputJson(doc)` / `outputError(code, msg)` — koperta
      `{"ok":true,"data":…}` / `{"ok":false,"error":{"code":"…","message":"…"}}`,
      `QTextStream(stdout)` UTF-8 + flush
- [ ] Exit codes w `Commands.h` (enum `Cli::ExitCode`): 0 ok; 1 ogólny; 2 użycie; 3 not-found;
      4 konflikt (instance running, brak --force); 5 niejednoznaczność; 6 błąd parsowania;
      7 sieć; 8 wymaga GUI
- [ ] Testy jednostkowe Output (do istniejącej struktury testów)

### T2 — Struktury komend + parser [subagent: cli-core]
- [ ] `launcher/cli/Commands.h`: struktury `Cmd::ModList{instance, includeDisabled}`,
      `Cmd::ModSearch{query, mcVersions, loaders, limit, offset}`,
      `Cmd::ModInfo{project, instance}`, `Cmd::ModInstall{project, instance, version, noDeps,
      noMetadata}`, `Cmd::ModRemove{targets, instance, yes}`, `Cmd::ModEnable/Disable{targets,
      instance, recursive}`, `Cmd::ModUpdate{targets|all, instance, dryRun, noDeps, force}`
      — rozszerzyć `CommandBase` variant; styl: `operator<=> = default` jak istniejące
- [ ] `launcher/cli/Parser.cpp`: grupa subkomend `mod` (CLI11: `app.require_subcommand()` na
      poziomie mod), wspólne flagi `-i/--instance`, `--json`, `-y/--yes`, `--force`;
      legacy flags nietknięte
- [ ] Rozszerzyć `ApplicationMessage::fromCliCommand/toCliCommand` o nowe komendy (spójność CLI/IPC)

### T3 — runTaskSync [subagent: cli-core]
- [ ] `launcher/cli/TaskRunner.h/.cpp`: `Result<> runTaskSync(Task::Ptr, timeoutMs)` —
      lokalna `QEventLoop` + `Task::finished` + timeout (wzorzec: `FlameAPI::getModFileChangelog`,
      `Meta::Index::loadVersion`); raport postępu 1 linia na stderr

### T4 — Egzekutor mod [subagent: mod-domain]
- [ ] `launcher/cli/ModCommands.h/.cpp` — per komenda:
      - `list`: `instance->loaderModList()` → `update()` → czekaj `parseFinished`; JSON z
        `Mod`+packwiz metadata (`Metadata::get`): name/slug/project_id/version_id/version_number/
        file/hash/enabled/date/source/release_type/lock_update
      - `search`: `ModrinthAPI::searchProjects` (paginacja po 25!); JSON wyniki
      - `info`: `getProjectTask` (slug lub ID) + wersje
      - `install`: resolve projektu → `ModrinthAPI::latestVersion` dla MC+loadera →
        `ResourceDownloadTask` (isIndexed=true, packwiz metadata domyślnie) w `ConcurrentTask`;
        deps: `GetModDependenciesTask` (off: `--no-deps`); po sukcesie usunąć stary plik
      - `remove`: resolve identyfikatora (filename→slug→projectId→name; niejednoznaczne → exit 5
        + kandydaci) → `Resource::destroy`; bez `update()` między usuwaniami
      - `enable/disable`: `Resource::enable(EnableAction)` (`.disabled`); `--recursive` po grafie zależności
      - `update`: EnsureMetadataTask → ModrinthCheckUpdate → (deps) → download; `--dry-run` z
        `CheckUpdateTask::getUpdates()`; lockUpdate pomijać bez `--force`
- [ ] Instancja uruchomiona → mutacje odmowa (exit 4) bez `--force`
- [ ] NIE wywoływać `startWatching()`; destruktory modeli domknięte przed wyjściem

### T5 — Dispatch headless [subagent: cli-core]
- [ ] `main.cpp`/`Application`: gdy `args.commands` zawiera wyłącznie komendy headless (mod/*,
      instance list) → `qputenv("QT_QPA_PLATFORM", "offscreen")` jeśli brak DISPLAY/WAYLAND,
      przed konstrukcją `Application`
- [ ] `processCommands`: `std::visit` — nowe komendy → `ModCommands::run()` → wynik →
      `Output::json/exit(code)`; porażka przerywa batch; Launch/Show w headless → exit 8

### T6 — Smoke testy E2E [subagent: mod-domain]
- [ ] Skrypt `tests/cli/spike-mod-smoke.sh`: utwórz testową instancję (ręcznie lub istniejącą),
      `mod search jei --json` → `mod install jei --yes` (deps!) → `mod list` (plik + metadata
      w mods/.index) → `mod disable jei` → `mod enable jei` → `mod update --dry-run` →
      `mod update all` → `mod remove jei`; asercje exit codes + JSON
- [ ] Uruchomić z `QT_QPA_PLATFORM=offscreen`

## Reguły spiKA
- Rzucalne, ale **nie łamiące legacy flags** ani istniejących testów.
- Wszystkie zmiany w małych commitach z prefiksem (`feat(cli):`, `chore(build):`).
- Żadnych zmian w GUI poza mechanics: rozszerzenia warian `Command`, dispatch, `ServersDat`-style
  ekstrakcje NIE wchodzą do spiKA.
- Po zielonym T6: decyzja „przekształcić w pełny plan (Faza 2+)" / iteracja spike.

## Status
- [x] Fork: https://github.com/kacperpaczos/PrismLauncher
- [x] Branch: `spike/mod-cli` (z 059625c8b)
- [x] T0 build green
- [ ] T1+T2+T3 cli-core
- [ ] T4 mod-domain
- [ ] T5 dispatch
- [ ] T6 smoke E2E
