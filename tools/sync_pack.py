#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""sync_pack.py -- materialise the data pack the build embeds.

The engine ships with a film/paper pack baked in as Win32 resources. Its input
is a *derived* tree, never a hand-edited one:

    spektra-core/third_party/darktable-spektrafilm/packs/<ver>   pinned submodule
                             |  (pristine, CC BY-SA 4.0, copied verbatim)
                             v
    + bandpass_hanatos2025 from the OFX profile archive           see below
                             v
    spektra-core/data/pack                                       git-ignored

The pack format predates the Hanatos2025 RGB->raw method, which folds a
per-channel bandpass into the film sensitivities; that bandpass only exists in
the OFX profile archive. tools/merge_bandpass_hanatos2025.py adds it, and this
script is the single entry point that does copy-then-merge in the right order.

Why it has to be derived: the merge is what makes 20 film profiles render the
way the shipped plug-in renders them. A build that silently falls back to the
pristine pack instead succeeds, warns about nothing, and produces a filter whose
results differ -- so every build route runs this script and stops on failure
rather than choosing a different pack directory.

Usage:
    tools/sync_pack.py [--dest DIR] [--src DIR] [--archive DIR]
                       [--force] [--allow-missing-archive] [--check]
                       [--print-src] [--quiet]

    --dest DIR     working pack to write   (default spektra-core/data/pack)
    --src DIR      pack to copy from       (default: the single
                   darktable-spektrafilm/packs/<ver> that exists)
    --archive DIR  OFX archive profiles    (default
                   upstream/spektrafilm-ofx/Resources/data/profiles/archive)
    --force        re-derive even if the stamp says the pack is current
    --allow-missing-archive
                   derive the pack without the OFX bandpass instead of failing
    --check        do not write; exit 1 if the pack is missing or out of date
    --print-src    print the resolved source pack path and exit
    --quiet        no output unless something fails or changes. The build routes
                   deliberately do NOT pass it: what the pack derivation did is
                   part of the build report.

Re-derivation is skipped when nothing changed: a stamp file records the content
hash of the source pack, the archived bandpasses and these two scripts, so a
plain rebuild leaves the pack (and therefore gen_assets) untouched. File mtimes
are preserved on the copy for the same reason.

The stamp lives beside the pack, not inside it: gen_assets embeds every file it
finds in the pack directory, so a stray file there would end up in the binary.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import merge_bandpass_hanatos2025 as bandpass  # noqa: E402  (same directory)

SUBMODULE = os.path.join(
    REPO, "spektra-core", "third_party", "darktable-spektrafilm")
PACKS_DIR = os.path.join(SUBMODULE, "packs")
DEFAULT_DEST = os.path.join(REPO, "spektra-core", "data", "pack")
DEFAULT_ARCHIVE = bandpass.DEFAULT_ARCHIVE
SUBMODULE_HINT = "git submodule update --init spektra-core/third_party/darktable-spektrafilm"
ARCHIVE_HINT = "git submodule update --init upstream/spektrafilm-ofx"


def fail(msg):
    print(f"sync_pack: {msg}", file=sys.stderr)
    return 1


def shown(path):
    """Repo-relative when it is inside the repo, absolute otherwise."""
    rel = os.path.relpath(path, REPO)
    return path if rel == ".." or rel.startswith(".." + os.sep) else rel


def resolve_src(explicit):
    """The one pack version in the submodule, or an actionable error."""
    if explicit:
        return os.path.abspath(explicit), None
    if not os.path.isdir(PACKS_DIR):
        return None, (f"no pack submodule at {os.path.relpath(SUBMODULE, REPO)}\n"
                      f"  -> {SUBMODULE_HINT}")
    versions = sorted(d for d in os.listdir(PACKS_DIR)
                      if os.path.isfile(os.path.join(PACKS_DIR, d, "pack.json")))
    if not versions:
        return None, f"no packs/<version>/pack.json under {os.path.relpath(PACKS_DIR, REPO)}"
    if len(versions) > 1:
        return None, ("several pack versions in the submodule: "
                      f"{', '.join(versions)}\n"
                      "  -> pass the one to use with --src")
    return os.path.join(PACKS_DIR, versions[0]), None


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_fingerprint(root):
    """Content hash of a directory: relative path + sha256 of each file."""
    entries = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            entries.append(f"{rel} {sha256_file(path)}")
    return hashlib.sha256("\n".join(entries).encode()).hexdigest(), len(entries)


