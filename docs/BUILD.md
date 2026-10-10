# Build V32 — 0 errors / 0 warnings

V32 lässt `-Werror` aktiv. Es gibt keine globale Abschaltung von `-Wunused-function`.

Im aktuellen CAN-only-Pfad bleiben einige Replay-/POP-Helfer absichtlich im Source erhalten, obwohl sie nicht aufgerufen werden. Diese Funktionen werden lokal mit `[[maybe_unused]]` dokumentiert. V32 ergänzt insbesondere `load_stored_blocks()` zu dieser expliziten Liste.

Der vollständige Build ist:

```bash
./build-clean.sh all
```

Er erzeugt und prüft:

- `build/mingw32/ftd2xx.dll`
- `build/rnet-can-proxy/rnet-can-proxy`
- `build/rollstuhl.emu/rollstuhl.emu`

Jeder Configure- und Build-Log wird auf Compiler-/CMake-Warnungen sowie Fehler geprüft. Ein solcher Fund beendet den Build mit Fehlerstatus. Nur ein vollständig sauberer Build meldet `0 errors, 0 warnings`.
