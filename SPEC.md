# The IDX2 and EL01 resource formats

Technical specification of the two custom formats used by the K-ON! Desktop Accessory (けいおん! デスクトップアクセサリー, Kyoto Animation shop release, 2009/2011, Win32)

- **IDX2** - an archive container holding every asset of the program (`KonMenu.idx`)
- **EL01** - an image format used for every graphic inside that archive (`*.el` files)

About attribution: the only developer identity found anywhere in these assets is the literal string `ELEMENTAL SOFT` embedded in every IDX2 header (§2), including archives embedded inside the program's companion executables. A long-running Japanese circle of the same name publishes desktop-accessory programs of the same genre; that this circle produced the files documented here is a reasonable inference from that signature, not an established fact.

Both were reconstructed from disassembly of `KonMenu.exe` (32-bit x86 PE, image base 0x400000) and verified against everything the game ships: 262 `.el` images, 3 `.wav` sounds, 17 `.txt` text files and one `main.sd` script blob. Appendix B lists the machine-code locations backing every claim. The two tools in this repository (`el01`, `idx2`) implement the whole document.

## Contents

    1  Vocabulary and conventions
    2  IDX2 archive container
    3  Other files in the archive (.wav, .txt, main.sd)
    4  EL01 image format
       4.1  Shape of a file
       4.2  Outer header and stream directory
       4.3  Stream sub-header
       4.4  Color body encodings
       4.5  Delta filters
       4.6  Mask streams (alpha)
       4.7  Composing the final image
       4.8  Encodings not present in shipped assets
    5  Reference decoder pseudocode
    6  Worked examples
    A  Validation against shipped assets
    B  Disassembly provenance

## 1. Vocabulary and conventions

All integers are **little-endian**. `u8`, `u16`, `u32` denote unsigned integers of 1, 2 and 4 bytes. Offsets are hexadecimal.

An EL01 file stores one image as one or two **streams**:

- a **color stream**, always present, decoding to width x height RGB pixels;
- a **mask stream**, present only on sprites, decoding to width x height alpha bytes.

Each stream begins with its own 20-byte **sub-header**, followed by its **body** (the encoded pixel data). Streams sit back to back; nothing else separates them.

## 2. IDX2 archive container

An IDX2 file is a fixed directory followed by raw member data:

    offset        content
    0x000         "IDX2"
    0x004         version (0x0201)
    0x006         entry count                (283 in the shipping file)
    0x008         total archive size         (= actual file size)
    0x00C         payload start              (= end of the entry table)
    0x010         signature "ELEMENTAL SOFT", NUL-padded
    ...           zero padding
    0x420         entry table: entry count records of 48 bytes
    payload start .. member bytes, concatenated back to back

The entry table starts at 0x420 in the shipping archive, where it occupies exactly `283 * 48 = 0x3510` bytes and ends at 0x3930 = payload_start. Members tile the remainder of the file precisely: sorted by offset there are no gaps and no overlaps, and the final member ends exactly at the total size from 0x008. Extraction therefore needs nothing beyond the table.

### Entry record (48 bytes)

    +0x00   u32   offset of the member from the start of the archive
    +0x04   u32   member size in bytes
    +0x08   u64   type tag: 0 = binary asset, 2 = text source
    +0x10   32    member name, ASCII, NUL-padded

Observed tags: 0 for the 266 `.el`/`.sd`/`.wav` members, 2 for the 17 `.txt` members (whose nature is described next).

## 3. Other files in the archive

### .wav

Ordinary RIFF/WAVE PCM, stored verbatim.

### .txt - obfuscated leftovers

Seventeen members carry scrambled text under an unrecovered cipher (not a single-byte XOR). They look intriguing but are dead weight: the game never reads them. Its resource resolver rewrites any request whose name ends in `.txt` - literally overwriting those four characters with `.sd` - so script data always comes from `main.sd`. Requests ending in `.es` are redirected the same way. Nothing in either format depends on these files.

### main.sd - ELSC1 bytecode

The one `.sd` member is the compiled form of all desktop scripts:

    0x00    4     magic "ELSC"
    0x04    1     format digit '1'
    0x05    u32   constant 2
    0x09    u32   file size in bytes
    0x0D    ...   bytecode

Further parsed fields: u16 counts at +0x0C and +0x0E describing two tables of 8-byte entries, and relative offsets at +0x28/+0x2C pointing at data blocks. The bytecode VM itself is out of scope for this document.

## 4. EL01 image format

### 4.1 Shape of a file

