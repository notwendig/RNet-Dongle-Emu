# RNet-Dongle-Emu

Offline-Emulator der FTD2XX/R-Net-Programmer-Schnittstelle mit Werkzeugen zur
Analyse von R-Net-Parameter- und Konfigurationsdateien.

> **Forschungs-/Interoperabilitätsprojekt.** Es gibt keinen physischen
> Rollstuhl-CAN/USB-Backend. Nicht zur Freigabe oder Abstimmung eines realen
> Fahrzeugs verwenden. Siehe [SECURITY.md](SECURITY.md).

## Enthalten

- `src/ftd2xx_emu.cpp`: 32-Bit-FTD2XX-Kompatibilitäts-DLL
- Replay plus zustandsbehaftetes POP-Repository mit persistentem RNB2-State
- `tools/rnd-decrypt.c`: Blowfish-Entschlüssler für die untersuchte
  `Generic_V33_1_1245.rnd`
- `tools/rnet-bin2md.py`: `.R-net` bzw. RNB2 → vollständiger Markdown-Bericht
- CMake-/VS-Code-freundliche MinGW32-Presets
- Tests, CI, Format- und Protokolldokumentation

Der Analyzer deckt alle Hauptbereiche des Programmers ab:

`Profile Management`, `Configuration`, `Speeds`, `Controls`, `Latched`,
`Seating`, `Motor`, `Input Output Module`, `Omni`, `Mouse 1`, `Mouse 2`,
`iDevice1`, `iDevice2`.

## Build

Fedora:

```bash
sudo dnf install cmake ninja-build mingw32-gcc-c++ gcc openssl-devel make python3
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```

DLL:

```text
build/mingw32/ftd2xx.dll
```

## RND lokal entschlüsseln

```bash
make -C tools
tools/rnd-decrypt Generic_V33_1_1245.rnd Generic_V33_1_1245.dec.bin
```

Die originale und die entschlüsselte RND-Datei gehören **nicht** ins Git-Repo.

## Vollständigen Bericht erzeugen

```bash
python3 tools/rnet-bin2md.py \
  example.R-net \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

oder aus dem Emulator-State:

```bash
python3 tools/rnet-bin2md.py \
  rnet-block-state.bin \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

Weitere Details: [README.md](README.md), [docs/](docs/), [NOTICE.md](NOTICE.md).

> **Replay-Hinweis:** `rnet-replay.txt` ist aufzeichnungsspezifisch und wird
> bewusst nicht im öffentlichen Repository mitgeliefert. `scripts/deploy-wine.sh`
> verwendet einen vorhandenen gültigen Replay weiter oder akzeptiert
> `RNET_REPLAY_FILE=/pfad/zu/rnet-replay.txt`.
