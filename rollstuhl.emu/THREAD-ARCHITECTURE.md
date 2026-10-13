# rollstuhl.emu – Protocol-StateMachine-Architektur V54

Marker: `RNET-PROTOCOL-STATES-V54`

## Grundlage

V54 übernimmt V53 vollständig: ReadBlock-State-Transitions sind nicht mehr von
einem Replay-Treffer abhängig; nur `0x002 RTR` ist die harte Power-Off-Barriere.
`0x78F#40 10 02` und `0x78F#4F 10 02` sind weiterhin **keine** unmittelbaren
Power-Off-Kommandos.

## V54: Post-Write-Reboot statt Kommunikationsabbruch bei 92 %

Der Write-Lauf zeigte einen eindeutigen Unterschied zwischen Emulator und
Referenztrace. Im Programmer-Write-Kontext erscheint `20/82`; anschließend
kommt die POP-Sequenz `40/10/02 -> 41/10/02 -> 42/10/02 -> 43/10/02 ->
4F/10/02`. Danach sendet der Programmer zunächst nichts mehr und wartet auf den
R-Net-Neustart.

Wichtig: `4F/10/02` kommt auch in Read-Läufen vor. Deshalb wird es in V54 nur
als **Post-Write-Trace-Marker** benutzt, wenn vorher `20/82` gesehen wurde. Es
ist selbst kein Power-Off-Befehl.

Der erfolgreiche Real-CAN-Trace liefert folgende Zeiten:

```text
4F/10/02
   |
   | 27,718 s
   v
002# / 004# ...       Sleep-Vorphase
002#R                 HARD BARRIER -> Generation++
...                    Sleep-Sequenz
000# / 000#R          Sleeping ACK / Sleep-all
   |
   | 10,232 s
   v
00C#                   CAN connection test / Wake
00E#, 7B3#, 1F...      normaler Power-On / Connect
```

V54 startet deshalb nach dem ersten `4F/10/02` **nach einem beobachteten
20/82** einen einmaligen, trace-getakteten Reboot. Programmer-Retry-Scans armen
den Timer nicht erneut.

## ReadBlock

```text
50/8C -> 1E3Fxxxx ACK(s) -> 80/8C -> Idle
```

`1E3Fxxxx` und `80/8C` ändern den Ablaufzustand unabhängig davon, ob Replay
eine direkte Antwort enthält.

## WriteBlock / Write-Kontext

Die vorhandene WriteBlock-StateMachine bleibt unverändert. Zusätzlich merkt V54
`20/82` als sichere Unterscheidung zum reinen Read-Lauf. Erst danach darf ein
`4F/10/02` den verzögerten Reboot armen.

## PowerOff

```text
002#       Vorphase
004#       Sleep commencing
002#R      HARD BARRIER -> Generation++ -> alte TX ungültig
004#       Sleep commencing
...
000#       Sleeping ACK
000#R      Sleep-all RTR
```

Nach dem vollständigen Sleep wird im Post-Write-Reboot nach 10,232 s der
Power-On-Ablauf gestartet. Ein normaler manueller Power-Off bleibt davon
unberührt.

## Parallelität und Replay

Alle Protokoll-StateMachines behalten eigene Worker-Threads. Die Session-
Generation sperrt veraltete Antworten präemptiv. Replay bleibt ausschließlich
Daten-/Antwortquelle; Ablaufzustände werden aus dem Protokoll bestimmt.

## Installation

V54 erwartet V53 als Ausgangsstand und ist idempotent. Die Update-Skripte legen
keine Backup-Dateien oder neuen Projektverzeichnisse im Repository an; Rollback
erfolgt über Git.