def stamp_value(src, archive):
    """What the derived pack depends on: source pack + archived bandpasses +
    the scripts that produce it. Any change here means the pack must change."""
    src_hash, n_files = tree_fingerprint(src)
    bands = bandpass.load_archive_bandpass(archive)
    band_hash = hashlib.sha256(
        "\n".join(f"{k} {json.dumps(bands[k], sort_keys=True)}"
                  for k in sorted(bands)).encode()).hexdigest()
    tools = " ".join(sha256_file(os.path.join(HERE, name))
                     for name in ("sync_pack.py", "merge_bandpass_hanatos2025.py"))
    return {"src": src_hash, "src_files": n_files, "bands": band_hash,
            "band_profiles": len(bands), "tools": tools}


def read_stamp(stamp_path):
    try:
        with open(stamp_path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def write_stamp(stamp_path, value):
    with open(stamp_path, "w", encoding="utf-8") as fh:
        json.dump(value, fh, indent=1, sort_keys=True)
        fh.write("\n")


def verify(dest, src):
    """A copy interrupted halfway must not pass as a pack."""
    want = {os.path.relpath(os.path.join(dirpath, name), src)
            for dirpath, _, filenames in os.walk(src) for name in filenames}
    have = {os.path.relpath(os.path.join(dirpath, name), dest)
            for dirpath, _, filenames in os.walk(dest) for name in filenames}
    if want - have:
        return f"pack is missing {len(want - have)} file(s) after the copy"
    try:
        with open(os.path.join(dest, "pack.json"), encoding="utf-8") as fh:
            json.load(fh)
    except (OSError, ValueError) as exc:
        return f"pack.json unreadable after the copy: {exc}"
    return None


def sync(dest, src, archive, force, check, quiet):
    stamp_path = dest + ".stamp"
    want = stamp_value(src, archive)
    current = read_stamp(stamp_path)
    summary = (f"{shown(dest)} is current ({want['src_files']} files, "
               f"{want['band_profiles']} archived bandpasses)")

    if current == want:
        if check:
            if not quiet:
                print(f"sync_pack: {summary}")
            return 0
        if not force:
            if not quiet:
                print(f"sync_pack: {summary} -- nothing to do")
            return 0
    elif check:
        reason = "pack is out of date" if current else "pack has not been derived yet"
        print(f"sync_pack: {reason} -- run tools/sync_pack.py", file=sys.stderr)
        return 1

    if not quiet:
        print(f"sync_pack: {shown(src)} -> "
              f"{shown(dest)}")
    shutil.rmtree(dest, ignore_errors=True)
    # copystat keeps source mtimes: a re-derive that changes nothing must not
    # make every downstream generated asset look stale.
    shutil.copytree(src, dest, copy_function=shutil.copy2)

    if bandpass.merge(dest, archive, check=False, quiet=quiet):
        return fail("bandpass merge failed")

    problem = verify(dest, src)
    if problem:
        return fail(problem)
    write_stamp(stamp_path, want)
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", default=DEFAULT_DEST)
    ap.add_argument("--src")
    ap.add_argument("--archive", default=DEFAULT_ARCHIVE)
    ap.add_argument("--force", action="store_true",
                    help="re-derive even when the stamp says the pack is current")
    ap.add_argument("--allow-missing-archive", action="store_true",
                    help="build without the Hanatos2025 bandpass instead of failing")
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if the pack is missing or stale")
    ap.add_argument("--print-src", action="store_true",
                    help="print the resolved source pack directory and exit")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    src, err = resolve_src(args.src)
    if err:
        return fail(err)
    if args.print_src:
        print(src)
        return 0
    if not os.path.isdir(args.archive):
        if args.allow_missing_archive:
            if not args.quiet:
                print(f"sync_pack: no OFX archive at "
                      f"{shown(args.archive)} -- building without "
                      f"the Hanatos2025 bandpass", file=sys.stderr)
        else:
            return fail("no OFX profile archive "
                        f"({shown(args.archive)})\n"
                        f"  -> {ARCHIVE_HINT}\n"
                        "     or pass --allow-missing-archive to derive the pack "
                        "without the Hanatos2025 bandpass; the film profiles that "
                        "ship one then fall back to Hanatos2026")
    return sync(os.path.abspath(args.dest), src, args.archive,
                args.force, args.check, args.quiet)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
