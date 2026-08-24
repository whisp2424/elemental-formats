#!/usr/bin/env python3
"""Test harness for the el01/idx2 tools.

Everything here is self-contained: the oracles are independent Python
implementations written from SPEC.md, so the suite needs nothing but the
tools and a directory of .el files.

  1. check_samples    - decodes every .el file with the C tool and compares
                        pixels against an independent Python implementation
                        of the color codecs (below), with masks cross-checked
                        against a second Python implementation (SPEC.md 4.6),
  2. check_synthetic  - feeds hand-built EL01 files covering paths absent
                        from shipped assets (raw+deltas, grayscale variants,
                        multi-entry palettes, all mask codecs, planar LZSS)
                        and compares composed output,
  3. check_roundtrip  - decodes each file, re-encodes it, decodes again, and
                        requires identical pixels.

Point --samples PATH (or SAMPLES) at any directory of EL01 files;
the shipped game's assets are simply the most complete set available.
"""

import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(__file__)
TOOL_EL01 = os.path.join(HERE, "..", "bin", "el01")
TMP = os.path.join(HERE, "out")

parser = argparse.ArgumentParser()
parser.add_argument(
    "--samples",
    default=os.environ.get("SAMPLES"),
    help="directory holding the .el files to check",
)
parser.add_argument(
    "--quick",
    type=int,
    nargs="?",
    const=8,
    default=0,
    help="only check the first N sample files",
)
ARGS = parser.parse_args()

if not ARGS.samples or not os.path.isdir(ARGS.samples):
    sys.exit("samples directory not given; pass --samples PATH "
             "(or set SAMPLES)")
SAMPLES = ARGS.samples


def ref_deltas(px, w, h, horiz, vert):
    px = list(px)
    if horiz:
        for y in range(h):
            for x in range(1, w):
                px[y * w + x] = (px[y * w + x] ^ px[y * w + x - 1]) & 0xFFFFFF
    if vert:
        for y in range(1, h):
            for x in range(w):
                px[y * w + x] = (px[y * w + x] ^ px[(y - 1) * w + x]) & 0xFFFFFF
    return px


def ref_decode_color(d, s):
    """Independent implementation of SPEC.md sections 4.4-4.5.

    Covers the encodings present in real assets (raw, RLE, palette RLE) plus
    the delta filters. Returns a bottom-up list of xRRGGBB ints.
    """
    flags = d[s + 2]
    w, h = struct.unpack_from("<HH", d, s + 0xC)
    npal, esc = d[s + 0x11], d[s + 0x12]
    cap = w * h
    body = d[s + 0x14 :]
    enc = flags & 0x2C
    out = []

    if enc == 0x00:
        stride = (w * 3 + 3) & ~3
        for y in range(h):
            row = body[y * stride : y * stride + w * 3]
            for j in range(0, len(row) - 2, 3):
                out.append(row[j + 2] << 16 | row[j + 1] << 8 | row[j])
    elif enc == 0x04:
        pos = 0
        while len(out) < cap and pos < len(body):
            t = body[pos]
            pos += 1
            if t != esc:
                g, b = body[pos], body[pos + 1]
                pos += 2
                out.append(b << 16 | g << 8 | t)
                continue
            second = body[pos]
            pos += 1
            if second == 0xFF:
                g, b = body[pos], body[pos + 1]
                pos += 2
                out.append(b << 16 | g << 8 | esc)
            elif second == 0:
                break
            else:
                b, g, r = body[pos], body[pos + 1], body[pos + 2]
                pos += 3
                out += [r << 16 | g << 8 | b] * min(second, cap - len(out))
    elif enc == 0x08:
        pal = []
        for i in range(npal):
            pal.append(body[i * 4 + 2] << 16 | body[i * 4 + 1] << 8 |
                       body[i * 4])
        pos = npal * 4
        while len(out) < cap and pos < len(body):
            t = body[pos]
            pos += 1
            if t != esc:
                g, b = body[pos], body[pos + 1]
                pos += 2
                out.append(b << 16 | g << 8 | t)
                continue
            second = body[pos]
            pos += 1
            if second == 0xFF:
                g, b = body[pos], body[pos + 1]
                pos += 2
                out.append(b << 16 | g << 8 | esc)
            elif npal == 1:
                if second == 0:
                    break
                out += [pal[0]] * min(second, cap - len(out))
            else:
                count = body[pos]
                pos += 1
                if count == 0 or second >= npal:
                    continue
                out += [pal[second]] * min(count, cap - len(out))
    else:
        raise NotImplementedError(f"reference decoder lacks enc {enc:#x}")

    out += [0] * (cap - len(out))
    if enc != 0x0C:
        if flags & 1 or flags & 2:
            out = ref_deltas(out, w, h, bool(flags & 1), bool(flags & 2))
    return out[:cap], w, h


