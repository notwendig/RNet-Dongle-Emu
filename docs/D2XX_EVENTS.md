# D2XX asynchrone Ereignisse

<!-- RNET-D2XX-EVENTS-V29 -->

Basis ist der **D2XX Programmer's Guide, FT_000071, Version 1.4**, Abschnitt 3.28 `FT_SetEventNotification`.

Die Anwendung übergibt an `FT_SetEventNotification(ftHandle, dwEventMask, pvArg)` eine Ereignismaske und in `pvArg` ein von der Anwendung erzeugtes Event-Handle. Für `FT_EVENT_RXCHAR` soll dieses Event signalisiert werden, wenn Zeichen empfangen wurden. Der R-Net Programmer wurde mit `Mask=1` beobachtet; damit ist für unseren Pfad `FT_EVENT_RXCHAR` relevant.

## V29-Implementierung

Der Emulator speichert getrennt:

- das vom R-Net Programmer übergebene Windows-Event-Handle,
- die angeforderte Event-Maske,
- einen eigenen Eventstatus,
- die RX-Queue.

Wenn neue Bytes in die RX-Queue gestellt werden, setzt der Emulator `FT_EVENT_RXCHAR` im Eventstatus und ruft `SetEvent()` auf, sofern das Event registriert und `FT_EVENT_RXCHAR` in der Maske aktiviert ist.

Damit ist die Benachrichtigung **an den Empfang neuer Daten gekoppelt**. Ein `FT_Read()` signalisiert nicht erneut nur deshalb, weil nach einem Teil-Read noch Bytes in der RX-Queue liegen.

`FT_GetStatus()` beziehungsweise `FT_GetEventStatus()` liefern den gespeicherten Eventstatus und konsumieren diesen Status. Die Anzahl verfügbarer RX-Bytes bleibt davon unabhängig und wird weiterhin aus der RX-Queue bestimmt.

Wenn `FT_SetEventNotification()` erst registriert wird, nachdem bereits RX-Daten vorliegen, wird `FT_EVENT_RXCHAR` als ausstehend markiert und das Event unmittelbar signalisiert. Das ist für den Startpfad wichtig, weil Initialdaten bereits vor der Eventregistrierung in der virtuellen RX-Queue liegen können.

`FT_EVENT_MODEM_STATUS` und `FT_EVENT_LINE_STATUS` werden weiterhin nicht synthetisch erzeugt, weil der beobachtete R-Net Programmer für diesen Pfad `Mask=1` verwendet.