An opaque image holds one stream; a sprite holds two:

    opaque image                       sprite
    +----------------+                 +----------------+
    | outer header   | 16 bytes        | outer header   | 16 bytes
    | directory u32  |  4 bytes        | directory u32×2|  8 bytes
    +----------------+ <- stream 0     +----------------+ <- stream 0
    | sub-header 20B |                 | sub-header 20B |
    | color body     |                 | color body     |
    +----------------+ <- end          +----------------+ <- stream 1
    (file may end with                | sub-header 20B |
     a few pad bytes)                 | mask body      |
                                      +----------------+

The directory entries are offsets relative to the file start, measured such that adding the total header extent (16 + 4·N bytes) yields the actual position of each stream. In every shipped file the first entry is 0 and the second equals the distance from stream 0's start to stream 1's start.

Sprites differ from opaque images in three ways: a second stream, four more header bytes, and a flags bit announcing the mask (see below). Decoders should not assume which is which by stream order alone - the mask is identified by its flags (§4.6).

### 4.2 Outer header and stream directory

    offset  size  field
    0x00    4     magic "EL01"
    0x04    u16   version, 0x0100 in all observed files
    0x06    u16   zero (reserved)
    0x08    u16   N, number of streams: 1 = opaque, 2 = sprite
    0x0A    u16   dir_offset: where the directory starts (= 16 in practice)
    0x0C    u16   hdr_extent = 16 + 4·N (so 20 or 24)
    0x0E    u16   zero (reserved)
    0x10    u32[N] stream_dir: one offset per stream

Stream i begins at file position `stream_dir[i] + hdr_extent`. Two details worth calling out:

- **dir_offset is self-describing indirection.** The loader reads a pointer location from +0x0A and then a value from there; it does not hardcode 16. All shipped files use 16 with stream_dir[0] = 0, meaning the first stream directly follows the header.
- **hdr_extent is not a per-stream header size.** Every stream sub-header is 20 bytes regardless; hdr_extent grows because the directory itself holds N more words. Earlier working notes that assumed "20-byte vs 24-byte stream headers" were misreading this field.

Version note: for version >= 0x0101 the loader additionally copies the four u32s at header +0x10..+0x1F into internal fields - positions occupied by the directory in 0x0100 files. Such files must therefore relocate their directory via dir_offset. No v0x0101 file ships with this title; the meaning of those fields is unknown. Parsers should honor dir_offset rather than assuming 16.

### 4.3 Stream sub-header

Every stream, color or mask, starts with the same 20-byte structure:

    +0x00   u16   zero
    +0x02   u8    flags            <- meaning depends on stream kind!
    +0x03   u8    zero
    +0x04   u32   zero
    +0x08   u32   extent (advisory, see below)
    +0x0C   u16   width
    +0x0E   u16   height
    +0x10   u8    depth            <- decoded pixel depth in bits
    +0x11   u8    npal             <- palette entry count (some codecs)
    +0x12   u8    esc              <- escape byte (some codecs)
    +0x13   u8    zero

Width and height repeat identically in both streams of a sprite.

**depth** states what the body decodes to: 24 for color streams (three bytes per pixel packed in u32 containers), 8 or 4 for masks (one output byte per pixel). On masks it doubles as a variant selector (§4.6).

**extent** declares the distance from this sub-header's start to the next stream's sub-header start; for the final stream it points loosely toward EOF (shipped files show a few bytes of slack). It is advisory: real decoders rely on end markers and pixel counts, and writers may set it approximately.

#### Flags have two dialects

This is the easiest place to go wrong, so here are both meanings together. The same bit positions mean different things depending on whether the stream is a color stream or a mask stream:

    bit    color stream                     mask stream
    0      horizontal delta filter (§4.5)   ┐
    1      vertical delta filter (§4.5)     │
    2      ┐                                ├ codec selector (flags & 0x1F)
    3      ├ encoding selector (& 0x2C)     │ (§4.6)
    4      │ (always clear)                 ┘ (bit 4 set on every mask)
    5      ┘
    7      mask present (sprite marker)     unused

Consequences:

- A mask stream always has `flags & 0x10 != 0`; color streams never do. This is how the game tells streams apart - it scans for bit 4.
- Color flag values observed in shipped assets: 0x04, 05, 06, 0A, 0B, 84, 86, 88, 8A, 8B - i.e. encodings 0x04 and 0x08 combined with delta bits and the mask bit.
- Mask flag values observed: 0x11 (byte-RLE) and 0x12 (palette-indexed RLE).

### 4.4 Color body encodings