def decode_png(path):
    out = os.path.join(TMP, "probe.png")
    subprocess.run([TOOL_EL01, "decode", path, "-o", out], check=True,
                   capture_output=True)
    from PIL import Image
    with Image.open(out) as im:
        return im.mode, im.size[0], im.size[1], im.tobytes()


def compose_bytes(surf, mask, w, h):
    """Top-down pixel bytes from a bottom-up xRRGGBB surface (+ top-down mask)."""
    out = bytearray()
    for y in range(h):
        row = surf[(h - 1 - y) * w : (h - 1 - y) * w + w]
        if mask is None:
            for v in row:
                out += bytes(((v >> 16) & 255, (v >> 8) & 255, v & 255))
        else:
            mrow = mask[y * w : (y + 1) * w]
            for v, a in zip(row, mrow):
                out += bytes(((v >> 16) & 255, (v >> 8) & 255, v & 255, a))
    return bytes(out), "RGBA" if mask is not None else "RGB"


def py_decode_mask(d, s):
    """Independent implementation of SPEC.md section 4.6."""
    flags = d[s + 2]
    w, h = struct.unpack_from("<HH", d, s + 0xC)
    depth, npal = d[s + 0x10], d[s + 0x11]
    cap = w * h
    body = d[s + 0x14 :]
    sel = flags & 0x1F
    out = []

    if sel == 0x10:
        out = list(body[:cap])
    elif sel == 0x11 and depth == 8:
        while len(out) < cap and len(body) >= 2:
            v, c = body[0], body[1]
            body = body[2:]
            if c == 0:
                break
            out += [v] * min(c, cap - len(out))
    elif sel == 0x11:
        while len(out) < cap and body:
            t = body[0]
            body = body[1:]
            if t & 0xF0:
                continue
            if not body:
                break
            c = body[0]
            body = body[1:]
            count = c
            if count == 0:
                if len(body) < 2:
                    break
                count = struct.unpack_from("<H", body)[0]
                body = body[2:]
                if count == 0:
                    break
            out += [t | t << 4] * min(count, cap - len(out))
    elif sel == 0x12:
        pal = list(body[:npal])
        body = body[npal:]
        while len(out) < cap and body:
            t = body[0]
            body = body[1:]
            if t not in pal:
                out.append(t)
                continue
            if not body:
                break
            c = body[0]
            body = body[1:]
            count = c
            if count == 0:
                if len(body) < 2:
                    break
                count = struct.unpack_from("<H", body)[0]
                body = body[2:]
                if count == 0:
                    break
            count = min(count, cap - len(out))
            out += [pal[pal.index(t)]] * count
    return out + [0] * (cap - len(out))


def check_samples(files):
    failures = []
    for name in files:
        path = os.path.join(SAMPLES, name)
        data = open(path, "rb").read()
        _, _, ns, tab, ext = struct.unpack_from("<HHHHH", data, 4)
        offs = struct.unpack_from("<%dI" % ns, data, tab)
        ref_surf, ref_w, ref_h = ref_decode_color(data, offs[0] + ext)
        mask = py_decode_mask(data, offs[1] + ext) if ns == 2 else None
        expected, mode = compose_bytes(ref_surf, mask, ref_w, ref_h)

        got_mode, gw, gh, got = decode_png(path)
        if (got_mode, gw, gh, got) != (mode, ref_w, ref_h, expected):
            bad = sum(1 for a, b in zip(got, expected) if a != b)
            failures.append((name, f"{bad} differing bytes"))
    print(f"samples: {len(files)} files checked against the reference "
          f"decoder ({len(files)} masks cross-checked on sprites), "
          f"{len(failures)} failures")
    for name, why in failures[:10]:
        print(f"  FAIL {name}: {why}")
    return not failures


