# Wznowienie prac nad Prism CLI

Stan zapisywany 2026-10-22 na polecenie użytkownika: zachować pracę lokalnie,
udokumentować recenzje i zamknąć agentów projektu. To checkpoint, nie deklaracja
gotowości produkcyjnej. W tej sesji nie wykonano push ani publikacji upstream.

## Wejście dla następnego agenta

1. Otwórz `/home/user/prism-cli-spike`, sprawdź `git status --short`,
   `git branch --show-current` i `git log -8 --oneline`. Oczekiwana gałąź:
   `spike/mod-cli`. Zachowaj ewentualne nowe zmiany użytkownika.
2. Przeczytaj ten plik, [recenzję Codexa](CODEX-REVIEW-2026-10-21.md),
   [rejestr rewizji](REVISION-LOG.md) oraz `CONTRIBUTING.md`.
3. Uzgodnij rolę z użytkownikiem przed uruchamianiem innych agentów. Użytkownik
   wskazał, że Claude miał być nadzorcą, a Codex wykonawcą/niezależnym recenzentem;
   doszło do pomylenia rozmów. Nie odtwarzaj automatycznie starej orkiestracji.
4. Buduj i testuj wyłącznie przez wrapper kontenerowy:
   `./containers/spike-build.sh build`, potem `./containers/spike-build.sh test`.
   Host nie ma wymaganego `javac`. Testy mod/pack/instance korzystające z API
   traktuj osobno od offline CTest; zapisuj dokładny SHA i polecenia weryfikacji.
5. Pracuj na konkretnym otwartym findingu. Dla każdej poprawki zapisz: reprodukcję,
   zmianę, test, SHA oraz wynik niezależnej recenzji, jeśli faktycznie się odbyła.
   Sam commit lub zielony test nie oznacza akceptacji niezależnego recenzenta.

## Projekt i baza

Fork `kacperpaczos/PrismLauncher`: C++/Qt6 launcher z komendami mod, resource,
shader, texture, instance, settings, version, world, server, log, screenshot,
notes i status. JSON, kody wyjścia, wykonanie headless i request/response IPC.
Główne pliki: `launcher/cli/`, `launcher/Application.cpp`,
`launcher/ApplicationMessage.*`, `launcher/InstanceTask.*`,
`launcher/minecraft/ServersDat.*`, `libraries/LocalPeer/` i `tests/`.

Spike oparto na niestabilnej gałęzi PR #6193 (`cli_rebirth`), historyczny pin
`059625c8b89509fe5180384fd9db8432512e93a7`. Ryex ostrzegł o wielokrotnych rebase'ach.
Nie aktualizuj bazy ani nie publikuj zmian w ramach samego wznowienia.

## Co rzeczywiście zrecenzowano

- [Pierwotna niezależna recenzja tej rozmowy](CODEX-INITIAL-REVIEW-2026-10-21.md):
  snapshot `e0e3d1839`;
  build i 33/33 CTest przeszły. Wykryto problemy semantyki mutacji, tasków,
  timeoutów, IPC, importu, ścieżek i obsługi błędów. Sieciowych smoke testów
  w tej pierwszej sesji nie uruchamiano.
- Po niej wykonano serię commitów naprawczych `809e805e3`..`3a31871eb`
  i dokumentację `9a84365d0`.
- Osobna sesja Codexa zrecenzowała **`9a84365d0`**. Wynik:
  **changes requested — 7 FIXED, 9 PARTIAL**, dodatkowo pięć nowych uwag.
  Recenzent raportował build, **35/35 CTest** oraz sieciowe smoke mod/pack PASS.
  Pełna końcowa odpowiedź została zachowana w
  [CODEX-REVIEW-2026-10-21.md](CODEX-REVIEW-2026-10-21.md).
  Źródło: lokalna sesja Codex `01a16072-05ba-7a80-8a84-0c3cca2f90dc`,
  końcowa odpowiedź z 2026-10-21 22:00:55 UTC.
- Późniejsze commity: `325556829` zachowuje URL-e przy rozwiązywaniu ścieżek;
  `95b7d2f98` poprawia wynik błędu metadanych i strukturalne błędy faz mod update.
  Nie ma tu zapisanej niezależnej akceptacji tych późniejszych rewizji.

## Praca zastana przy checkpointcie

HEAD przed zapisem: **`95b7d2f98`**. Niezacomitowane były:

- `launcher/cli/ModCommands.cpp` i `.h`: wydzielenie `restoreDisabledMods`,
  wykonanie przywracania `.disabled` również po nieudanym batchu oraz raportowanie
  błędów przywracania zamiast samego ostrzeżenia.
- `tests/CliModRestore_test.cpp`: cztery przypadki — przywrócenie pobranych plików,
  pominięcie brakującego downloadu, błędy operacji na plikach i pusta lista.
- `tests/CMakeLists.txt`: rejestracja `CliModRestore`.

Te zmiany są zachowywane jako zastana praca. Test helpera nie dowodzi sam w sobie
poprawności całego asynchronicznego update'u. Weryfikacja batcha powinna obejmować
sukces jednego downloadu przy awarii drugiego i timeout w trakcie downloadu.
Commit zachowujący ten kod i testy: **`4ea75a44d`**
(`fix(cli): restore disabled mods after partial update failure`).
Ręczny scenariusz poprzedniego agenta zachowano w
`docs/cli/repro/verify-disabled-restore.sh` (API Modrinth; nieuruchamiany tutaj).
Przerwany przy zamykaniu nadzorcy eksperymentalny build w `/tmp/br` nie jest
zaliczonym dowodem weryfikacji. Nie kontynuuj go zamiast wspieranego wrappera.

