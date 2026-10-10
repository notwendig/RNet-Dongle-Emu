# Unknown frames

There are two different cases.

## Decoder UNKNOWN

The broker could not match the CAN frame at all. In CSV this is usually visible in `name` or `ausgabe` as `UNKNOWN ...`.

Find them:

```bash
awk -F';' 'NR>1 && ($9 ~ /^UNKNOWN/ || $14 ~ /^UNKNOWN/) {print}' testdata/candump.csv
```

## Known but not understood

The broker matched the frame, but the exact meaning is not proven. These frames use:

```text
phase=unknown.observed
```

Find them:

```bash
scripts/list_unknown_observed.py testdata/candump.csv
```

Or with awk:

```bash
awk -F';' 'NR>1 && $11=="unknown.observed" {print}' testdata/candump.csv
```

## Reproducibility

Für Fehlerberichte bitte zusätzlich die Library-Version angeben:

```bash
RNetMsgBrokerTest --libversion
```