The color body starts at sub-header +0x14 (after npal·4 palette bytes when encoding 0x08 says so). The encoding is chosen by `enc = flags & 0x2C`:

    enc    name            idea
    0x00   raw             uncompressed rows, padded to 4 bytes
    0x04   RLE             token stream over literal pixels
    0x08   palette RLE     tokens referencing a small leading palette
    0x0C   grayscale RLE   1-byte gray pixels (two variants, §4.8)
    0x20   LZSS 32bpp      generic compressor output, decoded straight
                           into the pixel surface (§4.8)
    0x24   LZSS planar     generic compressor output in three color planes
                           (§4.8)

Shipped assets use only 0x04 (110 files) and 0x08 (152 files); the others are implemented by the game but specified from disassembly alone (§4.8).

Decoded pixels land in u32 containers laid out in memory as bytes B,G,R,X - i.e. value = R<<16 | G<<8 | B - filling a surface stored **bottom-up**: surface row 0 is the bottom row of the visible image. Decoders advance through the surface linearly, wrapping at width, and stop when the token stream says so rather than counting pixels.

Token grammar shared by 0x04 and 0x08:

    B G R              literal pixel
    esc FF g r         literal pixel whose blue byte equals esc
    esc n B G R        run of n copies          (n in 1..254)
    esc 0              end of body

`esc` is the per-stream escape byte from the sub-header; any value including 0 may be chosen. Because `esc FF` is intercepted as an escape prefix, runs never reach length 255.

#### raw (enc 0x00)

Each row is w·3 bytes of consecutive B,G,R literals followed by padding up to `(w·3 + 3) & ~3`. There is no end marker; the geometry defines the length.

#### RLE (enc 0x04)

Exactly the shared grammar above. Runs read `[esc][count][B][G][R]`.

#### palette RLE (enc 0x08)

The body opens with npal entries of 4 bytes each (B,G,R,pad; pad ignored), followed by tokens:

    g r ...            literals as above, except
    esc FF g r         escaped literal, blue byte = esc
    esc idx count      run: count copies of palette[idx]
    esc count          same, but only when npal == 1 (no index byte)
    esc idx 0 / esc 0  end of body

Note the end condition applies to the **count** position: in multi-palette mode `[esc][idx][0]` terminates whatever the index says, and in single-palette mode `[esc][0]` terminates. Runs cap at 254 since 255 is reserved for the escaped literal. Shipped wallpapers all use npal == 1 - the palette holds a single background colour and index-less runs.

### 4.5 Delta filters

After the color body decodes, unless the encoding was 0x0C (which skips them entirely):

- flags & 0x01 - horizontal pass: within each row, `px[x] ^= px[x-1]` for x = 1..w-1, masked to 24 bits;
- flags & 0x02 - vertical pass: `px[x] ^= px_above[x]` for rows 1..h-1.

Both are XOR prefix filters, hence trivially invertible: an encoder derives its output by subtracting neighbours exactly the same way. The 24-bit mask exists only because pixels ride in 32-bit containers.

### 4.6 Mask streams (alpha)

A mask decodes to w·h bytes, one alpha value per pixel, stored **top-down** (the opposite vertical order from the color surface!). Its codec is selected by `sel = flags & 0x1F`:

    sel   name               body
    0x10  raw                 w·h bytes, verbatim
    0x11  byte-RLE            [value][count] pairs; variant picked by depth
    0x12  palette-indexed     mini-palette plus indexed runs

Other selector values are silently treated as success with a zeroed plane - defensive behaviour worth knowing about when validating suspicious files.

#### byte-RLE (sel 0x11, depth 8)

    v c                c copies of v (c = 0 terminates, c = 1 emits one)

No escape concept; runs cover lengths 1..255.

#### nibble form (sel 0x11, depth 4)

Tokens `[v][count]` with v <= 15 emit `count` copies of the byte `v | v<<4`. A count byte of 0 promotes to a u16 LE count; a resulting count of 0 terminates. Tokens with the high nibble set fall into a defective path in the shipped binary - they consume one byte and emit nothing - so treat this variant as encoder-constrained to low-nibble tokens. No shipped asset uses it.

#### palette-indexed RLE (sel 0x12)

The body opens with npal **bytes** forming a mini-palette (plain values, not 4-byte entries), then a token stream over the remaining bytes:

    b                  emit b literally, if b is not in the mini-palette
    p c                emit mini-palette value p, c times
    p 0 hi lo          extended run: u16 LE count from (lo,hi)
    p 0 0 0 / 0 0      terminate (extended count of 0)

