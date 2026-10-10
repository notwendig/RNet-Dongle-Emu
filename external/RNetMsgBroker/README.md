# RNetMsgBroker

## Version

1.0.0 — Erste stabile GitHub-Release-Version. Enthält strikt gültige `R-Net.json`, Semikolon-CSV, robuste Versionsausgabe, `deliver`-Target und GitHub-CI.

0.2.17 — Versionsoptionen robust gemacht: `--version` und `--libversion` werden vor dem Qt-CommandLineParser abgefangen. Das Smoke-Test-Sample enthält keine absichtlichen Decoder-UNKNOWNs mehr.

0.2.16 — Lib-Version wird sichtbar ausgegeben: `--version`, `--libversion` und `libversion:` in der normalen Textausgabe. Alle Markdown-/Doc-Dateien wurden auf den aktuellen Stand gebracht.

0.2.15 — CMake-Target `deliver` ergänzt; erzeugt ein versioniertes Quellen-ZIP direkt aus dem Build-System.

0.2.14 — `R-Net.json` und Dokumentation auf korrekt geschriebene, strikt gültige JSON-Schlüssel bereinigt. Tippfehler-Aliase werden nicht mehr dokumentiert und nicht mehr als Parser-Aliase akzeptiert.

0.2.13 — GitHub-Pflegestand: Skripte, Doku, CONTRIBUTING/SECURITY/Issue-Templates und zusätzliche Kommentare ergänzt.
0.2.12 — CSV-Spaltenüberschrift präzisiert: die letzte Spalte heißt jetzt `ausgabe`.
0.2.11 — Test-App: `--out`/`-o` schreibt Semikolon-CSV.
0.2.10 — `R-Net.json` um Frame-Metadaten `phase`, `quelle`, `senke` ergänzt.

Eigenständige Qt-Core-Shared-Library zum Dekodieren von R-Net/CAN-Botschaften aus einer editierbaren `R-Net.json`.

## Enthalten

- `RNetMsgBroker` als Shared Library
- `readJson(jsonFilename)` zum Laden von Frame-Definitionen aus syntaktisch gültigem JSON
- interne Ablage als `QMultiMap<id-result, Definition>`
- `add(json)` und `del(json)` für Runtime-Erweiterung/-Löschung
- `toString(CanMsg)` und `toString(canId, data, extended, remote)`
- `RNetMsgBrokerTest` als Konsolen-Test-App
- Versionsausgabe über `--version`, `--libversion` und `RNetMsgBroker::versionString()`
- `R-Net.json` mit bekannten R-Net-Frame-Familien
- Frame-Metadaten `phase`, `quelle`, `senke` und `zyklus`
- Feld-Metadaten `unit`/`einheit` und `description`

## Ausgabeformat

`toString()` liefert bewusst eine kurze Broker-/Log-Zeile:

```text
RnetMsg_xyz (Modul=1); field_1=0xaa; field_2=0xbb
```

Dabei kommen `id_parts` aus der CAN-ID und `fields` aus dem Datenfeld hinter `#`.

## JSON-Schema

`R-Net.json` muss syntaktisch gültiges JSON sein. Keine Kommentare, keine unquotierten Schlüssel, keine `0x`-Zahlen ohne Anführungszeichen und keine absichtlich falsch geschriebenen Schlüssel.

Korrekte Schlüssel:

```json
{
  "Id-and-mask": "0x56700",
  "id-result": "0x56700",
  "key-mask": "0xFFF0F",
  "rnet_name": "RnetMsg_xyz",
  "frametype": "example",
  "phase": "test.example",
  "quelle": "test",
  "senke": "test",
  "zyklus": "20ms",
  "id_parts": [
    { "name": "Modul", "bit_mask": "0xf0", "shift": 4, "big_endian": false }
  ],
  "fields": [
    {
      "name": "field_1",
      "bit_mask": "0x00ff00",
      "shift": 8,
      "big_endian": false,
      "einheit": "",
      "description": "oberes Beispiel-Datenbyte"
    },
    {
      "name": "field_2",
      "bit_mask": "0x0000ff",
      "shift": 0,
      "big_endian": false,
      "einheit": "",
      "description": "unteres Beispiel-Datenbyte"
    }
  ]
}
```

`readJson()` akzeptiert nur syntaktisch gültiges JSON. Prüfung:

```bash
python3 -m json.tool R-Net.json >/dev/null && echo JSON_OK
```

Bei `--full` erscheinen die Frame-Metadaten in der Abschlussklammer, z. B.:

```text
RNetJsmSerialHeartbeat; serial32=0xfc801ecd; serial_tail=0x0; {typ=serial, phase=startup.serial_auth, quelle=jsm, senke=pm, id=0x0000000e, mask=0x000007ff, result=0x0000000e, STD, data=FC 80 1E CD 00 00 00 00}
```


## Versionsausgabe

```bash
./build/RNetMsgBrokerTest --version
./build/RNetMsgBrokerTest --libversion
```

`--version` wird vor dem Qt-CommandLineParser abgefangen und gibt die Test-App-/Library-Version aus. `--libversion` gibt nur die reine Library-Version aus, z. B.:

```text
1.0.0
```

Bei normaler Textausgabe ohne `--out` steht die Library-Version zusätzlich in der ersten Ausgabezeile:

```text
libversion: 1.0.0
geladen: 187 Definitionen aus ...
```

Bei `--out/-o` bleibt die Datei reine Semikolon-CSV; Versions- und Ladehinweise gehen nur ins Log.

## CSV-Ausgabe

```bash
./build/RNetMsgBrokerTest -d ./testdata/candump_sample.txt --full -o ./testdata/candump_sample.csv
```

CSV-Spalten:

```text
zeile;zeit;interface;can_id;format;rtr;dlc;data;name;typ;phase;quelle;senke;ausgabe
```

## GitHub-Pflege

Enthalten sind `CHANGELOG.md`, `AUTHORS.md`, `CONTRIBUTING.md`, `SECURITY.md`, GitHub-Templates, `scripts/build.sh`, `scripts/check.sh`, `scripts/candump_to_csv.sh`, `scripts/list_unknown_observed.py` und die Dokumente unter `docs/`.

Die Lizenz ist bewusst nicht automatisch festgelegt. Vor öffentlicher Veröffentlichung bitte eine passende `LICENSE` ergänzen.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build -j"$(nproc)"
```

## Deliver-ZIP erstellen

Das CMake-Target `deliver` erzeugt ein versioniertes Quellen-ZIP im Build-Verzeichnis:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target deliver
ls -l build/RNetMsgBroker_v*.zip
```

Das Archiv enthält die Projektdateien für Weitergabe/GitHub, aber keine Build-Artefakte und kein `.git`-Verzeichnis.

## Test

```bash
./build/RNetMsgBrokerTest --version
./build/RNetMsgBrokerTest --libversion
./build/RNetMsgBrokerTest --both
./build/RNetMsgBrokerTest -d ./testdata/candump_sample.txt --full -o ./testdata/candump_sample.csv
./scripts/check.sh
```
