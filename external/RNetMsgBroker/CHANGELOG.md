# Changelog

## 1.0.0

- Erste stabile GitHub-Release-Version.
- Version überall auf `1.0.0` angehoben.
- CI/Smoke-Test dokumentiert und auf Versionsausgabe, CSV-Erzeugung und Deliver-Archiv ausgerichtet.
- `deliver` erzeugt das Release-Archiv `RNetMsgBroker_v1.0.0.zip`.
- Dokumentation und Beispiele auf die stabile Version aktualisiert.

## 0.2.17

- `--version` and `--libversion` are now handled before `QCommandLineParser`, so they cannot be rejected as unknown options by parser setup.
- `testdata/candump_sample.txt` is now a known-good sample without intentional decoder `UNKNOWN` frames.
- `scripts/check.sh` checks the known-good sample and still reports real decoder misses.
- Documentation updated for the robust version switches and the corrected smoke-test sample.

## 0.2.16

- `RNetMsgBrokerTest --version` aktiviert die Qt-Standardversionsausgabe.
- `RNetMsgBrokerTest --libversion` gibt nur die reine Library-Version aus.
- Normale Textausgabe ohne `--out` beginnt mit `libversion: ...`.
- Alle Markdown-/Doku-Dateien auf Version 0.2.16 und die neuen Versionsbefehle aktualisiert.

## 0.2.15

- CMake-Target `deliver` ergänzt.
- `deliver` erzeugt `RNetMsgBroker_v${PROJECT_VERSION}.zip` im Build-Verzeichnis.
- Das Archiv wird mit `cmake -E tar --format=zip` erstellt und benötigt kein externes `zip`-Programm.

## 0.2.14

- `R-Net.json` auf korrekt geschriebene Schlüssel normalisiert (`big_endian`, `description`, `fields`, `phase`).
- Tippfehler-Aliase aus Parser und Dokumentation entfernt.
- `readJson()` verlangt wieder syntaktisch gültiges JSON ohne automatischen Reparaturversuch.
- `scripts/check.sh` prüft `R-Net.json` zusätzlich auf verbotene Tippfehler-Schlüssel.

## 0.2.13

- GitHub-Pflegestand ergänzt: `CHANGELOG.md`, `AUTHORS.md`, `CONTRIBUTING.md`, `SECURITY.md`, Pull-Request-Template und Issue-Template.
- Skripte ergänzt: Build, Check, candump-zu-CSV und Analyse von `phase=unknown.observed`.
- Doku ergänzt: CSV-Format, JSON-Schema, Start-up-Sequenz und Umgang mit unbekannten Frames.
- Öffentliche API und wichtige Parser-/CSV-Funktionen kommentiert.
- Optionales CMake-Install-Target ergänzt.

## 0.2.12

- CSV-Header präzisiert: letzte Spalte heißt `ausgabe`.

## 0.2.11

- `--out` / `-o` schreibt Semikolon-CSV mit stabiler Spaltenstruktur.
- Datei-Ausgabe enthält nur CSV, keine Log-Kopf-/Abschlusszeilen.

## 0.2.10

- `R-Net.json` um `phase`, `quelle`, `senke` ergänzt.
- Serial-Exchange-Sequenzen in Data-/RTR-Definitionen getrennt.
- `--full` zeigt Frame-Metadaten.

## 0.2.9

- `--out <file>` / `-o <file>` ergänzt.

## 0.2.8

- Beobachtete 7-Byte-JSM-Heartbeat-Variante ergänzt.
- Zusätzliche Startup-/RTR-Familien ergänzt.

## 0.2.7

- Frame-Familien aus redragonx/open-rnet `reference/RNET_FRAME_DICTIONARY.md` übernommen.
- Spezifischere Datenmuster gewinnen beim Matching.

## 0.2.6

- `--candump` / `-d` liest candump-Dateien.
- `--full` für candump-Ausgabe.

## 0.2.5

- Standard-Demo ohne Argumente gibt Kurz- und Vollausgabe aus.

## 0.2.4

- `--both` / `-b` ergänzt.

## 0.2.3

- `toString(..., bool full=false)` ergänzt.

## 0.2.2

- `zyklus` auf Frame-Ebene verschoben; alte Feldvarianten bleiben kompatibel.

## 0.2.1 und älter

- Grundstruktur als Qt-Core-Library mit JSON-Katalog und Test-App.
