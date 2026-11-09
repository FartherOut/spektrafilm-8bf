#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""gen_assets.py -- embed a spektrafilm data pack into the .8bf plugin.

Emits into <outdir>:
    asset_ids.h   SF_ASSET_RES[] resource-id table for assets_win.c
                  ({ path, id }, base id 3000), plus SF_ASSET_RES_N.
    pack.rc       RCDATA entries embedding each pack file as a Win32 resource
                  (compiled with resource.rc; the spectra LUT is re-encoded to
                  float32 so the engine loads it directly).
    pack_list.h   C lists of film/paper profile stems + manifest option strings
                  (the "use" field of each profile classifies it: filming/printing).

Usage:
  gen_assets.py [pack_dir] [outdir]
  # pack_dir defaults to spektra-core/data/pack (the 0.3.3 submodule pack)
"""
import json
import os
import struct
import sys


def half_to_float_bits(h):
    """Replicates spektra_sim.c _sf_half_to_float bit-for-bit (exact mapping)."""
    sign = (h & 0x8000) << 16
    exp = (h >> 10) & 0x1F
    mant = h & 0x3FF
    if exp == 0:
        if mant == 0:
            return sign
        m, shift = mant, 0
        while not (m & 0x400):
            m <<= 1
            shift += 1
        e = 127 - 15 - shift + 1
        return sign | (e << 23) | ((m & 0x3FF) << 13)
    if exp == 0x1F:
        return sign | 0x7F800000 | (mant << 13)  # inf / NaN payload
    return sign | ((exp + 127 - 15) << 23) | (mant << 13)


def maybe_reencode_spectra(pack_dir, outfile):
    """The 0.3.3 pack stores its 192x192x81 spectral LUT as float16 and the
    engine converts it per-sample (dtype 1). The embedded copy is re-encoded
    to float32 (dtype 0) so the engine reads it with a single memcpy instead
    of burning the conversion on every sample. The header hash is metadata and
    is untouched, so the pack's identity is unchanged; the values are
    bit-identical. Returns the file path to embed (None when nothing to
    re-encode)."""
    src = os.path.join(pack_dir, "spectra_lut.f32")
    with open(src, "rb") as f:
        b = f.read(32)
        if b[:4] != b"SFS2":
            return src
        hdr_version, d0, d1, d2, dtype, h, id_len = struct.unpack(
            "<iiiiiiI", b[4:32])
        if dtype != 1:
            return src
        f.seek(32)
        ids = f.read(id_len)
        payload = f.read()
    count = d0 * d1 * d2
    assert len(payload) == count * 2, (
        f"spectra_lut.f32 payload {len(payload)} != {count}*2")
    h16 = struct.unpack("<%dH" % count, payload)
    out = struct.pack("<%dI" % count, *(half_to_float_bits(x) for x in h16))
    new = bytearray()
    new += b"SFS2"
    new += struct.pack("<iiiiiiI", hdr_version, d0, d1, d2, 0, h, id_len)
    new += ids
    new += out
    with open(outfile, "wb") as f:
        f.write(bytes(new))
    return outfile


def profile_stem(path):
    return os.path.splitext(os.path.basename(path))[0]


def pretty_label(stem):
    """Fallback display name for a profile that ships no info.name:
    'kodak_portra_160' -> 'Kodak Portra 160'."""
    return " ".join(w[:1].upper() + w[1:] for w in stem.split("_") if w)


def classify(pack_dir):
    """Returns (films, papers) as lists of (stem, display_label). The label is
    the profile's own `info.name` (e.g. "Kodak Portra 160") when present, so the
    host UI shows a human name rather than the on-disk stem."""
    profiles = sorted(
        f for f in os.listdir(os.path.join(pack_dir, "profiles"))
        if f.endswith(".json")
    )
    films, papers = [], []
    for f in profiles:
        with open(os.path.join(pack_dir, "profiles", f), encoding="utf-8") as fh:
            info = (json.load(fh) or {}).get("info", {})
        # "use" is still/cine; the stage field distinguishes film from paper
        stage = (info.get("stage") or "").lower()
        stem = profile_stem(f)
        label = (info.get("name") or "").strip() or pretty_label(stem)
        if "print" in stage:
            papers.append((stem, label))
        else:
            films.append((stem, label))
    return films, papers


def collect(pack_dir, outdir):
    """Return (rels, abspaths): the pack-relative names and the files to embed.

    spectra_lut.f32 is re-encoded dtype 1 -> 0 (float16 -> float32) so the
    plugin's per-instance load is a memcpy, not a 3 M-sample convert loop.
    """
    rels = ["pack.json", "spectra_lut.f32"]
    for f in sorted(os.listdir(os.path.join(pack_dir, "profiles"))):
        if f.endswith(".json"):
            # Asset keys are guest-style paths served by sf_asset_lookup; they
            # MUST use '/' regardless of host. os.path.join would emit '\' on
            # Windows, producing invalid C escapes ("\k", "\f") in the emitted
            # key and breaking every lookup.
            rels.append("profiles/" + f)
    spectra_embed = maybe_reencode_spectra(
        pack_dir, os.path.join(outdir, "spectra_lut_f32.f32"))

    def abspath_for(r):
        if r == "spectra_lut.f32":
            return spectra_embed
        # Native separators for the file rc.exe/windres actually opens.
        return os.path.join(pack_dir, *r.split("/"))

    return rels, [abspath_for(r) for r in rels]


def write_pack_list_h(outdir, pack_dir):
    """Shared: film/paper stems + pretty display labels + manifest option
    strings (links into params). The stem arrays stay in sync with the profile
    file names; SF_FILMS_LABELS / SF_PAPERS_LABELS are the same length and align
    index-for-index, for the combo boxes."""
    em = '\\"'
    films, papers = classify(pack_dir)
    film_stems = [s for s, _ in films]
    paper_stems = [s for s, _ in papers]
    film_labels = [l for _, l in films]
    paper_labels = [l for _, l in papers]

    def cstr(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')

    def manifest_list(items):
        return ", ".join(em + cstr(x) + em for x in items)

    def emit_pairs(fh, base, stems, labels):
        fh.write(f"#define {base}_COUNT {len(stems)}\n")
        fh.write(f"static const char *const {base}[{base}_COUNT] = {{\n")
        for x in stems:
            fh.write(f'  "{cstr(x)}",\n')
        fh.write("};\n\n")
        fh.write(f"static const char *const {base}_LABELS[{base}_COUNT] = {{\n")
        for x in labels:
            fh.write(f'  "{cstr(x)}",\n')
        fh.write("};\n\n")

    with open(os.path.join(outdir, "pack_list.h"), "w") as fh:
        fh.write("/* generated by tools/gen_assets.py -- profile stems + display labels */\n")
        fh.write("#pragma once\n\n")
        emit_pairs(fh, "SF_FILMS", film_stems, film_labels)
        emit_pairs(fh, "SF_PAPERS", paper_stems, paper_labels)
        fh.write("#define SF_MANIFEST_FILMS \\\n")
        fh.write(f'  "{manifest_list(film_stems)}"\n\n')
        fh.write("#define SF_MANIFEST_PAPERS \\\n")
        fh.write(f'  "{manifest_list(paper_stems)}"\n')
    return film_stems, paper_stems


def emit_win(pack_dir, outdir):
    rels, abspaths = collect(pack_dir, outdir)
    keys = ["pack/" + r for r in rels]

    # ---- asset_ids.h -------------------------------------------------------
    # Base resource id 3000; assets_win.c looks each pack path up in
    # RCDATA(id) via this table. Ids must stay in sync with pack.rc below.
    with open(os.path.join(outdir, "asset_ids.h"), "w") as fh:
        fh.write("/* generated by tools/gen_assets.py --win -- pack resource ids */\n")
        fh.write("#pragma once\n\n")
        fh.write("typedef struct sf_asset_res_t\n{\n"
                 "  const char *path;   /* normalised path, e.g. pack/pack.json */\n"
                 "  int         id;     /* RCDATA resource id (find by RT_RCDATA) */\n"
                 "} sf_asset_res_t;\n\n")
        fh.write("static const sf_asset_res_t SF_ASSET_RES[] = {\n")
        for i, k in enumerate(keys):
            fh.write(f'  {{ "{k}", {3000 + i} }},\n')
        fh.write("};\n")
        fh.write("#define SF_ASSET_RES_N "
                 "((int)(sizeof(SF_ASSET_RES) / sizeof(SF_ASSET_RES[0])))\n")

    # ---- pack.rc -----------------------------------------------------------
    # RCDATA from a filename. Paths are native and absolute (this file is
    # generated into the build tree, never committed) so cwd does not matter:
    # backslash paths on Windows for rc.exe, forward-slash on Linux for windres.
    #
    # rc.exe processes C-style escapes inside the quoted filename, so native
    # backslashes must be doubled -- otherwise "\t" in "F:\temp\..." is read as a
    # TAB and "\b" in "\build" as a backspace (error RC2135). Forward-slash
    # paths on Linux/windres contain no backslashes, so this is a no-op there.
    with open(os.path.join(outdir, "pack.rc"), "w") as fh:
        fh.write("/* generated by tools/gen_assets.py --win -- embedded data pack */\n")
        for i, ap in enumerate(abspaths):
            p = os.path.abspath(ap).replace("\\", "\\\\")
            fh.write(f'{3000 + i} RCDATA "{p}"\n')

    films, papers = write_pack_list_h(outdir, pack_dir)
    total = sum(os.path.getsize(a) for a in abspaths)
    print(f"gen_assets --win: {len(rels)} files, {total/1048576:.1f} MiB -> {outdir}")
    print(f"gen_assets --win: {len(films)} films, {len(papers)} papers")
    print(f"gen_assets --win: resource ids {3000}..{3000 + len(rels) - 1}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, ".."))

    # `--win` is kept as a no-op alias so existing build scripts keep working.
    args = [a for a in sys.argv[1:] if a != "--win"]

    pack_dir = args[0] if len(args) > 0 else os.path.join(
        repo, "spektra-core", "data", "pack")
    outdir = args[1] if len(args) > 1 else os.path.join(
        repo, "hosts", "ps8bf", "build", "gen")
    os.makedirs(outdir, exist_ok=True)

    emit_win(pack_dir, outdir)


if __name__ == "__main__":
    main()
