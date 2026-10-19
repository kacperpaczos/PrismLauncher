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
- [x] `launcher/cli/Output.h/.cpp`: `outputJson(doc)` / `outputError(code, msg)` — koperta
      `{"ok":true,…}` / `{"ok":false,"error":{"code":"…","message":"…"}}`,
      `QTextStream(stdout)` UTF-8 + flush
- [x] Exit codes (`Cli::ExitCode` w `Output.h`): 0 ok; 1 ogólny; 2 użycie; 3 not-found;
      4 konflikt (instance running, brak --force); 5 niejednoznaczność; 6 błąd parsowania;
      7 sieć; 8 wymaga GUI
- [x] Testy jednostkowe Output (do istniejącej struktury testów)

### T2 — Struktury komend + parser [subagent: cli-core]
- [x] `launcher/cli/Commands.h`: struktury `Cmd::ModList{instance, includeDisabled}`,
      `Cmd::ModSearch{query, mcVersions, loaders, limit, offset}`,
      `Cmd::ModInfo{project, instance}`, `Cmd::ModInstall{project, instance, version, noDeps,
      noMetadata}`, `Cmd::ModRemove{targets, instance, force}`, `Cmd::ModEnable/Disable{targets,
      instance, recursive}`, `Cmd::ModUpdate{targets|all, instance, dryRun, noDeps, force}`
      — rozszerzyć `CommandBase` variant; styl: `operator<=> = default` jak istniejące
- [x] `launcher/cli/Parser.cpp`: grupa subkomend `mod` (CLI11: `app.require_subcommand()` na
      poziomie mod), wspólne flagi `-i/--instance`, `--json`, `-y/--yes`, `--force`;
      legacy flags nietknięte
- [x] Rozszerzyć `ApplicationMessage::fromCliCommand/toCliCommand` o nowe komendy (spójność CLI/IPC)

### T3 — runTaskSync [subagent: cli-core]
- [x] `launcher/cli/TaskRunner.h/.cpp`: `Result<> runTaskSync(Task::Ptr, timeoutMs)` —
      lokalna `QEventLoop` + `Task::finished` + timeout (wzorzec: `FlameAPI::getModFileChangelog`,
      `Meta::Index::loadVersion`); raport postępu 1 linia na stderr

### T4 — Egzekutor mod [subagent: mod-domain]
- [x] `launcher/cli/ModCommands.h/.cpp` — per komenda:
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
- [x] Instancja uruchomiona → mutacje odmowa (exit 4) bez `--force`
- [x] NIE wywoływać `startWatching()`; destruktory modeli domknięte przed wyjściem