Runs clamp to the remaining pixel budget. Shipped sprites use this with npal in {1, 3, 4}; the majority (74 of 88) use byte-RLE instead.

Masks have no delta passes, no escape byte, and no length beyond w·h.

### 4.7 Composing the final image

The color surface is bottom-up, the mask top-down; composition flips the color rows back upright while walking the mask in file order:

    for Y in 0 .. h-1:                      # visual row, top to bottom
        src_row = h-1-Y                     # surface row (bottom-up)
        for X in 0 .. w-1:
            v = surface[src_row*w + X]
            a = mask ? mask[Y*w + X] : 255  # opaque when unmasked
            out[Y][X] = (v>>16 & 255, v>>8 & 255, v & 255, a)

### 4.8 Encodings not present in shipped assets

Everything in this subsection comes from disassembly only; synthetic-file tests in this repository exercise it.

#### grayscale RLE (enc 0x0C)

Chosen variant depends on flags bit 0. Both variants skip the delta filters regardless of the flag values.

Pure gray, bit 0 clear - one byte per gray pixel:

    v                  gray pixel (v,v,v)
    esc FF             gray pixel of value esc
    esc n v            run of n copies of gray(v), n in 1..254
    esc 0              end

Hybrid gray/color, bit 0 set - carries a mode register cur, initially 0:

    esc 1 v            cur := v; emits nothing
                       (v = 0 returns to gray mode, v > 0 enables color)
    b                  gray(b) when cur == 0,
                       otherwise a color literal reading two more bytes
    esc FF ...         escaped literal, interpreted per current mode
    esc n ...          run of n pixels: [B][G][R] in color mode,
                       a single [value] in gray mode
    esc 0              end

#### LZSS-compressed forms (enc 0x20 and 0x24)

Both wrap a family of byte-oriented LZSS decompressors selected by a leading selector byte in the body.

enc 0x20 decompresses directly into the w·h·4-byte pixel surface, after which delta filters apply normally.

enc 0x24 decompresses to w·h·3 bytes arranged as three contiguous planes - all blue bytes, then all green, then all red - and interleaves them into the surface (alpha stays 0). Selector 1 maps to the fully understood variant:

    loop until output is full:
        t = next input byte
        t < 8:   copy the following t+1 input bytes to the output;
                 input advances by t+2
        t >= 8:  length = t - 7 (1..248); distance = next input byte;
                 copy `length` bytes starting `distance` behind the write
                 position, byte-wise (overlap-safe); input advances by 2

Selectors 2 and 3 are variations with long-literal escapes and multi-byte distances that no shipped asset uses; their exact parameters were not pinned down. Treat output from those selectors as unsupported.

## 5. Reference decoder pseudocode

    decode_el01(file):
        require magic == "EL01"
        ver, _, N, dir_off, extent = u16@4, u16@6, u16@8, u16@0xA, u16@0xC
        dir = u32[N] @ dir_off

        color = sub_header(file, dir[0] + extent)
        surf = decode_color_body(color)          # w*h u32, bottom-up, xRRGGBB
        if color.encoding != 0x0C:               # gray codecs skip filters
            if color.flags & 1: horizontal_delta(surf)
            if color.flags & 2: vertical_delta(surf)

        mask = none
        if N > 1:
            m = sub_header(file, dir[1] + extent)
            require m.flags & 0x10               # this is the mask marker
            mask = decode_mask(m)                # w*h bytes, top-down

        return compose(surf, mask, w, h)         # §4.7

    decode_color_body(s):                        # dispatch on s.flags & 0x2C
        0x00 -> raw24 rows, stride (w*3+3) & ~3
        0x04 -> token grammar of §4.4 (runs carry B,G,R payloads)
        0x08 -> npal*4-byte palette, then §4.4 grammar with palette indices
        0x0C -> flags&1 ? hybrid_gray : pure_gray          (§4.8)
        0x20 -> lzss(body) straight into the 32bpp surface
        0x24 -> lzss(body[1:]) into three planes, interleave (§4.8)

    decode_mask(m):                              # dispatch on m.flags & 0x1F
        0x10 -> copy body
        0x11 -> depth==8 ? byte-RLE : nibble form
        0x12 -> mini-palette indexed RLE

## 6. Worked examples

