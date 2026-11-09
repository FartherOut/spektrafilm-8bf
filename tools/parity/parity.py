#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Parity harness: exercises the .8bf mock host vs tools/refrender and
prints machine-readable PASS/FAIL lines. This is the local parity probe
used by the build/update workflow (ROADMAP §9)."""
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CORE = REPO / "spektra-core"
TEST = REPO / "hosts" / "ps8bf" / "test"
DATA = CORE / "data"


def run(*argv, cwd=None, env=None):
    return subprocess.run(list(argv), cwd=cwd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    tmp = Path(tempfile.mkdtemp(prefix="sf-parity-"))
    make = run("make", cwd=str(TEST))
    if make.returncode != 0:
        print("BUILD_FAIL\n" + make.stdout)
        return 1
    make2 = run("make", "build/refrender", "-C", str(CORE))
    if make2.returncode != 0:
        print("REFRENDER_BUILD_FAIL\n" + make2.stdout)
        return 1

    proc = run(str(TEST / "build/harness"), "selftest", cwd=str(DATA))
    if proc.returncode != 0:
        print("SELFTEST_FAIL\n" + proc.stdout)
        return 1
    print("selftest: PASS")

    # 8-bit e2e vs refrender on the locked 64x64 probe.
    probe = tmp / "in.ppm"
    buf = bytearray()
    for y in range(64):
        for x in range(64):
            buf += bytes((((x * 4) & 0xff), ((y * 4) & 0xff), (((x + y) * 2) & 0xff)))
    probe.write_bytes(b"P6\n64 64\n255\n" + buf)
    params = tmp / "params.json"
    params.write_bytes(b"{}")
    out_h = tmp / "harness.ppm"
    out_r = tmp / "refrender.ppm"
    h = run(str(TEST / "build/harness"), "render", str(probe), str(out_h), cwd=str(DATA))
    r = run(str(CORE / "build/refrender"), str(params), str(probe), str(out_r), cwd=str(DATA))
    if h.returncode != 0 or r.returncode != 0:
        print("RENDER_FAIL\n" + h.stdout + r.stdout)
        return 1
    if sha256_file(out_h) != sha256_file(out_r):
        print(f"PARITY_FAIL\nharness={sha256_file(out_h)}\nrefrender={sha256_file(out_r)}")
        return 1
    digest = sha256_file(out_h)
    print(f"e2e_8bit: PASS sha256={digest} (64x64, empty params)")

    # Threaded identity 260x260.
    big = tmp / "big.ppm"
    buf = bytearray()
    for y in range(260):
        for x in range(260):
            buf += bytes((((x * 3 + y) & 0xff), ((x ^ y) & 0xff), (((x * y) >> 3) & 0xff)))
    big.write_bytes(b"P6\n260 260\n255\n" + buf)
    s1 = tmp / "serial.ppm"; p4 = tmp / "parallel.ppm"; p1 = tmp / "t1.ppm"
    run(str(TEST / "build/harness"), "render", str(big), str(s1), cwd=str(DATA))
    run(str(TEST / "build/harness"), "render-parallel", str(big), str(p4), cwd=str(DATA),
        env={**os.environ, "SPEKTRAFILM_THREADS": "4"})
    run(str(TEST / "build/harness"), "render-parallel", str(big), str(p1), cwd=str(DATA),
        env={**os.environ, "SPEKTRAFILM_THREADS": "1"})
    if not (sha256_file(s1) == sha256_file(p4) == sha256_file(p1)):
        print("THREAD_IDENTITY_FAIL")
        return 1
    print(f"thread_identity: PASS sha256={sha256_file(s1)} (260x260)")
    print("parity: ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
