#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Emit the Mallett2019 sRGB reflectance basis as a C header.

The basis is the one and only per-method constant the engine needs for the
Mallett2019 RGB->raw path; everything else (the film sensitivity, the film's
reference illuminant and the green midgray normalisation) is already in the
pack/profile and is combined at sim-build time exactly as the OFX plug-in's
makeMallettRawMatrix() does.

Source of truth: the Rust port's ``MALLETT2019_BASIS`` (81 rows of 3), which is
baked from ``colour.recovery.MSDS_BASIS_FUNCTIONS_sRGB_MALLETT2019`` aligned to
the 380..780 nm / 5 nm grid (SF_NWL rows).  ``colour`` is not a runtime
dependency here -- the Rust constant is.  It lives in ``upstream/spektrafilm-rs``,
a reference-only tree that nothing in the build reads, so it is fetched with
``tools/fetch-sources.sh`` rather than vendored as a submodule.

Usage:
    tools/gen_mallett_basis.py [--check]

--check regenerates into memory and fails (exit 1) if the committed header
differs, for a cheap CI drift guard.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RUST = REPO / "upstream" / "spektrafilm-rs" / "crates" / "spektrafilm-core" / "src" / "mallett.rs"
OUT = REPO / "spektra-core" / "third_party" / "darktable" / "spektra_mallett_basis.h"

NWL = 81
_NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")


def parse_basis(path: Path) -> list[list[float]]:
    text = path.read_text(encoding="utf-8")
    start = text.index("MALLETT2019_BASIS")
    open_bracket = text.index("[", text.index("=", start))
    depth = 0
    end = None
    for i in range(open_bracket, len(text)):
        if text[i] == "[":
            depth += 1
        elif text[i] == "]":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        raise SystemExit(f"unterminated array in {path}")
    values = [float(v) for v in _NUM.findall(text[open_bracket:end])]
    if len(values) != NWL * 3:
        raise SystemExit(f"expected {NWL * 3} values, got {len(values)} in {path}")
    return [values[i * 3 : i * 3 + 3] for i in range(NWL)]


def render(basis: list[list[float]]) -> str:
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* spektra_mallett_basis.h -- Mallett2019 sRGB reflectance basis.",
        " *",
        " * GENERATED FILE -- do not edit. Produce with tools/gen_mallett_basis.py",
        " * from spektrafilm-rs/crates/spektrafilm-core/src/mallett.rs",
        " * (MALLETT2019_BASIS), itself baked from",
        " * colour.recovery.MSDS_BASIS_FUNCTIONS_sRGB_MALLETT2019 aligned to the",
        " * 380..780 nm / 5 nm grid (SF_NWL = 81 rows).",
        " *",
        " * GPL-3.0-or-later with the rest of the engine: the generator is ours, and",
        " * the table it reads comes from spektrafilm-rs, which labels itself",
        " * GPL-3.0. The numbers themselves are the published Mallett (2019) sRGB",
        " * reflectance basis.",
        " *",
        " * The Mallett2019 RGB->raw path combines this constant with the film",
        " * sensitivity and the film's reference illuminant at sim-build time:",
        " *",
        " *   M[out][in] = sum_l sens[l][out] * basis[l][in] * illum[l] / midgray_g",
        " *",
        " * matching the OFX plug-in's makeMallettRawMatrix() /",
        " * generate_profile_curves.py _mallett_basis_illuminant(). */",
        "#ifndef SPEKTRA_MALLETT_BASIS_H",
        "#define SPEKTRA_MALLETT_BASIS_H",
        "",
        "static const double SF_MALLETT2019_BASIS[81][3] = {",
    ]
    for row in basis:
        lines.append("  { " + ", ".join(f"{v:.17g}" for v in row) + " },")
    lines += ["};", "", "#endif /* SPEKTRA_MALLETT_BASIS_H */", ""]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify the committed header, do not write")
    args = ap.parse_args()

    if not RUST.is_file():
        print(f"error: {RUST.relative_to(REPO)} not found.\nspektrafilm-rs is a "
              f"reference-only tree: run tools/fetch-sources.sh to clone it",
              file=sys.stderr)
        return 1

    header = render(parse_basis(RUST))
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != header:
            print(f"drift: {OUT} is out of date; rerun tools/gen_mallett_basis.py", file=sys.stderr)
            return 1
        print(f"ok: {OUT} matches {RUST.name}")
        return 0

    OUT.write_text(header, encoding="utf-8")
    print(f"wrote {OUT} ({NWL} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
