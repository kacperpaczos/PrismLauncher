# Archiwum rewizji i dalszy stan

Zapis przeniesiony z ignorowanego `.scratch/REVISION-LOG.md` 2026-10-22.
Poniższe wpisy są notatkami poprzednich agentów, a nie niezależnym potwierdzeniem napraw.
Aktualny punkt wznowienia i ograniczenia dowodów: [HANDOFF.md](HANDOFF.md).

# REJESTR REWIZJI — spike/mod-cli (pełna historia + przyczyny)

> Cel pliku: retrospektywa „co poszło nie tak w tej architekturze". Każda rewizja
> naprawcza ma przypisaną kategorię przyczyny. Aktualizowane na bieżąco.

## Legenda kategorii przyczyn (do analizy końcowej)

- **[A-ekstrakcja]** błędy mechaniczne przy rozbijaniu funkcji na fazy (refaktor #10)
- **[A-zakres]** fix za szeroki / za wąski — dotknął przypadków, których nie powinien
- **[B-arch]** Architektura: sync-over-async wrappery, per-proces polityki zamiast
  per-operacja, zmiana cwd procesu, reentrancja nested event loop
- **[C-semantyka]** świadome decyzje semantyczne, które okazały się złe
  (update bez instalacji zależności, partial failure = sukces)
- **[D-testy]** luka testowa — happy path pokryty, ścieżki błędów nie
- **[E-środowisko]** flaky filesystem/host po crashu (PCH, moc, Bus error)
- **[F-polityka]** AI-policy upstream (CONTRIBUTING.md) — blokada contribucji

## Faza 0 — fundament (T0, 2026-10-19/20)

| Commit | Co | Uwagi |
|---|---|---|
| (T0 agent) | kontener Fedora+Qt6, Java target 7→8, SELinux fix, submoduły | build zielony, 24/24 testów |

## Faza scaffold + IPC v2 (2026-10-20)

| Commit | Co | Kategoria |
|---|---|---|
| 7bd01526d | IPC request id + response envelope | — |
| ba71093b7 | wykonywanie CLI w działającym GUI | **[B-arch]** reentrancja (naprawiona w K1 9c661f5d7) |
| 379fc88b0 | fix: peer przy usuwanych socketach | — |
| 5f6218a5e | komenda `status` | — |
| a37c696a3 | testy round-trip IPC | **[D-testy]** (pierwsze testy ścieżek błędów) |
| bc20d3a29 / 0c833a4a2 / b65d142e8 / 697ae0ebe | testy OutputCapture + docs IPC | — |

## Faza polish (GLM review, 2026-10-20)

| Commit | Co | Kategoria |
|---|---|---|
| 2ad6117b6 | **regresja zink→zinc przywrócona** (clang-tidy pass na 6193) | **[F] + upstream bug** |
| 03a411d16 | notes stdin przez IPC odmówione | **[B-arch]** |
| af4e24533 | martwy kod Result.h/Parser.cpp | **[D-testy]** (0 użyć = brak pokrycia) |
| 6e1b4997b | dedup resolveInstance (16 lokacji) | — |
| e68e786c2 | style pass (cap OutputCapture, inline constexpr) | — |
| aeb2ec06c | flaga taskowa zamiast globalnego flip-flop settings | **[C-semantyka]** poprawka |

## Faza merge Stage A+B (2026-10-21 rano)

| Commit | Co |
|---|---|
| cd07cfde7 | split mod update na fazy (pierwsza wersja) |
| e0e3d1839 | **split mod update + jeden CommandContext dla wszystkich runnerów** |

## Runda fixów po re-review Codexa #1 (2026-10-21 18:12–23:50) — 17 fixów

| Commit | Co naprawia | Kategoria przyczyny buga |
|---|---|---|
| 809e805e3 | **metadata task NIE BYŁ dodany do sekwencji** (mod update) | **[A-ekstrakcja]** + **[D-testy]** |
| 2a53efde7 | **kody wyjść gubione (ambiguous→Generic) + dwie koperty JSON** | **[A-ekstrakcja]** |
| 48a806498 | update wyłączonego moda włączał go z powrotem | **[C-semantyka]** |
| 918cb543a | zależności rozwiązywane, ale nie instalowane | **[C-semantyka]** |
| b0938e004 | awaria kosza = ciche permanentne usunięcie; sukces mimo błędu FS | **[B-arch]** (void API upstream) |
| a598c3f96 | screenshot copy --force / icon set usuwały własne źródło | **[A-zakres]** |
| 92bbf025a | import wciąż interaktywny (OptionalModDialog) + ignorował --download | **[B-arch]** (GUI-API bez kontraktu unattended) |
| 9c661f5d7 | reentrancja: nested messageReceived wykonywało komendy | **[B-arch]** |
| bea848832 | partial failures = ok:true, exit 0 | **[C-semantyka]** |
| bc3360048 | timeout = przestań czekać, nie przestań działać | **[B-arch]** |
| b2c8888d4 | askRetry per-proces → zagnieżdżone joby pytają | **[B-arch]** |
| bb40d38d2 | raw IPC ≠ standalone (pusty text→{}, truncated) | **[A-zakres]** |
| **221ed1402** | **ścieżki klienta vs cwd launchera — REGRESJA: psuło URL-e** | **[A-zakres]** → naprawa 325556829 |
| 881749c57 | log read błędy = pusty sukces | **[B-arch]** (LogReader API) |
| 5ee290640 | servers.dat: nieznane NBT gubione; brak guardu | **[A-zakres]** |
| 3a31871eb | settings: zapisywalne klucze wrażliwe, słaba walidacja | **[A-zakres]** |
| 9a84365d0 | docs | — |

## Runda fixów po re-review Codexa #2 (2026-10-22) — W TOKU

| Commit | Co naprawia | Kategoria |
|---|---|---|
| **325556829** | **URL import: resolveClientPath nie dotyka URL-i** (regresja 221ed1402) | **[A-zakres]** (naprawa) |
| ✅ 95b7d2f98 | mod update: protokół CliError zweryfikowany (kody 3/5/7, jeden render) + **metadata failure → hasCheckFailures → exit 7** (bug odtworzony red/green) | [A-ekstrakcja] (naprawa) |
| ⏳ | pozostałe: deps-install edge, import Flame/MultiMC, askRetry ownership, reaper, ServersDat per-serwer, log symlink, raw-IPC surrogate | [B-arch]/[C-semantyka] |

## Wnioski wstępne (do rozszerzenia w retrospektywie)

1. **Rozbicie 290-liniowej funkcji na fazy wprowadziło 2 regresje** — ekstrakcja bez
   testów pokrywających wyodrębniane ścieżki (brak modu bez metadanych, błędne kody
   wyjść) = mechanical-extraction error. Lekcja: **przed refaktorem dopisać testy
   charakteryzujące obecne zachowanie** (characterization tests).
2. **16 odkryć Codexa** = głównie [B-arch] (GUI-API bez kontraktu unattended) i
   [C-semantyka] (ciche częściowe sukcesy) — czyli dokładnie to, co Ryex nazwał
   „async nature / needs proper design".
3. **[D-testy]** był wspólnym mianownikiem: smoke testy Happy-path nie wykryły
   żadnej z regresji #10.
4. Polityka AI upstream (CONTRIBUTING.md) = twardy limit dla toru contribucji.

## Notatka z Task 2/3 (2026-10-22)
- Bug „dwie koperty/kody Generic" był już naprawiony w 2a53efde7 (runda #1) — Codex
  oceniał stan pośredni. Zweryfikowano zgodność z designem + dodano test regresji
  (Output::reportError emituje dokładnie jeden dokument).
- Bug „metadata failure → ok:true" POTWIERDZONY i naprawiony (95b7d2f98 poprzedzony
  red/green: repro exit 0 → fix exit 7 check_failed).
