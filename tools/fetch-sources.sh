#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-sources.sh — clone the reference-only upstreams into upstream/ (see
# SOURCES.md). Nothing in the build reads these trees, so they are git-ignored
# and unpinned: they are what we consult when porting, plus the one constant
# tools/gen_mallett_basis.py regenerates from. Anything the build itself reads
# is a real submodule instead — git submodule update --init.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
dest="$repo/upstream"
mkdir -p "$dest"

clone() { # <dir> <url> [ref]
  if [ -d "$dest/$1/.git" ]; then
    echo "$1: already cloned at $(git -C "$dest/$1" rev-parse --short HEAD)" \
         "(rm -rf upstream/$1 and rerun to refresh)"
  elif [ $# -ge 3 ]; then
    echo "cloning $2 @ $3 -> upstream/$1"
    git clone --depth 1 --branch "$3" "$2" "$dest/$1"
  else
    echo "cloning $2 -> upstream/$1"
    git clone --depth 1 "$2" "$dest/$1"
  fi
}

clone spektrafilm    https://github.com/andreavolpato/spektrafilm.git
clone vkdt           https://github.com/hanatos/vkdt.git
# Pinned to the tag the committed Mallett basis header was generated from:
# gen_mallett_basis.py --check compares that header against this tree, so a
# moving default branch would report drift the moment upstream touched the file.
clone spektrafilm-rs https://github.com/turbasvin/spektrafilm-rs.git v0.1.2

echo "done."
