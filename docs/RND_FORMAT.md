# RND analysis notes

The examined `Generic_V33_1_1245.rnd` file is an encrypted PGDT parameter
information database used by the tested R-Net Programmer build.

## Observed encryption

The examined file uses standard Blowfish on independent 8-byte blocks with a
PGDT-specific byte order for the two 32-bit halves. The application derives a
31-byte Blowfish key from a deterministic internal PRNG. `tools/rnd-decrypt.c`
implements the compatible transformation for this tested database generation.

The tool is version-specific research code; do not assume future/older RND
releases use the same derivation.

## Decrypted content

The decrypted database contains, among other structures:

- file-layout mappings,
- parameter dictionary entries,
- user-visible category paths,
- minimum/maximum/default/step metadata,
- units,
- ordinary 49-byte NV-location records,
- module/file selectors.

`tools/rnet-bin2md.py` uses those structures to produce a readable Markdown
inventory without shipping the decrypted database itself.
