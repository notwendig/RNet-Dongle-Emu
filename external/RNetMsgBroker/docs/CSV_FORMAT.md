# CSV format

`RNetMsgBrokerTest --out/-o` writes semicolon separated CSV.

Header:

```text
zeile;zeit;interface;can_id;format;rtr;dlc;data;name;typ;phase;quelle;senke;ausgabe
```

## Columns

| Spalte | Bedeutung |
|---|---|
| `zeile` | Eingabe-Zeilennummer aus der candump-Datei |
| `zeit` | candump-Zeitstempel ohne Klammern, falls vorhanden |
| `interface` | CAN-Interface aus candump, z. B. `can0` |
| `can_id` | CAN-ID als Hexwert |
| `format` | `STD` oder `EXT` |
| `rtr` | `1` bei Remote Transmission Request, sonst `0` |
| `dlc` | Anzahl Nutzdatenbytes |
| `data` | Nutzdaten als kompakter Hexstring |
| `name` | erkannter Frame-Name |
| `typ` | Frame-Typ aus JSON (`frametype`) |
| `phase` | Protokollphase, z. B. `startup.serial_auth` |
| `quelle` | vermutete Quelle, z. B. `jsm`, `pm`, `module` |
| `senke` | vermutete Senke, z. B. `pm`, `modules`, `bus` |
| `ausgabe` | vollständige Decoder-Ausgabe; wird CSV-gequotet |

## LibreOffice Calc

Beim Öffnen:

- Zeichensatz: UTF-8
- Trennoption: Semikolon
- Texttrenner: `"`
## Version/log output

CSV-Dateien bleiben reine CSV-Dateien. Bei `--out/-o` werden `libversion:` und `geladen:` nicht in die Datei geschrieben, sondern nur als Logausgabe gemeldet.

Die reine Library-Version kann separat abgefragt werden:

```bash
RNetMsgBrokerTest --libversion
```

