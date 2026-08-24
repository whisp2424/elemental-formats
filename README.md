# elemental-formats

A reverse-engineered implementation of the **EL01** Image Format and **IDX2** archive format.

## About

The K-ON! desktop accessory program (Kyoto Animation, 2009) stored everything it showed (wallpapers, sprites, icons, sounds, scripts) in two custom formats: an **IDX2** archive container and an **EL01** image format. Both went undocumented for years. This repository is the first public specification of both, plus a pair of small tools so the formats stay readable and writable by anyone, forever.

## What's here

- **SPEC.md** - the byte-level specification of both formats: overview, field tables, every codec with its token grammar, worked examples, and an appendix tracing each claim back to the disassembly.
- **RESEARCH.md** - how the search for prior documentation went.
- `el01` - a tool to decode and convert EL01 images to PNG (transparency included) or encode PAM/BMP images back into lossless EL01 files.
- `idx2` - a tool to list and extract IDX2 archives.

## Why?

The program is 32-bit Windows software from 2009, sold through a single shop, so everything drawn for it was locked inside formats nobody could read. A written spec changes that: the art becomes extractable, archivable and reusable independently of the executable, the formats stop being a dead end, and anything ever stored in them can come back out.

## Using the tools

```sh
# picture.el -> picture.png (RGBA for sprites)
el01 decode picture.el

# and back again, losslessly
el01 encode photo.pam -o photo.el

# show what an archive holds
idx2 list KonMenu.idx

# pull every file out
idx2 extract KonMenu.idx out/
```

For encoding, feed el01 a PAM or BMP file; any image editor can save those. Images with an alpha channel become two-stream sprites with a proper mask; flat images stay single-stream.

## Building

The project requires only a C compiler and `make`. No external libraries or additional dependencies are needed.

To build both binaries, run:

```sh
make
```

The resulting binaries are statically linked, and the build is deterministic.

`make test` runs the test suite if you have a directory of EL01 files to check against: `SAMPLES=/path/to/files make test`. The suite decodes every file against an independent Python implementation of the format, re-encodes and re-decodes everything requiring identical pixels, and feeds synthetic files through codecs the shipped assets never use.

## Disclaimer

This project was heavily AI-assisted. A coding agent was used throughout the reverse-engineering and implementation process, including analysis of the disassembly, reconstruction of the file formats, development of the specification, C implementation, tests, and documentation.

All claims in SPEC.md have been independently verified against either the complete set of files shipped with the original program or specific locations in its executable, with the relevant evidence documented in the specification's appendix.