### Opaque wallpaper (bg_clock.el, 800x600)

    45 4c 30 31   magic "EL01"
    00 01         version 0x0100
    00 00         reserved
    01 00         N = 1 (opaque)
    10 00         dir_offset = 16
    14 00         hdr_extent = 16 + 4*1 = 20
    00 00         reserved
    00 00 00 00   stream_dir[0] = 0   -> stream starts at 0 + 20 = 20

    stream at 20:
    00 00 0a 00   flags = 0x0A = palette RLE + vertical delta
    00 00 00 00   reserved
    98 75 06 00   extent = 0x67598 (last stream: roughly "to end of file")
    20 03 58 02   width 800, height 600
    18 01 80 00   depth 24, npal 1, esc 0x80
    00 00 00 80   palette[0] = B=0 G=0 R=128 (dark red backdrop)
    94 ff ff      first tokens: literals of B=0x94 G=0xff R=0xff ...

Decode: expand the token stream bottom-up, undo the vertical filter, flip. The single-entry palette explains how photographic wallpapers stay small: flat regions become index-less runs, and every pixel starts from one backdrop colour.

### Sprite (bt_bg00.el, 130x180)

    45 4c 30 31   magic "EL01"
    00 01         version 0x0100
    00 00         reserved
    02 00         N = 2 (sprite)
    10 00         dir_offset = 16
    18 00         hdr_extent = 16 + 4*2 = 24
    00 00         reserved
    00 00 00 00   stream_dir[0] = 0      -> color stream at 24
    18 75 00 00   stream_dir[1] = 0x7518 -> mask stream at 0x7518 + 24

    color stream @ 24:
    00 00 84 00   flags = 0x84 = plain RLE + mask present
    ...           extent 0x7518 (= stream_dir[1], as expected)
    82 00 b4 00   width 130, height 180
    18 00 00 00   depth 24, npal 0, esc 0x00
    00 0b ff ff   run token: escape(0x00), count 11,
                  11 copies of B=0 G=0xff R=0xff

    mask stream @ 0x7530:
    00 00 11 00   flags = 0x11 = byte-RLE mask
    ...           same 130x180, depth 8

Composition takes RGB from the flipped color surface and alpha from the top-down mask plane (§4.7).

## A. Validation against shipped assets

Checked across all 262 .el files:

    magic / version          EL01, 0x0100 everywhere
    stream counts            N=1: 174 files, N=2: 88 files
    hdr_extent == 16 + 4*N   262/262
    dir_offset / dir[0]      16 / 0 everywhere
    sub-header zeros         hold for all 350 streams (+0,+1,+3,+4,+0x13)
    color encodings          0x04 in 110 files, 0x08 in 152; none other
    depth byte               24 on every color stream
    npal                     0 (RLE) or 1 (palette RLE), never higher
    mask codecs              74 byte-RLE, 14 palette-indexed; no raw/nibble
    color end markers        found for 262/262 within declared extents
    extent field             matches the next directory entry throughout
    trailing slack           0..9 bytes after the final stream

Two independent implementations agree with this document: a Python decoder written from an earlier draft, and the C tools shipped here (which add the mask, grayscale and rare encodings). Re-encoding every sample image through the C encoder reproduces identical pixels after another decode.

## B. Disassembly provenance

Machine-code locations (KonMenu.exe virtual addresses) supporting the statements above:

    Loader and object model
      EL01 dispatch entry            0x41CE20
      magic / version check          0x41CE58, 0x41CE65
      stream addressing formula      0x41CE88
      mask stream scan (bit 4)       0x4103F0
      mask decode dispatch           0x410420
      mask merge into surface        0x41CDB0
      32bpp DIB allocation           0x41CCC0
      decoder-object init            0x41D0B0

    Color encodings
      dispatcher (flags & 0x2C)      0x4102F0, jump table 0x4103A0,
                                     lookup 0x4103BC
      raw                            0x40FCB0
      RLE                            0x40FD50
      palette RLE                    0x40FFE0
      gray pure / hybrid             0x40FE30 / 0x40FED0
      delta horizontal / vertical    0x40FC00 / 0x40FC60

    Rare paths
      LZSS 32bpp wrapper             0x4100E0
      planar interleave              0x410180
      LZSS selectors 1 / 2 / 3       0x40A100 / 0x40A190 / 0x40A2F0
      LZSS selector dispatch         0x40A080

    Masks
      raw plane                      via memcpy at 0x425DD0 (dispatch 0x410473)
      byte-RLE                       0x4104A0
      nibble form                    0x4104F0 (defect at 0x41054A)
      palette-indexed RLE            0x410570

    Archive and scripts
      ".txt" -> ".sd" redirection    0x407750
      resource resolver              0x4076A0
      ELSC magic check               0x407816
      ELSC writer                    0x41114A
