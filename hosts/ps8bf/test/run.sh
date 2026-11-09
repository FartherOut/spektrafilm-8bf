#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# run.sh — build and run the ps8bf Linux mock-host harness.
#
#   1. property tests for the coupler diffusion (A2 channel isolation, A3 tail)
#      and the host<->engine adapter (params, sample<->float, one-tile loop);
#   2. an end-to-end 8-bit render compared byte-for-byte with tools/refrender
#      on the same input (the locked parity target);
#   3. the threaded path (sf_parallel) compared byte-for-byte with serial.
#
# Both the mock host and refrender resolve the data pack under the relative
# name "pack", so they run from spektra-core/data.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
core="$repo/spektra-core"
data="$core/data"
work="${TMPDIR:-/tmp}/opencode/sf-harness"
mkdir -p "$work"

# Derive the pack before anything reads it: the harness resolves it under the
# relative name "pack", and the Hanatos2025 comparison below only bites once the
# archived bandpass is merged in. No-op when the pack is already current, and its
# one status line is part of the report rather than hidden.
python3 "$repo/tools/sync_pack.py"

make -C "$here" build/harness
make -C "$core" build/refrender

echo "== selftest (coupler diffusion + adapter) =="
( cd "$data" && "$here/build/harness" selftest )

echo
echo "== end-to-end vs tools/refrender (8-bit) =="
python3 - "$work/in.ppm" <<'PY'
import sys
W = H = 64
buf = bytearray()
for y in range(H):
    for x in range(W):
        buf += bytes(((x * 4) & 0xff, (y * 4) & 0xff, ((x + y) * 2) & 0xff))
with open(sys.argv[1], "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (W, H))
    f.write(buf)
PY
printf '{}' > "$work/params.json"

( cd "$data" && "$here/build/harness" render "$work/in.ppm" "$work/harness.ppm" )
( cd "$data" && "$core/build/refrender" "$work/params.json" "$work/in.ppm" "$work/refrender.ppm" )

if cmp -s "$work/harness.ppm" "$work/refrender.ppm"; then
  echo "  OK: mock .8bf output is byte-identical to tools/refrender"
else
  echo "  FAIL: mock .8bf output differs from tools/refrender"
  cmp "$work/harness.ppm" "$work/refrender.ppm" || true
  exit 1
fi

echo
echo "== RGB->raw method: Mallett2019 / Hanatos2025 vs tools/refrender =="
# The method is a shared sp_params key, so the mock .8bf (via the params block)
# and tools/refrender (via the JSON) must agree byte-for-byte, and each method
# must actually change the render relative to the Hanatos2026 default.
printf '{"rgb_to_raw_method": 1}' > "$work/params_mallett.json"
( cd "$data" && "$here/build/harness" render-method "$work/in.ppm" "$work/harness_mallett.ppm" 1 )
( cd "$data" && "$core/build/refrender" "$work/params_mallett.json" "$work/in.ppm" "$work/refrender_mallett.ppm" )
if cmp -s "$work/harness_mallett.ppm" "$work/refrender_mallett.ppm"; then
  echo "  OK: Mallett2019 matches refrender"
else
  echo "  FAIL: Mallett2019 differs from refrender"
  cmp "$work/harness_mallett.ppm" "$work/refrender_mallett.ppm" || true
  exit 1
fi
if cmp -s "$work/harness.ppm" "$work/harness_mallett.ppm"; then
  echo "  FAIL: Mallett2019 did not change the render (method not wired?)"
  exit 1
else
  echo "  OK: Mallett2019 changes the render vs the Hanatos2026 default"
fi

# Hanatos2025 needs the profile's archived per-channel bandpass; without it the
# engine falls back to Hanatos2026 (with a warning), so the test only bites when
# tools/merge_bandpass_hanatos2025.py has populated the pack.
if grep -q '"bandpass_hanatos2025": \[\[' "$data/pack/profiles/kodak_portra_400.json" 2>/dev/null; then
  printf '{"rgb_to_raw_method": 0}' > "$work/params_hanatos2025.json"
  ( cd "$data" && "$here/build/harness" render-method "$work/in.ppm" "$work/harness_hanatos2025.ppm" 0 )
  ( cd "$data" && "$core/build/refrender" "$work/params_hanatos2025.json" "$work/in.ppm" "$work/refrender_hanatos2025.ppm" )
  if cmp -s "$work/harness_hanatos2025.ppm" "$work/refrender_hanatos2025.ppm"; then
    echo "  OK: Hanatos2025 matches refrender"
  else
    echo "  FAIL: Hanatos2025 differs from refrender"
    cmp "$work/harness_hanatos2025.ppm" "$work/refrender_hanatos2025.ppm" || true
    exit 1
  fi
  if cmp -s "$work/harness.ppm" "$work/harness_hanatos2025.ppm"; then
    echo "  FAIL: Hanatos2025 did not change the render (bandpass not applied?)"
    exit 1
  else
    echo "  OK: Hanatos2025 changes the render vs the Hanatos2026 default"
  fi
else
  echo "  note: pack has no bandpass_hanatos2025; Hanatos2025 falls back to"
  echo "        2026. Run tools/merge_bandpass_hanatos2025.py to enable it."
fi

echo
echo "== threaded path is byte-identical to serial (sf_parallel) =="
# A larger frame so the per-stage split actually runs (the runner stays serial
# below ~64k pixels). SPEKTRAFILM_THREADS=4 forces the split even on a 1-core
# box, so this exercises the multi-range path and its byte-identity guarantee.
python3 - "$work/big.ppm" <<'PY'
import sys
W, H = 260, 260
buf = bytearray()
for y in range(H):
    for x in range(W):
        buf += bytes(((x * 3 + y) & 0xff, (x ^ y) & 0xff, ((x * y) >> 3) & 0xff))
with open(sys.argv[1], "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (W, H))
    f.write(buf)
PY
( cd "$data" && "$here/build/harness" render "$work/big.ppm" "$work/big-serial.ppm" >/dev/null )
( cd "$data" && SPEKTRAFILM_THREADS=4 "$here/build/harness" render-parallel \
    "$work/big.ppm" "$work/big-parallel.ppm" >/dev/null )
( cd "$data" && SPEKTRAFILM_THREADS=1 "$here/build/harness" render-parallel \
    "$work/big.ppm" "$work/big-t1.ppm" >/dev/null )

if cmp -s "$work/big-serial.ppm" "$work/big-parallel.ppm"; then
  echo "  OK: 4-thread output is byte-identical to serial"
else
  echo "  FAIL: 4-thread output differs from serial"
  cmp "$work/big-serial.ppm" "$work/big-parallel.ppm" || true
  exit 1
fi

if cmp -s "$work/big-serial.ppm" "$work/big-t1.ppm"; then
  echo "  OK: SPEKTRAFILM_THREADS=1 overrides to serial, byte-identical"
else
  echo "  FAIL: SPEKTRAFILM_THREADS=1 output differs from serial"
  exit 1
fi

echo
echo "all harness checks passed"

