# RND analysis notes

The examined `Generic_V33_1_1245.rnd` file is a PGDT parameter-information
database used by the tested R-Net Programmer generation.

## Observed encryption

The examined file uses Blowfish with a PGDT-specific transformation/byte order.
The compatible research implementation for this database generation is in
`tools/rnd-decrypt.c`.

The implementation is deliberately version-specific. Other RND generations may
use different key derivation, layout or metadata.

## Decrypted content

The researched database contains structures used for:

- file and module mappings
- parameter dictionary entries
- user-visible category paths
- limits/defaults/steps
- units
- NV-location information
- module/file selectors

`tools/rnet-bin2md.py` uses the locally decrypted information to build a readable
inventory without requiring the decrypted vendor database to be committed to
the repository.

Original and decrypted RND files are local research inputs and must not be
published as project content.