def outer(n_streams, offsets, extent=None):
    extent = extent if extent is not None else 16 + 4 * n_streams
    return (
        b"EL01"
        + struct.pack("<HHHHHH", 0x100, 0, n_streams, 16, extent, 0)
        + struct.pack("<%dI" % n_streams, *offsets)
    )


def subhdr(flags, w, h, body, depth=24, npal=0, esc=0, extent=0):
    return (
        struct.pack("<HBBIIHHBBBB", 0, flags, 0, 0, extent, w, h,
                    depth, npal, esc, 0)
        + body
    )


GRAY = lambda v: v | v << 8 | v << 16


def synth_cases():
    """Each case: (file_bytes, want_surface, want_mask_or_None, w, h).

    want_mask == "FROM_FILE" decodes the mask from the second stream via the
    Python oracle instead of a fixed expectation.
    """
    cases = {}

    enc_px = [0x00030201, 0x00060504, 0x00030201, 0x00060504]
    rows = bytes([1, 2, 3, 4, 5, 6, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0])
    cases["raw_delta"] = (
        outer(1, [0]) + subhdr(0x03, 2, 2, rows),
        ref_deltas(enc_px, 2, 2, True, True), None, 2, 2,
    )

    body = bytes([0x10, 0x80, 0xFF, 0x80, 0x03, 0x20, 0x80, 0x00])
    cases["gray_pure"] = (
        outer(1, [0]) + subhdr(0x0C, 5, 1, body, esc=0x80),
        [GRAY(0x10), GRAY(0x80), GRAY(0x20), GRAY(0x20), GRAY(0x20)],
        None, 5, 1,
    )

    hyb = bytes([
        0x0A,
        0x55, 0x03, 0x20,
        0x55, 0x01, 0x07,
        0x01, 0x02, 0x03,
        0x55, 0x02, 0x05, 0x06, 0x07,
        0x55, 0xFF, 0xAA, 0xBB,
        0x55, 0x01, 0x00,
        0x09,
        0x55, 0x00,
    ])
    cases["gray_hybrid"] = (
        outer(1, [0]) + subhdr(0x0D, 3, 3, hyb, esc=0x55),
        [GRAY(0x0A), GRAY(0x20), GRAY(0x20), GRAY(0x20),
         0x00030201, 0x00070605, 0x00070605,
         0x00BBAA55, GRAY(0x09)],
        None, 3, 3,
    )

    pal = bytes([10, 20, 30, 0, 40, 50, 60, 0])
    tok = bytes([
        1, 2, 3,
        0xEE, 0, 5,
        0xEE, 1, 2,
        0xEE, 0xFF, 0xAA, 0xBB,
        0xEE, 0, 0,
    ])
    p0 = 30 << 16 | 20 << 8 | 10
    p1 = 60 << 16 | 50 << 8 | 40
    want = [0x00030201] + [p0] * 5 + [p1] * 2 + [0x00BBAAEE]
    cases["pal_rle_multi"] = (
        outer(1, [0]) + subhdr(0x08, 4, 4, pal + tok, npal=2, esc=0xEE),
        want + [0] * (16 - len(want)), None, 4, 4,
    )

    planes_in = bytes([1, 2, 3, 4, 5, 6,
                       1, 2, 3, 4, 5, 6,
                       9, 9, 9, 9, 9, 9])
    lz = (bytes([7]) + planes_in[:8]
          + bytes([0x0B, 0x06])
          + bytes([5]) + planes_in[12:])
    comp = bytes([1]) + lz
    b_plane, g_plane, r_plane = (planes_in[0:6], planes_in[6:12],
                                 planes_in[12:18])
    cases["planar_lzss"] = (
        outer(1, [0]) + subhdr(0x24, 3, 2, comp),
        [r << 16 | g << 8 | b
         for b, g, r in zip(b_plane, g_plane, r_plane)],
        None, 3, 2,
    )

    m_rle8 = bytes([7, 3, 9, 1, 0, 0])
    m_nib = bytes([0x0C, 3, 0x00, 5, 0, 0])
    mpal = bytes([170, 187, 204, 221])
    mtok = bytes([17, 170, 3, 187, 0, 2, 0, 204, 0, 0, 0, 0])
    for tag, mbody, extra in (
        ("mask_rle8", m_rle8, {}),
        ("mask_nibble", m_nib, {"depth": 4}),
        ("mask_pal", mtok, {"sel": 0x12, "npal": 4}),
    ):
        sel = extra.get("sel", 0x11)
        depth = extra.get("depth", 8)
        npal = extra.get("npal", 0)
        data = (
            outer(2, [0, 56])
            + subhdr(0x00, 4, 3, bytes(36))
            + subhdr(sel, 4, 3, mbody, depth=depth, npal=npal)
        )
        cases[tag] = (data, [0] * 12, "FROM_FILE", 4, 3)

    px = [(1, 2, 3), (4, 5, 6), (7, 8, 9),
          (10, 11, 12), (13, 14, 15), (16, 17, 0)]
    color = bytes([1, 2, 3, 4, 5, 6, 0, 0,
                   7, 8, 9, 10, 11, 12, 0, 0,
                   13, 14, 15, 16, 17, 0, 0, 0])
    sprite_surf = [r << 16 | g << 8 | b for b, g, r in px]
    cases["compose_sprite"] = (
        outer(2, [0, 44])
        + subhdr(0x00, 2, 3, color)
        + subhdr(0x10, 2, 3, bytes([255, 128, 64, 0, 10, 200]), depth=8),
        sprite_surf,
        [255, 128, 64, 0, 10, 200],
        2, 3,
    )

    return cases


