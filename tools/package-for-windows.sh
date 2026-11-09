#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# package-for-windows.sh — make a clean source bundle for building the .8bf
# on a Windows machine.
#
# The bundle is the tracked tree (`git ls-files`) plus the two pinned upstream
# trees the pack derivation reads, flattened to plain files:
#
#   hosts/ps8bf/                 the adapter: src, pi, res, third_party/imgui,
#                                CMakeLists.txt, build.ps1, INSTALL.md, tests
#   spektra-core/                engine + shims + sim_port
#   tools/                       sync_pack.py, merge_bandpass_hanatos2025.py,
#                                gen_assets.py, gen_mallett_basis.py
#   docs/ ROADMAP.md SOURCES.md CHANGELOG.md
#   LICENSE THIRD-PARTY-NOTICES.md
#   spektra-core/third_party/darktable-spektrafilm/
#                                the pristine 0.3.3 pack sync_pack.py copies
#   upstream/spektrafilm-ofx/Resources/data/profiles/archive/
#                                the archived Hanatos2025 bandpass it merges in
#
# Tracked-only, deliberately: build output, the derived spektra-core/data/pack
# and its stamp, __pycache__ and stray .ppm renders are all untracked, so
# listing the index keeps them out without an exclude list that has to be
# maintained as the tree grows. The derived pack is *not* shipped: build.ps1
# runs tools/sync_pack.py, which re-derives it from the two upstream trees
# above, so a Windows build proves the derivation route works instead of
# trusting a copy of its output.
#
# The two upstream trees are flattened in because they are submodules: a plain
# `git clone` on Windows leaves them empty, and tar has no notion of submodules
# at all. Copying their tracked contents to the same relative paths makes the
# defaults in tools/sync_pack.py resolve with no flags and no `git submodule
# update` on the Windows side.
#
# Usage:
#   tools/package-for-windows.sh [output.tar.gz]
# Default output: ./sf-win-src.tar.gz
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$repo/sf-win-src.tar.gz}"

cd "$repo"

pack_src="spektra-core/third_party/darktable-spektrafilm"
ofx="upstream/spektrafilm-ofx"
archive="$ofx/Resources/data/profiles/archive"

# Both pack sources must be present and non-empty before bundling. An empty
# submodule would still produce a tarball, and the Windows user would find out
# only at the pack step — after installing a toolchain on the strength of the
# instructions in INSTALL.md.
for tree in "$pack_src/packs" "$archive"; do
  if [ ! -d "$tree" ] || [ -z "$(ls -A "$tree")" ]; then
    echo "error: $tree is missing or empty." >&2
    echo "       initialise the submodules first:  git submodule update --init" >&2
    exit 1
  fi
done

list="$(mktemp)"
trap 'rm -f "$list"' EXIT

# NUL-separated throughout, so no path can be mis-split. The two submodule paths
# are excluded from the superproject listing: `git ls-files` reports a gitlink as
# a plain path, and tar would then archive that directory recursively — which for
# `upstream/spektrafilm-ofx` means the whole 44 MiB upstream worktree, its own
# build output included, on top of the files listed below.
#
# Tracked files inside each submodule are then listed relative to the
# superproject, which is where tar runs; anything untracked in there (an
# upstream's own build output) stays out, and so does a submodule's .git gitfile,
# which would point at a repository the bundle does not carry.
git ls-files -z -- . \
  ":(exclude)spektra-core/third_party/darktable-spektrafilm" \
  ":(exclude)upstream" > "$list"
git -C "$pack_src" ls-files -z | sed -z "s|^|$pack_src/|" >> "$list"
git -C "$ofx" ls-files -z -- Resources/data/profiles/archive \
  | sed -z "s|^|$ofx/|" >> "$list"

# -h dereferences symlinks. The tree has none today; it is here so that if one
# is ever introduced, the bundle carries a real file rather than a link Windows
# cannot follow.
tar -czhf "$out" --null -T "$list"

# --- verify the artifact, not the intent ------------------------------------
# A bundle that extracts to a tree which cannot build is worse than no bundle,
# and the two ways it breaks are invisible until a rebuild on the other machine:
# a file build.ps1 or CMake needs, or one of the pack sources. So check the
# archive's own listing.
have="$(tar -tzf "$out")"
files_only="$(grep -v '/$' <<<"$have")"
count_files() { grep -c "^$1/" <<<"$files_only" || true; }

# Every path once. An accidental duplicate is not merely larger: the last copy
# wins on extraction, so a stale tree earlier in the archive could silently
# shadow the one that was meant to be shipped.
dupes="$(sort <<<"$files_only" | uniq -d)"
if [ -n "$dupes" ]; then
  echo "error: bundle lists the same path more than once:" >&2
  head -5 <<<"$dupes" | sed 's/^/  /' >&2
  exit 1
fi

# Nothing generated. Tracked-only is what guarantees this, so assert it rather
# than trust it: a bundle carrying a stale build tree or the derived pack would
# be a bundle whose build silently reuses output instead of deriving it.
for pat in '(^|/)build/' '^spektra-core/data/pack/' '__pycache__/' '(^|/)\.git(/|$)' '\.ppm$'; do
  hit="$(grep -E -m1 "$pat" <<<"$files_only" || true)"
  if [ -n "$hit" ]; then
    echo "error: bundle carries generated content matching $pat (e.g. $hit)" >&2
    exit 1
  fi
done

missing=0
for f in hosts/ps8bf/build.ps1 \
         hosts/ps8bf/CMakeLists.txt \
         hosts/ps8bf/third_party/imgui/imgui.cpp \
         hosts/ps8bf/third_party/imgui/LICENSE.txt \
         tools/sync_pack.py \
         tools/merge_bandpass_hanatos2025.py \
         tools/gen_assets.py \
         spektra-core/third_party/darktable/spektra_core.c \
         LICENSE \
         THIRD-PARTY-NOTICES.md; do
  grep -qxF "$f" <<<"$files_only" || { echo "  missing from bundle: $f" >&2; missing=1; }
done
for tree in "$pack_src/packs" "$archive"; do
  [ "$(count_files "$tree")" -gt 0 ] \
    || { echo "  missing from bundle: $tree (0 files)" >&2; missing=1; }
done
[ "$missing" -eq 0 ] || { echo "error: bundle incomplete, refusing to publish $out" >&2; exit 1; }

echo "wrote $out"
printf '  files      %s total, of which %s pack source and %s archived bandpass\n' \
  "$(grep -c . <<<"$files_only")" \
  "$(count_files "$pack_src")" "$(count_files "$archive")"
printf '  size       %s\n' "$(du -h "$out" | cut -f1)"
echo
echo "copy it to Windows, extract, then:"
echo "    cd hosts\\ps8bf"
echo '    .\build.ps1'
echo "the first build derives spektra-core\\data\\pack from the two bundled"
echo "upstream trees, and stages LICENSE + THIRD-PARTY-NOTICES.md into dist\\."