- Lekcja: review zakresu e0e3d1839..HEAD mieszał stan sprzed i po częściowych fixach —
  przy kolejnych re-review podawać dokładny SHA bazowy każdego findingu.

## Checkpoint lokalny — 2026-10-22

Wpis zweryfikowany w sesji zapisującej checkpoint; poprzednie wpisy pozostają
archiwalnymi notatkami ich autorów. Osobny Codex oceniał dokładnie `9a84365d0`,
z wynikiem **7 FIXED / 9 PARTIAL**, a nie ogólnym zatwierdzeniem wszystkich napraw.
Pełny raport: [CODEX-REVIEW-2026-10-21.md](CODEX-REVIEW-2026-10-21.md).

| Rewizja | Zawartość | Dowód/status |
|---|---|---|
| `325556829` | Zachowanie URL-i przy rozwiązywaniu ścieżek klienta | Późniejsza poprawka; bez nowej niezależnej akceptacji w tym archiwum |
| `95b7d2f98` | Strukturalne błędy faz mod update i błąd metadanych wpływający na wynik | Późniejsza poprawka; jej notatki red/green są powyżej, wymagają rozróżnienia od recenzji |
| `4ea75a44d` | Przywracanie disabled po częściowej awarii batcha, raport błędów restore, test `CliModRestore` | Zastana praca zachowana; wrapper build PASS, CTest **37/37 PASS**, 13,73 s; brak nowej niezależnej recenzji |
| commit dokumentacji po `4ea75a44d` | Handoff, obie recenzje Codexa, ten rejestr, repro i korekta roadmapy | Lokalny zapis na polecenie użytkownika, bez push |

Pozostałe sprawy i dokładna procedura wznowienia: [HANDOFF.md](HANDOFF.md).
Zamknięto sesje Claude settings, Codex re-review, nadzorcy OpenCode/makara
oraz osierocony terminal spike-resources. Innych projektów nie zamykano.