def check_synthetic():
    failures = []
    cases = synth_cases()
    for name, (data, want_surf, want_mask, w, h) in cases.items():
        path = os.path.join(TMP, name + ".el")
        with open(path, "wb") as f:
            f.write(data)

        if want_mask == "FROM_FILE":
            mask_off = struct.unpack_from("<I", data, 20)[0] + 24
            want_mask = py_decode_mask(data, mask_off)

        expected, mode = compose_bytes(want_surf, want_mask, w, h)
        got_mode, gw, gh, got = decode_png(path)
        if (got_mode, gw, gh, got) != (mode, w, h, expected):
            failures.append(name)
    print(f"synthetic: {len(cases)} files checked, {len(failures)} failures")
    for name in failures:
        print(f"  FAIL {name}")
    return not failures


def check_roundtrip(files):
    """decode -> encode -> decode must reproduce identical pixels."""
    failures = []
    for name in files:
        src = os.path.join(SAMPLES, name)
        mode, w, h, pixels = decode_png(src)
        alpha = mode == "RGBA"
        depth = 4 if alpha else 3

        pam = os.path.join(TMP, "rt.pam")
        with open(pam, "wb") as f:
            f.write(("P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL 255\n"
                     "TUPLTYPE %s\nENDHDR\n"
                     % (w, h, depth,
                        "RGB_ALPHA" if alpha else "RGB")).encode())
            f.write(pixels)

        el = os.path.join(TMP, "rt.el")
        subprocess.run([TOOL_EL01, "encode", pam, "-o", el], check=True,
                       capture_output=True)

        ns = struct.unpack_from("<H", open(el, "rb").read(10), 8)[0]
        if (ns == 2) != alpha:
            failures.append((name, f"stream count {ns} for alpha={alpha}"))
            continue

        rt_mode, rw, rh, rt_pixels = decode_png(el)
        if (rt_mode, rw, rh, rt_pixels) != (mode, w, h, pixels):
            failures.append((name, "pixels changed across round-trip"))
    print(f"round-trip: {len(files)} files decoded -> encoded -> decoded, "
          f"{len(failures)} failures")
    for name, why in failures[:10]:
        print(f"  FAIL {name}: {why}")
    return not failures


def main():
    os.makedirs(TMP, exist_ok=True)
    files = sorted(n for n in os.listdir(SAMPLES) if n.endswith(".el"))
    if ARGS.quick:
        files = files[: ARGS.quick]
    ok = check_synthetic() & check_roundtrip(files) & check_samples(files)
    print("ALL PASS" if ok else "FAILURES PRESENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
