#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""merge_bandpass_hanatos2025.py -- add the archived Hanatos2025 film bandpass to
a data pack.

The RGB->raw spectral-upsampling method Hanatos2025 folds a per-profile,
per-(wavelength, channel) bandpass into the film sensitivities. The OFX build
carries that bandpass as `data.bandpass_hanatos2025` (81 x 3) inside every film
profile it archived, but the pack format this engine consumes predates the
method and has no such field.

The engine treats the field as optional: a profile without it makes the
Hanatos2025 method fall back to Hanatos2026 (with a warning), which is exactly
the right behaviour for the profiles that genuinely have no archived bandpass
(the print papers and the two B&W films). This script copies the field from the
OFX archive into the pack's profiles so the stocks that *do* ship one become
selectable.

It is idempotent, leaves every other key byte-identical (the pack is written the
same way Python's json.dumps serialises it today), and only ever adds the field.

Usage:
    tools/merge_bandpass_hanatos2025.py [PACK_DIR] [--archive DIR] [--check]
                                        [--quiet]

    PACK_DIR       pack to merge into (default spektra-core/data/pack)
    --archive DIR  OFX archive profiles (default
                   upstream/spektrafilm-ofx/Resources/data/profiles/archive)
    --check        do not write; exit 1 if any profile still lacks the field
    --quiet        only report when something was (or would be) added; errors
                   still print. The build routes do not pass it: the merge line
                   is part of the build report, including the "already present"
                   case that shows the pack is current.

The pack directory is a derived, git-ignored build input, so this script mutates
a scratch tree rather than anything under version control. Normal builds reach it
through tools/sync_pack.py, which copies the pristine submodule pack and then
calls merge() here; running this script directly is for checking a pack by hand.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
DEFAULT_PACK = os.path.join(REPO, "spektra-core", "data", "pack")
DEFAULT_ARCHIVE = os.path.join(
    REPO, "upstream", "spektrafilm-ofx", "Resources", "data",
    "profiles", "archive")

FIELD = "bandpass_hanatos2025"


def load_archive_bandpass(archive_dir):
    """stem -> bandpass value, for every archived profile that carries one."""
    out = {}
    if not os.path.isdir(archive_dir):
        return out
    for name in sorted(os.listdir(archive_dir)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(archive_dir, name)
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        band = (doc.get("data") or {}).get(FIELD)
        if band:
            out[os.path.splitext(name)[0]] = band
    return out


def merge(pack_dir, archive_dir, check, quiet):
    profiles = os.path.join(pack_dir, "profiles")
    if not os.path.isdir(profiles):
        print(f"merge_bandpass_hanatos2025: no profiles/ under {pack_dir}",
              file=sys.stderr)
        return 2
    bandpass = load_archive_bandpass(archive_dir)
    added, present, missing = [], [], []
    for name in sorted(os.listdir(profiles)):
        if not name.endswith(".json"):
            continue
        stem = os.path.splitext(name)[0]
        path = os.path.join(profiles, name)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        doc = json.loads(text)
        data = doc.get("data")
        if not isinstance(data, dict):
            continue
        if FIELD in data:
            present.append(stem)
            continue
        band = bandpass.get(stem)
        if band is None:
            # No archived bandpass for this profile (print paper / B&W): the
            # method falls back for it, which is correct. Not an error.
            missing.append(stem)
            continue
        data[FIELD] = band
        if not check:
            if json.dumps(doc) == text:
                present.append(stem)
                continue
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(json.dumps(doc))
        added.append(stem)

    verb = "would add" if check else "added"
    if not quiet or added:
        print(f"merge_bandpass_hanatos2025: {verb} {FIELD} to {len(added)} profiles, "
              f"{len(present)} already present, {len(missing)} without an archived "
              f"bandpass (fall back to Hanatos2026)")
    for stem in added:
        print(f"  + {stem}")
    if check:
        # A profile is only actionable if the archive has a bandpass for it;
        # "missing" ones are expected and fine.
        return 1 if added else 0
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", nargs="?", default=DEFAULT_PACK)
    ap.add_argument("--archive", default=DEFAULT_ARCHIVE)
    ap.add_argument("--check", action="store_true",
                    help="report only; exit 1 if a profile could still be merged")
    ap.add_argument("--quiet", action="store_true",
                    help="only report when something was (or would be) added")
    args = ap.parse_args(argv)
    return merge(args.pack, args.archive, args.check, args.quiet)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
