# Authors and sources

## Projekt

- Jürgen W. Sievers / R-Net Analyzer project context
- ChatGPT-assisted implementation and documentation work

## Frame-Definitionen und Referenzen

Dieses Repository enthält beobachtete und abgeleitete R-Net/CAN-Frame-Definitionen.
Ein Teil der Definitionen wurde aus öffentlich dokumentierten open-rnet-Informationen übernommen oder daran angelehnt:

- redragonx/open-rnet, insbesondere `reference/RNET_FRAME_DICTIONARY.md`
- eigene candump-Beobachtungen und lokale Tests

## Hinweis

R-Net ist ein sicherheitskritisches Rollstuhl-Steuerungssystem. Die Namen und Bedeutungen einzelner Frames können unvollständig oder vorläufig sein. Frames mit `phase=unknown.observed` sind absichtlich als beobachtet, aber fachlich noch nicht sicher verstanden markiert.

## Versioning

Die in Fehlerberichten und Releases genutzte Version stammt aus `RNetMsgBroker::versionString()` und kann mit `RNetMsgBrokerTest --libversion` abgefragt werden.