## Otwarte sprawy do ponownej weryfikacji

Poniższa lista bazuje na niezależnej recenzji `9a84365d0`; późniejsze zmiany
mogły rozwiązać część uwag. Potwierdzaj na nowym, dokładnie oznaczonym SHA.

1. **Timeout/lifetime:** zatrzymanie oczekiwania nadal pozwala na dalsze działanie
   taska i kolejne operacje na jego modelu/instancji. `unfinishedTasks()` zachowuje
   taski bez usuwania po zakończeniu. Potrzebne ownership, koordynacja i cleanup.
2. **Retry per operacja:** globalny default nie obejmuje jawnego `askRetry=true`,
   już istniejących jobów; może zmieniać zachowanie niezwiązanych z CLI jobów GUI.
3. **Import:** pozostałe dialogi Flame optional/blocked oraz pobieranie gry
   w importach MultiMC/Technic. Sprawdzić politykę bez interakcji dla wszystkich typów.
4. **Ścieżki IPC:** GUI potrzebuje cwd klienta lub ścieżek rozwiązanych po stronie
   klienta. `325556829` dotyczy URL-i; nie uznawać go za dowód naprawy całego IPC.
5. **ServersDat:** zachowanie nieznanych pól każdego serwera, nie tylko roota,
   i koordynacja z opóźnionymi zapisami modelu GUI.
6. **Raw output:** cięcie UTF-8 może rozdzielić parę surogatów (emoji).
7. **Logi:** błędy read/seek pliku zwykłego mogą nadal dawać pusty sukces
   (reprodukcja recenzenta: symlink logu do `/proc/self/mem`).
8. **Mod update:** niezależnie zweryfikować `95b7d2f98` i zapisany checkpoint
   przywracania stanu disabled; kody, jedna koperta, metadata failure,
   partial download, błędy rename/remove oraz raport instalacji zależności.
9. **Reszta pierwotnej recenzji:** GUI actions podczas nested event loop,
   walidacja/limity ramek IPC, batch semantics, spójność profili i trwałość zapisów.
   Guard IPC sam w sobie nie jest blokadą wszystkich akcji GUI.

## Dokumentacja i upstream

`SPIKE-ROADMAP.md` jest historyczną kroniką. Poprzednie stwierdzenie „all fixed”
nie było wynikiem akceptującej recenzji; zastępuje je status opisany powyżej.
Opis partial failure musi odzwierciedlać exit 1 dla rzeczywistych częściowych
błędów. Nie zakładaj kompletności maszynowych kodów w każdym `failed` bez testu.

Wątki: PrismLauncher/PrismLauncher #103, #6313, #6315, #6316, PR #6314 i #6193.
Stan upstream może się zmienić; przed opisaniem aktualnych decyzji przeczytaj je
przez `gh`. Ryex wspiera dobre CLI, ale wymaga projektu unattended task execution
i error paths; obecne sync-over-async wrappery nie mają jego akceptacji.
`CONTRIBUTING.md` zawiera politykę upstream dotyczącą udziału AI. Zachowaj jawne
pochodzenie pracy i oddziel lokalny fork od decyzji o zgłoszeniach upstream.

## Zakończenie agentów

W checkpointcie zamknięto przez Orca sesje projektu: Claude settings,
Codex re-review, aktywnego nadzorcę OpenCode w powiązanym worktree `makara`
oraz osierocony terminal `spike-resources`. Historia recenzji i rewizji została
przeniesiona z lokalnych sesji/ignorowanego `.scratch/` do śledzonych plików.
Pozostałe projekty użytkownika nie należą do zakresu zamykania.

W chwili przygotowania commitów pozostawała wyłącznie sesja Codexa zapisująca
checkpoint; jej zamknięcie następuje po przekazaniu wyniku użytkownikowi.

## Wynik weryfikacji checkpointu

Kod zapisany w **`4ea75a44d`** (sprawdzono identyczny stan źródeł przed commitem):

- `./containers/spike-build.sh build`: PASS. Ninja ostrzegł o odzyskiwaniu pliku
  logu (`premature end of file; recovering`); build zakończył się kodem 0.
- `./containers/spike-build.sh test`: **37/37 PASS**, 0 błędów, 13,73 s.
  Obejmuje `CliModRestore`, `CliDomainSmoke` i `CliIpcSmoke`.
- `git diff --check`: PASS; składnia zachowanego skryptu repro (`bash -n`): PASS.
- Sieciowych smoke testów ani zachowanego repro w tej sesji nie uruchamiano.
- `clang-format` nie jest dostępny na hoście; nie wykonano osobnego formatowania.
- Nie wykonano nowej niezależnej recenzji poprawki `4ea75a44d`.

Dokumentacja jest zapisana w kolejnym commicie po `4ea75a44d`; jego SHA można
odczytać przez `git log -2 --oneline`. Drzewo ma być czyste po checkpointcie.
Wynik testów jest punktem startowym do dalszej recenzji, nie jej zastępstwem.
