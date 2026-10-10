# Betriebsmodi von `rollstuhl.emu`

<!-- RNET-MODES-V29 -->

`rollstuhl.emu` hat zwei explizite Rollen. Es bleibt **ein Binary**; das erste Kommandozeilenargument bestimmt die Rolle.

## `emu`

```text
R-Net Programmer App -> FTD2XX DLL -> can0 <CAN> can1 -> rollstuhl.emu
```

Start:

```bash
./start.sh emu
```

Direkt:

```bash
RNET_EMU_CAN=can1 ./build/rollstuhl.emu/rollstuhl.emu emu
```

`start.sh emu` startet den SocketCAN-Proxy, `rollstuhl.emu emu` und den R-Net Programmer. Die DLL wird auf `Device=can0` gestellt.

## `dev`

```text
RealRollstuhl -> can0 <CAN> Device.emu_CJSM
```

Start:

```bash
./start.sh dev
```

Direkt:

```bash
RNET_DEV_CAN=can0 ./build/rollstuhl.emu/rollstuhl.emu dev
```

In diesem Modus werden Programmer, FTD2XX-DLL-Pfad und SocketCAN-Proxy nicht gestartet. Für die GUI wird kein Programmer-Replay geladen. Das Fenster trägt den Titel `Device.emu_CJSM — DEV — <can>`.

## Verwaltung

```bash
./start.sh stop
./start.sh status
```

Umgebungsvariablen: `RNET_APP_CAN`, `RNET_EMU_CAN`, `RNET_DEV_CAN`, `RNET_BITRATE`, optional `RNET_PROXY_PORT`.

## Installer-Sicherheit

Der V29-Installer patcht die GUI nur an bekannten aktiven Quellpfaden. Er durchsucht **nicht** `run/`, `build/` oder `backup-*`. Damit kann eine Sicherungskopie nicht erneut versehentlich als aktive `main.cpp` gewählt werden.
