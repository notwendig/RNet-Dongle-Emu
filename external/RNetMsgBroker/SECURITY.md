# Security and safety policy

## Sicherheitskritischer Kontext

R-Net/CAN wird in elektrischen Rollstühlen eingesetzt. Fehlerhafte Interpretation oder aktive Einspeisung von Frames kann gefährliche Bewegungen oder Systemfehler auslösen.

## Dieses Projekt

RNetMsgBroker ist ein Decoder und Analysewerkzeug. Es soll Frames lesen, klassifizieren und als Text/CSV ausgeben. Es ist nicht als Fahrsteuerung gedacht.

## Sichere Tests

- Tests bevorzugt mit gespeicherten candump-Dateien durchführen.
- Keine aktiven TX-Versuche am Rollstuhl ohne isolierte Testumgebung.
- Bei CAN-TX: Rollstuhl sichern, Räder frei, Not-Aus/Power-Off erreichbar.
- Unbekannte Frames nicht als sichere Fahr-, Brems- oder Mode-Befehle deklarieren.

## Meldungen

Sicherheitsrelevante Fehler bitte mit Logausschnitt, Version (`RNetMsgBrokerTest --libversion`) und Reproduktionsschritten melden. Keine sensiblen Seriennummern veröffentlichen, wenn sie einer realen Person oder einem realen Hilfsmittel zugeordnet werden können.