### T5 — Dispatch headless [subagent: cli-core]
- [x] `main.cpp`/`Application`: gdy `args.commands` zawiera wyłącznie komendy headless (mod/*,
      instance list) → `qputenv("QT_QPA_PLATFORM", "offscreen")` jeśli brak DISPLAY/WAYLAND,
      przed konstrukcją `Application` (+ pominięcie kreatora konfiguracji)
- [x] `processCommands`: `std::visit` — nowe komendy → `ModCommands::run()` → wynik →
      `Output::json/exit(code)`; porażka przerywa batch; Launch/Show w headless → exit 8

### T6 — Smoke testy E2E [subagent: mod-domain]
- [x] Skrypt `tests/cli/spike-mod-smoke.sh`: utwórz testową instancję (ręcznie lub istniejącą),
      `mod search jei --json` → `mod install jei --yes` (deps!) → `mod list` (plik + metadata
      w mods/.index) → `mod disable jei` → `mod enable jei` → `mod update --dry-run` →
      `mod update all` → `mod remove jei`; asercje exit codes + JSON
- [x] Uruchomić z `QT_QPA_PLATFORM=offscreen`

## Zaimplementowana składnia `mod` (stan po T1–T5 — T4/T6 muszą się zgadzać)

```
prismlauncher mod list    -i <id> [--include-disabled] [--json]
prismlauncher mod search  [query] [--mc <v>...] [--loader <l>...] [--limit N] [--offset N] [--json]
prismlauncher mod info    <project> [-i <id>] [--json]
prismlauncher mod install <project> -i <id> [--version <v>] [--no-deps] [--no-metadata] [-y|--yes] [--json]
prismlauncher mod remove  [target...] -i <id> [--force] [-y|--yes] [--json]
prismlauncher mod enable  [target...] -i <id> [--recursive] [-y|--yes] [--json]
prismlauncher mod disable [target...] -i <id> [--recursive] [-y|--yes] [--json]
prismlauncher mod update  [target...] [--all] -i <id> [--dry-run] [--no-deps] [--force] [-y|--yes] [--json]
```

- `-y/--yes` jest przyjmowane, ale nie trafia do struktur `Cmd::Mod*` (headless i tak nie pyta).
- `Cmd::ModRemove` ma `force` (nie `yes`); `--json` domyślnie włączone (flaga akceptowana, wyjście zawsze JSON).
- Gdy wszystkie komendy są headless i brak `DISPLAY`/`WAYLAND_DISPLAY` → `QT_QPA_PLATFORM=offscreen`
  + pominięcie kreatora konfiguracji; mod przez IPC do działającego GUI → `ipc_unsupported` + exit 8 (TODO).

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
- [x] T1+T2+T3 cli-core
- [x] T4 mod-domain
- [x] T5 dispatch
- [x] T6 smoke E2E

## Review round 1 (MiniMax): K1-K3 fixed
- **K1** `fix(cli): headless exit scoped to startup commands` — decyzja „headless i wyjdź" zapada raz,
  w konstruktorze `Application` (`m_headlessRun = Cli::allCommandsHeadless(args.commands)`);
  `processCommands` (także z IPC/`messageReceived`) nie kończy już działającego GUI; mixed startup
  (headless + Launch/URI) wykonuje komendy bez exit (warning na stderr), a `performMainStartupAction`
  blokuje okno tylko przy `m_headlessRun`.
- **K2** `fix(cli): json envelope for parse errors` — błędy parsowania CLI11 → koperta
  `{"ok":false,"error":{"code":"parse_error","message":"…"}}` na stdout + exit 2; `--help`/`--version`
  bez zmian (exit 0, wyjście CLI11). Test `CliParser` uruchamia realny binarny launcher (QProcess).
- **K3** `fix(cli): application context for mod command runners` — `Cli::ModContext { Application& app; }`
  + `runModCommand(ModContext&, const Cmd::ModX&)`; `ModCommands.h` bez include'ów GUI (forward-decl
  `class Application;`), kontekst budowany raz w `processCommands`.
- **Nity** `fix(cli): review nits` — usunięty nieużywany `#include <QByteArray>`; `qputenv` offscreen
  respektuje jawne `QT_QPA_PLATFORM` (W7); `outputJson` odrzuca nie-obiektowe dokumenty.
- Build + `ctest`: 27/27 zielone. `mod list` (bez `-i`) → exit 2 + JSON; `--help`/`--version` → exit 0.
- Pozostałe uwagi z review (W1-W9 poza W7 i „drobnica" przy `outputJson`) są funkcjonalne i wchodzą
  w zakres T4 (mod-domain) — nie dotykane w tym przebiegu.


## T4+T6 — implementacja (2026-10-19)

### Commity
| commit | treść |
|---|---|
| `feat(cli): mod list and info commands` | `list` + `info` + `Cli::outputError` z dodatkowymi polami |
| `feat(cli): mod search via modrinth` | `search` z paginacją po 25 |
| `feat(cli): mod install with dependency resolution` | `install` + `GetModDependenciesTask` |
| `feat(cli): mod remove, enable, disable` | `remove`/`enable`/`disable` + `--recursive` |
| `feat(cli): mod update with metadata ensure and dry-run` | `update` + `EnsureMetadataTask` + `ModrinthCheckUpdate` |
| `fix(cli): review fixes for the mod commands` | zależności `mod update`, `checkFailed`, `--force` w `remove`, `affected_by`, `--no-metadata`, walidacja loadera, koperty błędów |
| `test(spike): end-to-end mod management smoke test` | `tests/cli/spike-mod-smoke.sh` |

### Kształt JSON-a (przykłady realne, 1.21.1 + fabric)
- `mod list` → `{"ok":true,"instance":"spike-test","total":3,"mods":[…]}`
  - mod: `{"name":"Just Enough Items","slug":"jei","project_id":"u6dRKJwZ","version_id":"XNRMMwd2",
    "version_number":"19.69.0.456","file":"jei-1.21.1-fabric-19.69.0.456.jar",
    "hash":{"sha1":null,"sha512":"47d8…"},"enabled":true,"date":"2026-10-19T17:27:23Z",
    "source":"modrinth","release_type":"Beta","lock_update":false}`
- `mod search` → `{"ok":true,"offset":0,"limit":3,"total":32,"results":[…]}`
  - wynik: `{"slug":"jei","project_id":"u6dRKJwZ","name":"Just Enough Items (JEI)","description":"…",
    "authors":["mezz"],"website":"https://modrinth.com/mod/jei","icon_url":"https://cdn.modrinth.com/…",
    "downloads":84733031}`
- `mod info` → `{"ok":true,"project":{…},"versions":[…],"installed":{…}|null}`
  - wersja: `{"version_id":"XNRMMwd2","version_number":"19.69.0.456","name":"19.69.0.456",
    "release_type":"Beta","date":"…","file_name":"jei-1.21.1-fabric-19.69.0.456.jar",
    "mc_versions":["1.21.1",…],"loaders":["fabric"]}`
- `mod install` → `{"ok":true,"installed":[…],"dependencies":[…],"failed":[]}`
- `mod remove` → `{"ok":true,"removed":[{"name":…,"file":…,"slug":"jei"}],"failed":[]}`
- `mod enable`/`disable` → `{"ok":true,"changed":[{"slug":"jei","file":…,"enabled":false,"affected_by":[]}],"failed":[]}`
- `mod update` → `{"ok":true,"dry_run":false,"updated":[…],"up_to_date":[…],"skipped":[…],"failed":[],"dependencies":[…]}`
- błędy → `{"ok":false,"error":{"code":"instance_not_found","message":"…"}}` (+ opcjonalne pola: `failed`, `candidates`)

Kody wyjścia: 0 ok, 2 użycie (nieznany loader), 3 not-found (instancja/projekt/wersja/cel),
4 konflikt (instancja uruchomiona), 5 niejednoznaczny cel, 6 błąd parsowania odpowiedzi,
7 sieć / nieudane pobranie, 1 pozostałe błędy.

### Odstępstwa od planu
- **`search` używa surowej odpowiedzi API**, a nie sinku RPC z `ResourceAPI::searchProjectsTask`:
  tylko surowa odpowiedź niesie `total_hits` oraz `downloads` każdego projektu (oba pola
  są wymagane w wyjściu). Zapytania i parsowanie hitów są identyczne (`Modrinth::Parse::loadIndexedPack`),
  liczba wywołań API jest taka sama (1 na stronę).
- **Wersja instancji bierze się z `cachedVersion` komponentu**, nie z `version`. Launcher zapisuje
  `cachedVersion` dopiero po rozwiązaniu komponentów (to wymaga pobrania metadanych Mojang);
  `PackProfile::getComponentVersion()` zwraca właśnie `cachedVersion`. Headless nie ma etapu
  rozwiązywania komponentów, więc `PackProfile::reload(Net::Mode::Offline)` jest wołane, a wartość
  brana jest z komponentu `net.minecraft`. Bez `cachedVersion` w `mmc-pack.json` komenda `install`
  kończy się błędem `no_minecraft_version`.
- **Slug w JSON-ie jest normalizowany** — metadane wczytane przez model mają slug z sufiksem
  `.pw.toml` (`ResourceFolderLoadTask` podaje nazwę pliku indeksu), więc sufiks jest obcinany
  (`jei`, nie `jei.pw.toml`).
- **`mod install` nie ma `--force`** (składnia zamrożona), więc instalacja na uruchomionej
  instancji jest zawsze odrzucana (exit 4). `remove`/`update` mają `--force` i przepuszczają.
- **`mod enable`/`disable` na modzie już w stanie docelowym** nie jest błędem — komenda kończy się
  exit 0, a `changed` jest pustą listą.
- **`Cli::outputError` ma dodatkowy overload** z obiektem `QJsonObject` — pozwala dołożyć do koperty
  błędu listy `failed`/`candidates` (koperta sukcesu `outputJson` zawsze dostaje `ok:true`).
- **`mod update --no-deps`** — lista, którą zwraca `CheckUpdateTask::getDependencies()`, zawiera
  same aktualizowane mody (jest wejściem dla `GetModDependenciesTask`), więc zależności są
  rozwiązywane osobnym `GetModDependenciesTask` i raportowane w polu `dependencies`. Zależności
  nie są pobierane — aktualizacja podmienia tylko mody, które mają nowszą wersję. `--no-deps`
  pomija rozwiązywanie zależności.
- **`mod remove --force`** — zgodnie z opisem flagi usuwa też mody zablokowane (`lockUpdate`) i
  nie zgłasza błędu niejednoznaczności (usuwane są wszystkie dopasowania). Bez `--force` zablokowany
  mod jest odmawiany (exit 4, kod `locked`).
- **Smoke test nie jest w ctest** — wymaga sieci (realne API Modrinth). Uruchamiany ręcznie:
  `tests/cli/spike-mod-smoke.sh` (wewnątrz kontenera). Asercje exit code + JSON (python3, bez jq).

### Uwagi z review, które wchodzą w zakres T4
- **W9** — `mod` jest teraz podkomendą (`prismlauncher mod <sub>`). Bare `mod` (bez podkomendy)
  kończy się błędem parsowania CLI11 (`parse_error`, exit 2); `mod` nie ma już trybu „legacy”.
  Kompatybilność z `prismlauncher mod list` z T2–T5 zachowana.
- **W2** — semantyka timeoutu zadań: `Cli::runTaskSync` ma domyślny limit 300 s, ale komendy mod
  używają własnego limitu (180 s) na żądanie sieciowe; po przekroczeniu zadanie jest przerwane
  (`abort()`) i zwracany jest błąd `network_error` (exit 7). Nie ma jeszcze osobnego kodu błędu
  dla timeoutu — to kandydat do rozdzielenia w fazie 2+.
- **W1/W3/W4/W8** — hardening parsera (`--mc`/`--loader` nie połykają query, walidacja liczby
  celów, `Range` dla `--limit`/`--offset`, `fallthrough`) był z T2 i pozostaje w mocy.

### Uwagi z review kodu (2 osie: standardy + specyfikacja)
- **Budżet wywołań API smoke testu** — zmierzony empiretycznie (kategoria `launcher.task.net`
  w logu): **19 wywołań `api.modrinth.com`** (5 pobrań plików z CDN to osobne zapytania, plus
  26 zapytań do `i18n.prismlauncher.org` — aktualizacja tłumaczeń przy każdym uruchomieniu
  launchera). Limit z briefu (< 30 wywołań Modrinth) jest spełniony.
- **Cel, którego nie ma w instancji → exit 3** — brief C6 mówi „No match → `failed` entry +
  NotFound(3) at end", więc `remove`/`enable`/`disable`/`update` kończą się kodem 3, nawet jeśli
  część celów została rozwiązana (zmienione mody są raportowane w kopertcie błędu).
- **`dependentOn`** — zależności pobierane przez `mod install` podają w nagłówku API wersję moda,
  który ich wymaga (tak jak w GUI).
- **Nieznany loader w `mod search`** → błąd użycia (exit 2), a nie ciche pominięcie filtra.
- **Odpowiedź API, która nie jest JSON-em** → `network_error` (exit 7), zgodnie z briefem
  („Network/API failure → ExitCode::Network"), a nie osobny kod parsowania.
- **Konwencja nazw z `.clang-tidy`** — `GlobalConstantPrefix: "g_"` / `StaticConstantPrefix: "s_"`:
  stałe w `ModCommands.cpp` używają prefiksu `g_` (`g_SearchPageSize`, `g_ModelWaitMs`,
  `g_NetworkTimeoutMs`, `s_suffix`), a nie `k`/`static const` bez prefiksu. `clang-tidy-diff` w CI
  tego wymaga.
- **Powtórzony kod** — preamble (rozwiązanie instancji + model + oczekiwanie na skan) był skopiowany
  w pięciu komendach; wyciągnięty do `resolveModInstance()`, `openModModel()` i
  `checkInstanceRunning()`. Kształt JSON-a moda pobranego z metadanych był powtórzony w `mod install`
  — wyciągnięty do `downloadedModToJson()`.
- **Nazwy** — `resolveTarget()` (dopasowania jednego celu) przemianowane na `matchTarget()`, żeby
  odróżnić go od `resolveTargets()` (rozwiązanie listy celów).
- **`Signed-off-by` (DCO)** — `CONTRIBUTING.md` wymaga podpisu DCO, ale **żaden** commit na tej
  gałęzi (ani wcześniejsze T0–T5, ani te z T4/T6) go nie ma. Poprawki wymaga przepisanie historii
  (`git rebase --signoff`), a brief zabrania force-pusha — do zrobienia przed otwarciem PR-a.
- **Rozmiar `ModCommands.cpp`** — plik ma ~1,4 tys. linii i robi kilka rzeczy naraz (formatowanie
  JSON, paginacja Modrinth, zależności, skan modelu, pobieranie, sprawdzanie aktualizacji). W fazie 2+
  sensownie podzielić go na `ModCommands.cpp` + moduły per obszar.

### Świadome odstępstwa od briefu (uzgodnione w review)
- `Cli::outputError` ma dodatkowy overload z `QJsonObject` — bez niego nie da się dołożyć listy
  `failed`/`candidates` do koperty błędu, której brief wymaga („multiple matches → Ambiguous(5) +
  candidates array"). API jest tylko rozszerzone, nic nie zmienia się.
- `mod remove --force` usuwa też mody zablokowane (`lockUpdate`) i nie zgłasza błędu
  niejednoznaczności — flaga jest opisana w zamrożonej składni CLI jako „Remove even if the mod is
  locked or ambiguous", więc implementacja trzyma się jej opisu, a nie węższego czytania punktu C6.
- Dopasowanie celów jest dokładne (nazwa pliku, slug, project id, nazwa) — bez wariantów
  ignorujących wielkość liter, żeby literówka nie dawała „niejednoznaczności" zamiast
  „nie znaleziono".

### Znane ograniczenia (po review kodu)
- `waitForModel()` nie odróżnia nieudanego skanu modelu od udanego (`updateFinished` jest
  emitowane niezależnie od wyniku, a `onUpdateFailed()` w bazie jest pusty) — uszkodzony folder
  `mods/` jest raportowany jako pusta lista modów. Tak samo zachowuje się GUI.
- `Cli::outputJson()` (z T1) wypisuje kopertę błędu, ale nie może zmienić kodu wyjścia procesu —
  wywołujący zwraca `ExitCode::OK`. Do poprawki w fazie 2+.
- Każde uruchomienie CLI sprawdza aktualizacje tłumaczeń (23 zapytania do
  `i18n.prismlauncher.org` w smoke teście) — to zachowanie launchera, nie mod-komend; w wersji
  headless warto je pominąć.
- Mod, dla którego nie znaleziono wersji dla loadera instancji (np. mod tylko na Forge w
  instancji Fabric), trafia do `failed` z komunikatem API (nie do `up_to_date`).

### Ograniczenia
- Tylko Modrinth. Flame/FTB/legacy nie są obsługiwane (`source` z metadanych Flame jest czytane,
  ale instalowanie przez CLI nie).
- Moduły (`coreModList`, `nilModList`) i pozostałe typy zasobów (resource packi, shader packi,
  datapacki, texture packi) nie są obsługiwane — tylko `mods/` instancji.
- `mod search` nie ma sortowania ani filtru po stronie — używa domyślnego sortowania Modrinth.
- Brak `mod install --version` po `version_number` dla wersji z różnym `fileId` (match po `version_id`,
  `version_number` albo `name`).
