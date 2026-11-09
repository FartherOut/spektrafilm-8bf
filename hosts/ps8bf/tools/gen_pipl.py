#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""gen_pipl.py — emit the Photoshop PiPL resource for the spektrafilm .8bf.

A PiPL (Plug-in Property List) is a small big-endian property list that
Photoshop/Affinity read from the plug-in DLL *without executing it*, to learn
the plug-in kind, name, entry point and which filter "cases" it handles.

The Windows resource is a user-defined resource whose **type** is the
four-character code ``PiPL`` and whose **id** is arbitrary (Adobe's own samples
range over e.g. 16000/18008; hosts enumerate the type).  See
``res/resource.rc`` (``16000 PiPL "pipl.bin"``).

Byte layout (verified byte-for-byte against the output of Adobe's
``Cnvtpipl.exe`` run under Wine, see ``res/pipl.bin``):

    uint16  1                       "first byte must be 1"
    uint32  0                       kCurrentPiPLVersion
    uint32  property_count
    PIProperty[property_count]

    PIProperty:
      4cc vendor   ('8BIM')
      4cc key
      uint32 index         (0)
      uint32 length        (data length, already padded to 4)
      bytes  data          (padded with NULs to a multiple of 4)

Every multi-byte integer is stored little-endian (so the 4CC '8BIM' appears on
disk as the bytes ``M I B 8``) — this is what ``Cnvtpipl`` writes for the
Windows ``.rc``; Photoshop byte-swaps as needed when reading.

Usage:
  gen_pipl.py [out.pipl.bin]

Defaults to ``hosts/ps8bf/res/pipl.bin`` next to this script.
"""
import os
import struct
import sys

# --- fixed plug-in metadata -------------------------------------------------
PIPL_ID = 16000
PLUGIN_NAME = "spektrafilm"
PLUGIN_VERSION = (1 << 16) | 0          # major << 16 | minor
ENTRY_WIN64 = "PluginMain"              # exported symbol (see plugin.def)

# FilterCaseInfo handling codes (PIPL.r): 4 = inWhiteMat, 1 = outStraightData.
IN_WHITE_MAT = 4
OUT_STRAIGHT = 1
# flags1 bits (PIFilter.h): bit2 filtersLayerMasks, bit1 worksWithBlankData,
# bit0 = 0 -> Photoshop copies source to destination before we run.
FICI_FLAGS1 = 0x06
FICI_FLAGS2 = 0x00
FICI_CASES = 7

# SupportedModes is a 16-bit field, MSB-first, bit b -> image mode (15 - b):
# 15 Bitmap, 14 GrayScale, 13 Indexed, 12 RGB, 11 CMYK, 10 HSL, 9 HSB,
# 8 Multichannel, 7 Duotone, 6 Lab, ... 4 RGB48 (16-bit RGB), ... The slice
# renders 8- and 16-bit RGB; note each image mode needs its own bit (Photoshop
# greys the filter out in 16-bit docs unless RGB48 is advertised).
MODE_RGB = 1 << 12     # image mode 3 (RGBColor, 8-bit)
MODE_RGB48 = 1 << 4    # image mode 11 (RGB48, 16-bit)
MODE_MASK = MODE_RGB | MODE_RGB48


def cc4(code):
    """A four-character code as stored on disk by Cnvtpipl (byte-reversed)."""
    assert len(code) == 4
    return code.encode("ascii")[::-1]


def _pad4(b, extra):
    """Pad b with NULs to a multiple of 4, counting `extra` trailing bytes.

    `cnvtpipl` writes a Pascal string as length-byte + chars + a terminator
    (extra=1) and a C string as chars + NUL (extra=0) before 4-aligning; both
    were confirmed against the ground-truth ``pipl.bin``.
    """
    total = (len(b) + extra + 3) & ~3
    return b + b"\x00" * (total - len(b))


def pstring(s):
    """Pascal string (length byte + chars + terminator), 4-byte aligned."""
    return _pad4(bytes([len(s)]) + s.encode("ascii"), 1)


def cstring_pad(s):
    """C string (NUL terminated), 4-byte aligned."""
    return _pad4(s.encode("ascii") + b"\x00", 0)


def u32le(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def property_(key, data):
    """One PIProperty: vendor/key/index/length/data."""
    assert len(data) % 4 == 0, f"property {key!r} data not 4-aligned"
    return (cc4("8BIM") + cc4(key) + u32le(0) + u32le(len(data)) + data)


def build():
    props = []

    # Kind: '8BFM' = Photoshop filter module.
    props.append(property_("kind", cc4("8BFM")))
    # Name: menu label.
    props.append(property_("name", pstring(PLUGIN_NAME)))
    # Category: Affinity Photo always nests each .8bf under a submenu named
    # after this property, with the menu item labelled by 'name'.  It must
    # therefore differ from the name, or the same word shows twice
    # ("spektrafilm > spektrafilm"); omitting it is not a fix either, because
    # Affinity renders a missing category as an empty submenu rather than a
    # flat entry.  A descriptive category keeps the plug-in to a single
    # "spektrafilm" item under a named group, the convention G'MIC uses
    # (category "GMIC", item "G'MIC-Qt").  Photoshop itself treats an absent
    # category as top-level, but Affinity does not.
    props.append(property_("catg", pstring("Film Simulation")))
    # Version.
    props.append(property_("vers", u32le(PLUGIN_VERSION)))
    # Code: exported entry point for Win64 (key '8664').
    props.append(property_("8664", cstring_pad(ENTRY_WIN64)))
    # SupportedModes: 16-bit mask (big-endian on disk) + 2 reserved bytes.
    props.append(property_("mode", struct.pack(">H", MODE_MASK) + b"\x00\x00"))
    # FilterCaseInfo: 7 filter cases, 4 bytes each.
    fici = bytes([IN_WHITE_MAT, OUT_STRAIGHT, FICI_FLAGS1, FICI_FLAGS2])
    props.append(property_("fici", fici * FICI_CASES))

    out = bytearray()
    out += struct.pack("<H", 1)          # "first byte must be 1"
    out += u32le(0)                      # kCurrentPiPLVersion
    out += u32le(len(props))             # property count
    for p in props:
        out += p
    return bytes(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default = os.path.abspath(os.path.join(here, "..", "res", "pipl.bin"))
    out_path = sys.argv[1] if len(sys.argv) > 1 else default
    data = build()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"gen_pipl: wrote {len(data)} bytes -> {out_path} "
          f"(id {PIPL_ID}, name {PLUGIN_NAME!r})")


if __name__ == "__main__":
    main()
