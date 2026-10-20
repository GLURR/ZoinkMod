# Wave Memory Assist – Geometry Dash 2.2081 / Geode 5.11.2

**STATUS: PROTOTYP Z KODEM ZRODLOWYM.** Logika osi czasu jest przetestowana natywnym kompilatorem C++.
Integracja z Geode/GD 2.2081 **nie zostala jeszcze skompilowana ani sprawdzona w grze**.
ZIP NIE JEST GOTOWYM PLIKIEM `.geode`.

## Jak ma dzialac

1. Ustaw `Record next run` w ustawieniach Geode, wejdz na level i rozpocznij od 0%.
2. Zrob pelne, poprawne przejscie 100%. Mod zapisuje **wylacznie** nacisniecia i puszczenia
   w gamemode wave, wraz z klatka fizyki (P1/P2). Dopiero ukonczony run zostanie zapisany.
3. Po 100% plik `.wma` zapisze sie w folderze danych moda (`geode/saved/local.wave-memory-assist/references/`
   albo odpowiadajacym mu katalogu Geode). Opcja `Record next run` wylaczy sie sama.
4. Po kolejnym wejsciu na ten sam level nagranie jest ladowane automatycznie.
5. `Early window` np. 20: klik w klatce 480–500 moze zostac przypisany do nagranego
   PRESSED w klatce 500. Dopiero klatka 500 wykona nacisniecie. Analogicznie RELEASE.
6. Jesli nie klikniesz w oknie, zapisany input **nie zostanie wykonany**. Nie ma autoplay/noclipa.

**Wazne:** Nie wystarczy zebrac kilkanascie wczesnych klikow w dowolnych momentach.
Kazde kolejne zdarzenie jest porownywane z nastepnym zdarzeniem o odpowiedniej polaryzacji
z nagranej sekwencji. Dlugosc przytrzymania jest rowniez korygowana; zwykle samo
przesuwanie PRESS bez RELEASE nie uratuje wave.

## Ograniczenia

- Tylko wave. Pozostale gamemode'y przepuszczaja inputy bez modyfikacji.
- Nie obsluguje rewizji levela jako tego samego nagrania; hash zmian w levelstring daje nowy klucz.
- Nie przesuwa pozycji gracza i nie wylacza kolizji.
- Przy brakujacym inputcie mozliwy jest desync, zgon albo wejscie w kolejna sekwencje z inna faza.
- Przy zmianach TPS nie zastosuje referencji (wykrywa rozbieznosc pierwszego kroku).
- Nie wspiera checkpointow/practice i start positions; nagrywanie i odtwarzanie od 0%.
- Nie testowano jeszcze wspolpracy z CBF, MegaHack i innymi modami zmieniajacymi inputy.
- Na oficjalnych demonlistach/recordach taki assist powinien byc traktowany jako niedozwolony.

## Ustawienia
- Enable mod / Record next run
- Early window od 0 do 120 krokow fizyki (domyslnie 20)
- P1/P2 oddzielnie
- Overlay REC/ASSIST + licznik inputow

## Budowanie pliku .geode na GitHub Actions

1. Wgraj zawartosc ZIP do **prywatnego** repo GitHub.
2. Wejdz w Actions -> Build Wave Memory Assist (Windows) -> Run workflow.
3. Jesli build zakonczy sie sukcesem, pobierz artefakt WaveMemoryAssist-Windows,
   rozpakuj i skopiuj otrzymany `.geode` do `Geometry Dash/geode/mods/`.
4. Jesli kompilacja zwroci blad, to znaczy, ze trzeba poprawic integracje pod Twoja
   wersje Geode. Konfiguracja workflow **nie gwarantuje** sukcesu builda.

## Testy logiki C++ (bez SDK)
`sh tests/run-tests.sh`

## Podstawy API
- Geode: https://docs.geode-sdk.org/classes/GJBaseGameLayer/
- Geode: https://docs.geode-sdk.org/classes/PlayLayer/
- Build: https://github.com/geode-sdk/build-geode-mod

## Licencja
Do prywatnych testow; nie publikuj wynikow z assistem jako legitne rekordy.
