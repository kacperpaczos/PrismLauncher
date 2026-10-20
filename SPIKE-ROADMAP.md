# SPIKE: CLI zarządzania modami — rozpiska

> Cel spiKA: **pełne zarządzanie modami z konsoli** — `prism mod list|search|info|install|remove|enable|disable|update`
> — działające headless (bez GUI, platforma offscreen), z wyjściem JSON i kodami wyjścia.
> Baza: gałąź `cli_rebirth` (PR #6193, Ryex), pin **`059625c8b89509fe5180384fd9db8432512e93a7`**.
> To jest SPIKE — poza zakresem: pełny split initCore/initGui, CurseForge.
> Pełny plan: sesja Plan (Faza 0–6).
>
> **Etap A (2026-10-20)** rozszerza spiKA o grupy `instance`, `settings` i `version`
> (zarządzanie instancjami, ustawieniami instancji i komponentami profilu).
>
> **Etap 2 (`spike/resources`, branch `kacperpaczos/spike-resources`)**: dołożenie grupy
> resource/shader/texture packów (`resource|shader|texture list|search|info|install|remove|enable|disable`)
> — szczegóły w sekcji „Etap 2 — resource / shader / texture packi".
>
> **Etap 3 (ten sam branch)**: dołożenie grup domenowych
> (`world|server|log|screenshot|notes`) — szczegóły w sekcji
> „Etap 3 — world / server / log / screenshot / notes".
>
> **Etap 4 (ten sam branch, 2026-10-20)**: IPC v2 — żądanie/odpowiedź przez gniazdo
> (`requestId`, odpowiedź tym samym gniazdem) i polecenie `status` — szczegóły w sekcji
> „Etap 4 — IPC v2: request/response + `status`".

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
  + pominięcie kreatora konfiguracji. Od Etapu 4 komendy headless wysłane do działającego GUI są
  **wykonywane w procesie GUI** przez IPC v2 (klient drukuje odesłany JSON i kończy się jego kodem
  wyjścia); `ipc_unsupported` + exit 8 zostaje tylko wtedy, gdy działa stary build launchera,
  który nie umie odpowiedzieć na żądanie z `requestId`.

## Etap 2 — resource / shader / texture packi (2026-10-19)

Stan: **zrobiony** (build zielony, ctest 28/28, smoke test E2E zielony).
Branch: `kacperpaczos/spike-resources` (baza: `spike/mod-cli` @ `e6b1adbf8`).

### Commity etapu 2

| commit | treść |
|---|---|
| `fix(build): support worktree checkouts in the spike build script` | `containers/spike-build.sh` — montowanie `.git` worktree'a (gitfile wskazuje na ścieżkę bezwzględną poza `/repo`) |
| `refactor(cli): share the resource helpers between the mod and pack commands` | `launcher/cli/CommandUtils.h/.cpp` — wspólne `sourceName`, `normalizedSlug`, `isoDate`, `resourceToJson`, `loadComponentProfile`, `waitForModel`, `fetchProject`, `fetchProjectVersions`, `matchTarget`, `resolveTargets` |
| `feat(cli): resource pack commands` | grupa `resource` |
| `feat(cli): shader pack commands` | grupa `shader` |
| `feat(cli): texture pack commands` | grupa `texture` |
| `test(spike): end-to-end smoke test for the pack commands` | `tests/cli/spike-pack-smoke.sh` |

### Zaimplementowana składnia grup packów

Trzy grupy (`resource`, `shader`, `texture`) mają identyczny zestaw podkomend:

```text
prismlauncher <grupa> list    -i <id> [--include-disabled] [--json]
prismlauncher <grupa> search  [query] [--mc <v>...] [--limit N] [--offset N] [--json]
prismlauncher <grupa> info    <project> [-i <id>] [--json]
prismlauncher <grupa> install <project> -i <id> [--version <v>] [--no-metadata] [-y|--yes] [--json]
prismlauncher <grupa> remove  <target>... -i <id> [--force] [-y|--yes] [--json]
prismlauncher <grupa> enable  <target>... -i <id> [-y|--yes] [--json]
prismlauncher <grupa> disable <target>... -i <id> [-y|--yes] [--json]
```

- `list` czyta model folderu instancji (`resourcepacks/`, `shaderpacks/`, `texturepacks/`).
- `search`/`info`/`install` idą do Modrinth (jak w `mod`); `search` ma paginację po 25.
- `install` zapisuje metadane packwiz (`.index/<slug>.pw.toml`), chyba że podano `--no-metadata`.
- `remove`/`enable`/`disable` rozwiązują cele w kolejności: nazwa pliku (z lub bez `.disabled`)
  → slug z metadanych → project id → nazwa wyświetlana; `--force` rozwiązuje niejednoznaczność
  do wszystkich dopasowań.
- Raport packa **nie** ma pola `lock_update` (ma je tylko raport moda). Dla resource packów
  dodatkowo `pack_format` (z `pack.mcmeta`).

### Decyzje zakresowe

- **Brak czasownika `update` dla packów.** `update` wymaga mechaniki `EnsureMetadataTask` +
  `ModrinthCheckUpdate` + wymiany pliku, a packi nie mają mechanizmu blokowania aktualizacji
  (`lockUpdate`) ani `affected_by`. Aktualizacja packa = `remove` + `install` nowej wersji,
  więc w etapie 2 świadomie jej nie ma (dla modów `mod update` istnieje).
- **Brak rozwiązywania zależności — tylko mody.** `GetModDependenciesTask` dotyczy wyłącznie
  modów (`mods/`); packi na Modrinth nie deklarują zależności w sposób, który dałoby się
  rozwiązać (resource/shader packi nie zależą od siebie), więc `install` instaluje dokładnie
  jeden projekt. Flagi `--no-deps` nie ma w komendach packów.
- **Texture packi to resource packi w Modrinth.** Modrinth nie ma osobnego typu projektu dla
  texture packów, więc grupa `texture` szuka i pobiera z typu `resourcepack`, a lokalnie
  zapisuje do `texturepacks/`.
- **Tylko Modrinth** — tak jak w etapie 1 (brak CurseForge, Flame, FTB).
- **Datapacki nadal poza zakresem** (choć są rodzajem `Resource` w launcherze).

### Smoke test

`tests/cli/spike-pack-smoke.sh` — 33 kroki (list → search → info → install → list → disable →
list → list --include-disabled → enable → info → remove → list + przypadki błędów; potem to samo
dla shaderów i texture packów). Wymaga sieci (realne API Modrinth), więc **nie jest w ctest**;
uruchamiany w kontenerze:

```sh
podman run --rm --security-opt label=disable -v "$PWD":/repo -w /repo -e HOME=/tmp \
    prism-spike-dev bash tests/cli/spike-pack-smoke.sh
```

Realne wyniki (przykład z ostatniego przebiegu, 2026-10-19, MC 1.21.1):

| grupa | projekt | wersja | zainstalowany plik |
|---|---|---|---|
| resource | `fresh-animations` (Fresh Animations) | 1.11.0 (`KYOqeb1P`) | `FreshAnimations_v1.11_1.20+.zip` |
| shader | `complementary-reimagined` (Complementary Shaders - Reimagined) | r5.9.3 (`Bqen1mJX`) | `ComplementaryReimagined_r5.9.3.zip` |
| texture | `cartoon-texture-pack` (Cartoon Texture Pack) | 1.0.11 (`KHKFUGdZ`) | `Cartoon Texture Pack.zip` |

Przebieg: 33/33 kroków zielonych (w tym kody wyjścia 3 dla „nie znaleziono" i „brak instancji").

### Testy jednostkowe

Nowe przypadki są w istniejących binariach testowych (liczba binariów ctest bez zmian — 28/28):

- `tests/CliParser_test.cpp` — 14 nowych przypadków: parsowanie trzech grup podkomend, walidacja
  `--limit`/`--offset`, odrzucanie obcego pozycjonalu, `--mc` nie połyka zapytania, niezależność grup.
- `tests/ApplicationMessage_test.cpp` — 5 nowych przypadków: round-trip `fromCliCommand`/`toCliCommand`
  dla komend packów.
- `tests/Regressions_test.cpp` — obsługa nowych wariantów komend w `toString` (test regresyjny
  porównujący sparsowane komendy).

## Etap 3 — world / server / log / screenshot / notes (2026-10-20)

Stan: **zrobiony** (build zielony, ctest 32/32, smoke test E2E zielony).
Branch: `kacperpaczos/spike-resources` (baza: `spike/mod-cli` @ `e6b1adbf8`).

### Commity etapu 3

| commit | treść |
|---|---|
| `refactor(cli): extract ServersDat from ServersPage` | `launcher/minecraft/ServersDat.{h,cpp}` — odczyt i zapis `servers.dat` wyciągnięte z `ServersPage`, używane przez GUI i przez CLI |
| `feat(cli): world commands` | grupa `world` |
| `feat(cli): server commands` | grupa `server` |
| `feat(cli): log commands` | grupa `log` |
| `feat(cli): screenshot commands` | grupa `screenshot` |
| `feat(cli): notes commands` | grupa `notes` |
| `refactor(cli): extract the reading of a log into a shared module` | `launcher/logs/LogReader.{h,cpp}` — czytanie logu wyciągnięte z komendy `log read`, żeby dało się je przetestować osobno |
| `fix(cli): reject a world whose level.dat cannot be read` | `world import` odrzuca plik, którego `level.dat` nie daje się przeczytać |
| `fix(cli): replace the notes of an instance when they are not appended` | `notes set` bez `--append` **zastępuje** notatki, a nie je dopisuje |
| `test(spike): tests for the domain commands` | `tests/LogReader_test.cpp`, `tests/World_test.cpp`, `tests/cli/spike-domain-smoke.sh` |

### Zaimplementowana składnia grup domenowych

```text
prismlauncher world list   -i <id> [--size] [--json]
prismlauncher world info   <folder|name> -i <id> [--size] [--json]
prismlauncher world copy   <folder|name> -i <id> [--name <name>] [--json]
prismlauncher world rename <folder|name> -i <id> --name <name> [--json]
prismlauncher world delete <folder|name> -i <id> [--permanent] [--json]
prismlauncher world import <zip|dir>    -i <id> [--name <name>] [--json]

prismlauncher server list   -i <id> [--ping] [--json]
prismlauncher server add    <name> <address> -i <id> [--index N]
                            [--accepts-textures ask|always|never] [--json]
prismlauncher server remove <index|name> -i <id> [--force] [--json]

prismlauncher log list   -i <id> [--json]
prismlauncher log read   -i <id> [--file <path>] [--head N] [--tail N]
                         [--grep <regex>] [--level <level>] [--json]
prismlauncher log upload -i <id> [--file <path>]
                         [--service mclogs|paste.gg|0x0.st|hastebin]
                         [--anonymize] [-y|--yes] [--json]

prismlauncher screenshot list   -i <id> [--json]
prismlauncher screenshot upload <id|all> -i <id> [--limit N] [-y|--yes] [--json]
prismlauncher screenshot copy   <id> <destination> -i <id> [--force] [--json]

prismlauncher notes show -i <id> [--json]
prismlauncher notes set  -i <id> [<text>] [--file <path>] [--append] [--json]
```

- Światy są adresowane **folderem** (stabilny identyfikator), nie pozycją na liście. `info`,
  `copy`, `rename` i `delete` przyjmują folder albo nazwę z `level.dat`.
- Metadane świata (`name`, `gameMode` + `gameModeRaw`, `seed`, `lastPlayed` w ISO-8601 UTC,
  `icon`, `warnings`) czytane są przez `World`; `sizeBytes` jest raportowane tylko z `--size`.
- `delete` przenosi świat do kosza, `--permanent` usuwa go od razu. `import` przyjmuje zip
  (także z nestowanym folderem świata) albo folder.
- `servers.dat` to **surowy NBT** (nie gzip) i jest obsługiwany wyłącznie przez
  `launcher/minecraft/ServersDat` — wspólny dla GUI i CLI. Uszkodzony plik nie jest nadpisywany
  (błąd parsowania → exit 6), brak pliku → utworzenie od zera.
- Serwery są adresowane **indeksem** (tylko one) albo nazwą. `--ping` dokłada liczbę graczy
  (albo `null`, gdy serwer nie odpowiada).
- `log read` wypisuje surowe linie na stdout (da się je potokować); z `--json` linie są
  obiektami `{n, level, text}`, a koperta mówi, czy log był ucięty. Poziom linii zgaduje się
  tak samo jak w launcherze (`LogParser::guessLevel`).
- `log upload` i `screenshot upload` **publikują** zawartość, więc wymagają `--yes`.
- Notatki instancji żyją w kluczu `notes` pliku `instance.cfg` i są czytane oraz zapisywane
  **wyłącznie** przez `SettingsObject` (`instance->notes()` / `instance->setNotes()`), plik
  nigdy nie jest zapisywany na boku.

### Decyzje zakresowe

- **Osobny `ServersDat` zamiast kodu w `ServersPage`.** `servers.dat` to surowy NBT, a GUI
  miało własny parser — bez wydzielenia nie dałoby się użyć w CLI bez duplikacji. Klasa jest
  wspólna dla obu, więc GUI i CLI czytają oraz zapisują plik tak samo.
- **Ochrona uszkodzonego `servers.dat` przeniesiona do `ServersDat`.** GUI odrzucało plik,
  którego nie da się sparsować; CLI robi to samo (exit 6, bez zapisu). Plik nie istniejący jest
  tworzony od zera (`{servers: []}`), a zapis idzie przez `PSaveFile` (atomowo, z opcjonalnym
  `.bak`).
- **`level.dat` i `servers.dat` mają osobne helpery.** `level.dat` to NBT w gzipie
  (libnbtplusplus), `servers.dat` to surowy NBT — pomieszanie helperów daje błędne wyniki
  po cichu, więc nie są wspólne.
- **Adresowanie świata po folderze.** Folder świata jest stabilny (nazwa z `level.dat` może się
  zmienić przy `rename`), więc to on jest identyfikatorem świata w JSON-ie.
- **Usuwanie do kosza zamiast trwałego.** `world delete` domyślnie przenosi świat do kosza
  (`--permanent` usuwa), tak jak w GUI. Osobne wywołanie kosza może nie działać w kontenerze
  bez pulpitu — wtedy komenda mówi, żeby użyć `--permanent`.
- **Ograniczenie „instancja uruchomiona" jest nieskuteczne w osobnym procesie.** Sprawdzenie
  `instance->isRunning()` działa tylko wewnątrz procesu launchera — CLI to osobny proces, więc
  nie widzi uruchomionej instancji. To samo ograniczenie co w etapie 1 (mody).
- **`screenshot upload` ujawnia `deleteHash`.** GUI trzyma go tylko w pamięci, więc z CLI jest
  to jedyna szansa na usunięcie wgranego screenshota — hash jest raportowany w JSON-ie.
- **Wgrywanie wielu screenshotów tworzy album** (jak w GUI) — `ImgurAPI::makeAlbum`.

### Smoke test

`tests/cli/spike-domain-smoke.sh` — 42 kroki na fabrykowanej instancji (dwa światy, log,
screenshot): listowanie, info (po folderze i po nazwie), kopiowanie, zmiana nazwy, usuwanie
(do kosza i trwałe), import (zip z nestowanym folderem i zwykły folder), serwery (dodanie,
lista, usunięcie po indeksie i po nazwie), notatki (zapis, dopisanie, `--file`, brak pliku),
logi (lista, czytanie surowe i w JSON, `--tail`, `--level`, `--grep`, brak pliku) i screenshoty
(lista, kopiowanie, `--force`, brak pliku).

Test **nie potrzebuje sieci**, więc jest częścią ctest (`CliDomainSmoke`). Smoke testy modów
i packów zostają poza ctest — gadają z prawdziwym API Modrinth.

```sh
podman run --rm --security-opt label=disable -v "$PWD":/repo -w /repo -e HOME=/tmp \
    prism-spike-dev bash tests/cli/spike-domain-smoke.sh
```

Przebieg: 42/42 kroków zielonych (w tym kody wyjścia 3 dla „nie znaleziono" i „brak
instancji", 4 dla „plik istnieje").

### Testy jednostkowe

Dwa nowe binaria ctest (liczba binariów: 28 → 31, z smoke testem 32):

- `tests/LogReader_test.cpp` — czytanie logu: wszystkie linie, pierwsze linie, ostatnie linie,
  plik bez końca linii, plik większy niż blok, plik spakowany (gzip) — odczyt, ogon i początek.
- `tests/World_test.cpp` — metadane świata: nazwa, tryb gry, ziarno i czas ostatniej gry są
  czytane z `level.dat`; folder bez `level.dat` i ze zepsutym `level.dat` nie jest światem.

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
- [x] Etap A: `instance` (list, info, create, duplicate, rename, delete, icon)
- [x] Etap A: `settings` (list, get, set, unset)
- [x] Etap A: `version` (show, list-available, change-mc, install-loader, remove-loader, reinstall)
- [x] Etap 2: resource/shader/texture packi — branch `kacperpaczos/spike-resources`
- [x] Etap 2: build green + ctest 28/28
- [x] Etap 2: smoke E2E packów (33/33 kroków)
- [x] Etap 3: world/server/log/screenshot/notes — branch `kacperpaczos/spike-resources`
- [x] Etap 3: build green + ctest 32/32
- [x] Etap 3: smoke E2E domen (42/42 kroków, w ctest jako `CliDomainSmoke`)
- [x] Merge: pełny CLI na `spike/mod-cli` (Etap A + Etap 2 + Etap 3), scalone z
      `origin/kacperpaczos/spike-resources` — wszystkie 12 grup komend w jednej binarki
- [x] Etap 4: IPC v2 — `requestId` + koperta odpowiedzi, wykonanie komend CLI w działającym GUI
- [x] Etap 4: polecenie `status` (GUI odpowiada przez IPC; headless: `running: null`, W1)
- [x] Etap 4: build green + ctest 33/33
- [x] Etap 4: smoke E2E IPC (start GUI offscreen, odpowiedzi przez gniazdo, K1 — w ctest jako `CliIpcSmoke`)

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
  w logu): **25 wywołań `api.modrinth.com`** (6 pobrań plików z CDN to osobne zapytania, plus
  zapytania do `i18n.prismlauncher.org` — aktualizacja tłumaczeń przy każdym uruchomieniu
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

### Ochrona uruchomionej instancji (W1 — świadomie bez zmian)
Brief C6 wymaga, żeby mutacja na uruchomionej instancji kończyła się kodem 4. W procesie
CLI **ta gałąź jest nieosiągalna**: `isRunning()` żyje w procesie GUI, a mod przez IPC do
działającego launchera zwraca `ipc_unsupported` + exit 8 (gałąź T5, poza zakresem tego sprawozdania).
Guard został zaimplementowany zgodnie z briefem (`MinecraftInstance::isRunning()` → `Conflict(4)`,
z `--force`), ale w headless zmianie nie zostanie wywołany — przyjęte i zaakceptowane w ramach spiKA
(gałąź do rozważenia w fazie 2+, gdy komendy mod będą działały w procesie launchera).

> **Aktualizacja (Etap 4, 2026-10-20):** gałąź „komendy mod działają w procesie launchera"
> zrealizowana — patrz sekcja „Etap 4 — IPC v2: request/response + `status`". Guard jest
> teraz aktywny, gdy komenda zostaje wykonana przez działające GUI.

### Świadome odstępstwa od briefu (uzgodnione w review)
- `Cli::outputError` ma dodatkowy overload z `QJsonObject` — bez niego nie da się dołożyć listy
  `failed`/`candidates` do koperty błędu, której brief wymaga („multiple matches → Ambiguous(5) +
  candidates array"). API jest tylko rozszerzone, nic nie zmienia się.
- **Retry przy błędzie sieci** — zadania sieciowe w GUI pytają użytkownika, czy ponowić
  żądanie (dialog `NetworkJobFailedDialog`). W headless dialogu nie da się odpowiedzieć, więc
  zadania CLI nigdy o to nie pytają (`makeCliNetJob()`), a `NetJob` nie otwiera dialogu w procesie
  headless (`Application::isHeadlessRun()`). Bez tego `mod info` nieistniejącego projektu trwał
  180 s, a `mod search` przeciwko niedziałającemu API nigdy się nie kończył.
- **`mod install --version`** filtruje wersję tak samo jak wersję wybraną bez `--version` — musi
  wspierać wersję Minecraft i loadera instancji (w inny sposób jar z NeoForge trafił do instancji
  Fabric). Wiele dopasowań → `ambiguous_target` (exit 5), brak → `version_not_found` (exit 3).
- **`mod info` pobiera zespół projektu** — odpowiedź `/project/{id}` nie zawiera autora, tylko id
  zespołu, więc członkowie zespołu są pobierani z `/project/{id}/members` (dodatkowe zapytanie, gdy
  projekt nie ma autora).
- **`mod update` z nieudanym sprawdzeniem** kończy się kodem 7 (`check_failed`), niezależnie od tego
  czy samo pobieranie się powiodło.
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
- Każde uruchomienie CLI sprawdza aktualizacje tłumaczeń (26 zapytań do
  `i18n.prismlauncher.org` w smoke teście) — to zachowanie launchera, nie mod-komend; w wersji
  headless warto je pominąć.
- Mod, dla którego nie znaleziono wersji dla loadera instancji (np. mod tylko na Forge w
  instancji Fabric), trafia do `failed` z komunikatem API (nie do `up_to_date`).

### Ograniczenia
- Tylko Modrinth. Flame/FTB/legacy nie są obsługiwane (`source` z metadanych Flame jest czytane,
  ale instalowanie przez CLI nie).
- Moduły (`coreModList`, `nilModList`) i pozostałe typy zasobów (resource packi, shader packi,
  datapacki, texture packi) nie są obsługiwane — tylko `mods/` instancji.
  *(To stan etapu 1 — w etapie 2 resource/shader/texture packi są już obsługiwane; datapacki nadal nie.)*
- `mod search` nie ma sortowania ani filtru po stronie — używa domyślnego sortowania Modrinth.
- Brak `mod install --version` po `version_number` dla wersji z różnym `fileId` (match po `version_id`,
  `version_number` albo `name`).

## Etap A — instancje, ustawienia, wersje (2026-10-20)

Pełna obsługa instancji z konsoli: `instance`, `settings` i `version`. Wzorzec
(`Commands.h` → `Parser.cpp` → `*Commands.cpp` → JSON przez `Output`) skopiowany
z grupy `mod`.

### Commity
| commit | treść |
|---|---|
| `feat(cli): instance list and info` | `list` + `info` (z poprzedniego przebiegu) |
| `feat(cli): instance create, duplicate, rename, delete and icon` | `create` (vanilla + `--from`), `duplicate`, `rename`, `delete`, `icon set` |
| `fix(cli): do not start a task that is already running` | `runTaskSync` nie woła `start()` na zadaniu, które launcher już uruchomił |
| `feat(cli): settings get, set, unset and list` | kurated schema ustawień instancji |
| `feat(cli): version show, list-available and the loader commands` | komponenty instancji, metadane, zmiana MC, loadery |
| `fix(cli): look for the jar of Minecraft in the cache of the launcher` | weryfikacja pobranych plików szukała złej ścieżki |
| `test(spike): end-to-end instance, settings and version smoke test` | `tests/cli/spike-instance-smoke.sh` |

### Zaimplementowana składnia

```
prismlauncher instance list    [--group <g>]
prismlauncher instance info    <id>
prismlauncher instance create  --name <n> --mc <ver> [--loader fabric|forge|neoforge|quilt|none]
                               [--loader-version <v>|latest] [--group <g>] [--icon <key|file>]
                               [--download|--no-download]
prismlauncher instance create  --from <url|file>
prismlauncher instance duplicate <id> [--name <n>] [--copy|--links|--hardlinks|--clone]
                               [--no-saves|--no-mods|--no-resource-packs|--no-shader-packs|--no-options|--no-configs]
prismlauncher instance rename  <id> <new-name> [--keep-folder|--rename-folder]
prismlauncher instance delete  <id> [--purge]
prismlauncher instance icon    set <id> <key|file>

prismlauncher settings list    <id> [--all|--modified]
prismlauncher settings get     <id> <key>...
prismlauncher settings set     <id> <key>=<value>...
prismlauncher settings unset   <id> <key>...

prismlauncher version show     <id>
prismlauncher version list-available [--mc] [--loader fabric|forge|neoforge|quilt]
                                      [--mc-version <v>] [--type release|snapshot|all]
prismlauncher version change-mc <id> <version>
prismlauncher version install-loader <id> <loader> <version|latest>
                                    [--force-conflict keep|disable|replace]
prismlauncher version remove-loader <id> <loader>
prismlauncher version reinstall <id>
```

### Kształt JSON-a (przykłady realne)

- `instance list` → `{"ok":true,"total":1,"instances":[…]}`
  - instancja: `{"id":"spike-test","name":"spike-test","path":"…","group":"","mc_version":"1.21.1",
    "loader":"fabric","loader_uid":"net.fabricmc.fabric-loader","loader_version":"0.20.1",
    "icon_key":"default","last_played":null,"total_time_played":0,"managed_pack":null,"running":null}`
- `instance info` → `{"ok":true,"instance":{…,"game_root":"…","instance_root":"…","mods_root":"…",
  "uuid":"…","instance_type":"OneSix","components":[…]}}`
- `instance create` → `{"ok":true,"id":"…","name":"…","path":"…","mc_version":"1.21.1",
  "loader":"fabric","loader_version":"0.20.1","downloaded":true}`
- `instance rename` → `{"ok":true,"old_id":"…","new_id":"…","old_name":"…","name":"…","folder_renamed":true}`
- `instance delete` → `{"ok":true,"id":"…","path":"…","method":"trash|delete","purged":false}`
- `settings list|get|set|unset` → `{"ok":true,"instance":"…","total":N,"settings":[…]}`
  - ustawienie: `{"key":"memory.max","value":8192,"type":"int","source":"instance|global|default",
    "overridden":true,"ini_key":"MaxMemAlloc","gate":"OverrideMemory"}`
- `version show` → `{"ok":true,"instance":"…","mc_version":"1.21.1","loader":"fabric","loader_version":"0.20.1",
  "total":4,"components":[…]}`
  - komponent: `{"uid":"net.minecraft","name":"Minecraft","version":"1.21.1","important":true,
    "enabled":true,"custom":false,"removable":false,"version_changeable":true}`
- `version list-available` → `{"ok":true,"uid":"net.minecraft","type":"release","total":103,
  "versions":[{"version":"1.21.1","type":"release","date":"…"}]}`
- `version install-loader` → `{"ok":true,"loader":"quilt","uid":"org.quiltmc.quilt-loader",
  "loader_version":"0.31.0-beta.4","mc_version":"1.21.1","conflicts":[{"uid":"…","action":"disabled|kept|removed"}]}`
- błędy → `{"ok":false,"error":{"code":"…","message":"…"}}` (+ `known_keys` dla `unknown_setting`)

Kody wyjścia: 0 ok, 2 użycie (nieznany klucz ustawienia, nieznany loader, zły typ wartości,
min>max RAM), 3 not-found (instancja, wersja MC, wersja loadera, ikona, loader w instancji),
4 konflikt (weryfikacja pobranych plików), 5 niejednoznaczność / `not_removable`, 7 sieć.

### Odstępstwa od briefu

- **`settings unset` resetuje też flagę grupy.** Brief: „`unset` = `settings()->reset(key)` (+ gate reset)".
  Reset flagi oznacza, że cała grupa (np. `OverrideMemory`) wraca do wartości globalnych — nie
  tylko resetowany klucz. Resetowanie samej wartości nie dałoby efektu, bo `OverrideSetting::get()`
  i tak zwraca wartość globalną, gdy flaga jest wyłączona.
- **Launcher zapisuje tylko wartości, które się zmieniły** (`Setting::set` emituje sygnał tylko
  przy zmianie). Ustawienie wartości równej globalnej nie zapisuje się więc w `instance.cfg` —
  dokładnie tak, jak w widoku ustawień GUI. Wartość i tak jest efektywna, ale przy późniejszej
  zmianie globalnej instancja pójdzie za nią. Testy używają wartości różnych od domyślnych.
- **`version remove-loader` na loaderze nieusuwalnym → exit 5** (zgodnie z briefem), choć kod 5
  w ogólnym schemacie oznacza „niejednoznaczność"; komunikat ma jasny kod `not_removable`.
- **`version install-loader` z `latest`** bierze najnowszą wersję loadera, która obsługuje wersję
  Minecraft *instancji* (a nie najnowszą w ogóle) — Fabric i Quilt nie mają w metadanych wymagania
  wersji MC, więc dla nich filtr to „MC >= 1.14".
- **`instance create --from` używa `InstanceImportTask`** (z `setConfirmUpdate(false)`), a nie
  `ModrinthInstanceCreationTask` — `InstanceImportTask` obsługuje zarówno pliki, jak i adresy URL
  (Modrinth, CurseForge, packi spoza społeczności) i nie wymaga połączenia z API.
- **`settings` ma kurated schema** (34 klucze). `instance.cfg` ma ich więcej; pozostałe są widoczne
  tylko z `--all` (20 dodatkowych kluczy). Sterowanie kluczami spoza schematu (np. `ExportName`)
  nie jest obsługiwane.
- **Zmienne środowiskowe** (`Env`) to jeden JSON-owy obiekt w `instance.cfg`, więc w CLI ma swoją
  składnię: `env.NAME=value` (get/unset po `env.NAME`).
- **`version change-mc` resetuje `UseLatestMinecraftVersion`** i (gdy globalne `AutomaticJavaSwitch`
  i instancyjne `AutomaticJava` są włączone) `OverrideJavaLocation` — dokładnie jak strona wersji
  w GUI.
- **Weryfikacja pobranych plików** szuka jar-a Minecraft w cache launchera (`libraries/`), a nie w
  katalogu bibliotek instancji — tam go nie ma (błąd znaleziony i naprawiony w trakcie implementacji).
- **`runTaskSync` nie uruchamia zadania drugi raz.** `PackProfile::resolve()` już startuje zadanie,
  a `Task::start()` na uruchomionym zadaniu kończy proces asercją `ASSERT: (isRunning()) == false`.

### Znane ograniczenia

- **Ochrona uruchomionej instancji jest nieosiągalna** — tak samo jak w komendach `mod`: `isRunning()`
  żyje w procesie GUI, a CLI to osobny proces. Guard jest zaimplementowany (`instance_running`,
  exit 4), ale nie zostanie wywołany. *(Aktualizacja, Etap 4: przez IPC do działającego GUI —
  jest wywoływany; patrz sekcja „Etap 4".)*
- **`settings` nie obejmuje ustawień paczki** (packwiz), globalnych ustawień launchera ani
  ustawień konta — tylko ustawienia instancji.
- **`version change-mc` i `version install-loader` potrzebują sieci** — bez niej kończą się kodem 7.
  `version show` i `settings` działają bez sieci (rozwiązywanie komponentów działa offline, ale
  wersje komponentów wtedy pochodzą z profilu, a nie z metadanych).
- **Rozwiązywanie komponentów offline kończy się błędem** w logu (`ComponentUpdateTask failed`),
  bo metadane nie są dostępne — to zachowanie launchera, nie komendy.
- **`instance delete` używa kosza**, jeśli jest dostępny (na maszynie bez pulpitu pliki są usuwane).
- **`instance rename` odmawia**, gdy instancja ma powiązane instancje — GUI pyta wtedy użytkownika,
  a proces headless nie może zadać pytania.
- **`version show` nie zgłasza problemów komponentów** (`Component::getProblems`) — tylko ich stan.

### Ograniczenia (dalsze)
- `version list-available` bez `--loader` listuje wersje Minecraft; wersje snapshot są odfiltrowane
  flagą `--type` (domyślnie `all`).
- Loadery `liteloader` nie są obsługiwane przez CLI (launcher zna tylko `fabric`, `forge`,
  `neoforge`, `quilt`).

## Etap 4 — IPC v2: request/response + `status` (2026-10-20)

Stan: **zrobiony** (build zielony, ctest 33/33, smoke E2E IPC zielony, w ctest jako `CliIpcSmoke`).
Branch: `spike/mod-cli`. Ostatni brakujący element pełnego CLI: komendy CLI działają też wtedy,
gdy launcher GUI już biega, a `status` eksponuje stan launchera.

### Model interakcji

- **Headless-first** — bez działającego launchera każda komenda CLI biega w swoim procesie
  (offscreen, bez kreatora konfiguracji), dokładnie jak przed Etapem 4.
- **IPC, gdy GUI biega** — klient wykrywa żywego peera (`LocalPeer::isClient()`) i wysyła żądanie
  zamiast samego przekazu: `ApplicationMessage::request(cmd, requestId)`, gdzie `requestId`
  to UUID (bez nawiasów). Peer potwierdza zrozumienie ramką `Accepted`, wykonuje komendę
  i odsyła **tym samym gniazdem** ramkę `Response`: `{requestId, ok, exitCode, data|error}`
  (+ `rawOutput`, gdy komenda pisała surowy tekst, oraz `errorCode`/`errorMessage` w kopercie
  błędu). Klient drukuje otrzymany JSON na swoje stdout i kończy się otrzymanym kodem wyjścia.
- **Timeouty** — klient czeka na odpowiedź `--ipc-timeout` sekund (domyślnie **300 s**, bo
  `mod update` może pobierać długo). Brak odpowiedzi w czasie → koperta `ipc_timeout` + exit 8
  (`GuiRequired`). Odpowiedź „nic" od starego builda launchera (protokół v1: zamknięcie gniazda bez
  odpowiedzi) → `ipc_unsupported` + exit 8 — jedyny pozostały przypadek tego kodu.
- **K1 nienaruszony** — strona odbierająca NIGDY nie woła `QCoreApplication::exit`; o wyjściu
  headless decyduje wyłącznie `m_headlessRun`. GUI przeżywa obsługę żądań (też nieudanych) —
  sprawdzane w smoke teście po każdej komendzie.
- **Przechwytywanie wyjścia (sink)** — komendy wykonywane w procesie GUI nie mogą pisać na jego
  terminal. Wokół `processCommands` instalowany jest `Cli::OutputCapture` (`Output::setOutputSink`),
  który zbiera dokumenty JSON (i surowy tekst, z limitem rozmiaru, żeby ogromny `log read`
  nie spułkował ramki odpowiedzi); ostatni dokument wraca w `data`. Ścieżka headless dalej pisze
  na realne stdout.
- **Kompatybilność wstecz** — wiadomości bez `requestId` (stary klient / GUI) lecą starym torem
  (`processCommands` bez odpowiedzi); nowe pola parsowane tolerancyjnie, stare GUI ignoruje
  nieznane typy wiadomości.

### `status`

```
prismlauncher status [--json]
```

- GUI biega: stan przez IPC — `{version, dataPath, instances: [{id, name, running}],
  accounts: [{id, name, type}]}`; `running` jest realnym stanem procesów instancji.
- Headless: te same dane zbierane lokalnie; `running: null` (JSON `null`) — osobny proces CLI
  nie widzi procesów instancji, więc stan jest **nieznany** (W1), a nie fałszywy.

### Commity etapu 4

| commit | treść |
|---|---|
| `feat(ipc): request id and response envelope` | `ApplicationMessage`: `type` (`Request`/`Accepted`/`Response`), `requestId`, `ok`, `exitCode`, `rawOutput`, `errorCode`/`errorMessage`; fabryki `request`/`accepted`/`response` |
| `feat(ipc): execute cli commands in the running gui` | `LocalPeer::sendRequest` (ramki na tym samym gnieździe, ack, deadline), `Cli::Ipc` (`sendCommandToPeer`), `OutputCapture` (sink), dispatch w `Application::messageReceived` → `answerRequest`, `--ipc-timeout` |
| `fix(ipc): do not touch the peer when its sockets are deleted` | racy shutdown peera przy zamykaniu GUI |
| `feat(cli): status command` | `StatusCommands` + parsowanie `status [--json]` + serializacja przez IPC |
| `test(ipc): cover request id, error envelope and raw output round-trips` | 7 testów `ApplicationMessage`, 5 testów `CliParser` (status, ipc-timeout) |
| `test(ipc): end-to-end smoke of the request/response IPC` | `tests/cli/spike-ipc-smoke.sh` + rejestracja `CliIpcSmoke` w ctest |

W trakcie etapu wpadły też porządki poprzednich przebiegów (refaktor `resolveInstance` do
`CommandUtils`, usunięcie martwego kodu, styl) i dwie poprawki: notes ze stdin odmawia przez IPC
(proces GUI nie ma terminala klienta) oraz nazwa sterownika zink nadpisana przebiegiem clang-tidy.

### Testy

- `ApplicationMessage` (7 nowych): `requestId` w żądaniu, round-trip `Accepted`/`Response`,
  błędy w kopercie, `rawOutput`, `status` przez IPC, stare wiadomości bez odpowiedzi.
- `CliParser` (5 nowych): `status`, `status --json`, `status` nie miesza się z innymi komendami,
  `--ipc-timeout` parsowanie, domyślny timeout 300 s.
- `CliIpcSmoke` (ctest, 13 kroków, bez sieci): start GUI offscreen w rzucalnym katalogu danych,
  potem `status`, `instance list`, `notes show/set` (launcher realnie zapisuje plik),
  komenda na nieistniejącej instancji (błąd przez IPC, kod 3), kontrolki K1 po każdej grupie,
  na koniec status/instancje po ubiciu GUI (ścieżka headless, `running: null`).

### Znane ograniczenia (Etap 4)

- **`status` headless raportuje `running: null`** — świadome (W1): CLI to osobny proces; stan
  procesów instancji zna tylko GUI. Rozróżnienie „false"/„null" pozwala klientom nie mylić
  „na pewno nie biega" z „nie wiem".
- **Jeden żądanie na gniazdo** — klient wysyła komendę i czeka; nie ma multipleksowania ani
  anulowania w trakcie (przerwanie Ctrl+C u klienta zostawia GUI wykonywać komendę dalej).
- **`notes set` bez `--text`/`--file` (stdin) przez IPC → `notes_not_read`** — czytanie stdin
  zablokowałoby pętlę zdarzeń GUI, więc komenda wykonana w procesie launchera tego odmawia
  (`allowStdin = isHeadlessRun()`); ścieżka headless czyta stdin jak przed Etapem 4.
- **`ipc_timeout` nie cofa skutków** — jeśli GUI odpowiedzi po limicie, komenda i tak została
  wykonana; klient tylko nie poznał wyniku.
