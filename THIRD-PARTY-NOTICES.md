# Third-party notices

spektrafilm-8bf is a film-simulation filter (`.8bf`) for Photoshop-compatible hosts.
The code this project writes is **GPL-3.0-or-later**; the grant is in
[`LICENSE`](LICENSE) and every owned source file repeats it as an SPDX line.

This file collects what the licences of *everything else* — in the repository and
in the shipped binary — ask to have reproduced, and says where each obligation is
already met. It is meant to travel with the plug-in, so the shipping build
(`hosts/ps8bf/build.ps1`) copies it and `LICENSE` next to `spektrafilm.8bf` into
`hosts/ps8bf/dist/`.

Two licences actually shape a distribution:

- **darktable's GPL-3.0-or-later** for the engine: notices stay attached to the
  files, and the whole work stays under the same licence.
- **CC BY-SA 4.0** for the film/paper data pack: attribution must travel with
  every copy and every derivative, in any format, and the licence names "an app's
  About screen" as a place it belongs. Our pack is a *modified* derivative, so
  the modification note travels too.

Everything else is permissive (MIT, BSD-2-Clause, public domain) or consulted at
maintenance time without being compiled or shipped.

## What ships inside `spektrafilm.8bf`

| Component | Licence | Where it lives | What it requires, and where it is satisfied |
|---|---|---|---|
| darktable `spektra` stage and support code | GPL-3.0-or-later | `spektra-core/third_party/darktable/` | Own notices kept verbatim in each file; this file names the tree. [§1](#1-darktable-the-spektra-stage--gpl-30-or-later) |
| spektrafilm film/paper data pack | CC BY-SA 4.0 + author's preamble | derived into `spektra-core/data/pack`, embedded as resources | Author, source URL, licence and modification note on both About surfaces. [§2](#2-the-data-pack--cc-by-sa-40--authors-preamble) |
| Dear ImGui 1.93 (full release tree vendored, two backend files patched; built from core + Win32 / DX11 backends) | MIT | `hosts/ps8bf/third_party/imgui/` | `LICENSE.txt` kept unmodified; full text reproduced here. [§6](#6-dear-imgui-193--mit) |
| `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h` (inside ImGui) | public domain / MIT, dual | same tree | Header of each file states both grants; nothing further required. [§6](#6-dear-imgui-193--mit) |
| Mallett 2019 sRGB reflectance basis table | numbers are published data; the header we ship is GPL-3.0-or-later | `spektra-core/third_party/darktable/spektra_mallett_basis.h` | Banner records where the table was taken from. [§5](#5-spektrafilm-rs--gpl-30-reference-only) |
| `.8bf` interface declarations | ours; no Adobe licence involved | `hosts/ps8bf/pi/` | Nothing to pass on — no Adobe file is in the tree. [§7](#7-the-8bf-interface-no-adobe-code) |

Not in the binary, but in the repository: the pinned submodule
[`upstream/spektrafilm-ofx`](https://github.com/chaert-s/spektrafilm-ofx)
(GPL-3.0), read at pack-derivation time for its archived bandpass [§4](#4-spektrafilm-ofx--gpl-30),
and the git-ignored reference clones fetched by `tools/fetch-sources.sh` [§3](#3-spektrafilm-andrea-volpato--gpl-30),
[§5](#5-spektrafilm-rs--gpl-30-reference-only), [§8](#8-vkdt--johannes-hanika--bsd-2-clause-reference-only).

## 1. darktable: the `spektra` stage — GPL-3.0-or-later

The engine is the `spektra` spectral film-simulation stage from
[darktable](https://github.com/darktable-org/darktable), ported file-by-file into
`spektra-core/third_party/darktable/` (`spektrafilm.c`, `spektra_core.{c,h}`,
`spektra_sim.{c,h}`, `grain.h`, `imagebuf.c`, `gaussian.{c,h}`), plus the small
shims under `spektra-core/shims/` that replace darktable's host coupling.

Each vendored file keeps darktable's own notice verbatim (only the copyright
years differ from file to file — 2012-2026, 2016-2025, 2026):

> This file is part of darktable,
> Copyright (C) 2026 darktable developers.
>
> darktable is free software: you can redistribute it and/or modify
> it under the terms of the GNU General Public License as published by
> the Free Software Foundation, either version 3 of the License, or
> (at your option) any later version.

That "or any later version" is why this project may grant GPL-3.0-or-later rather
than GPL-3.0-only.

`spektra-core/shims/dt_stubs.c` additionally holds `dt_gaussian_kernel_1d` and
`dt_gaussian_yvv_coeffs` copied **verbatim** from darktable's
`src/common/gaussian.c` (master @ `8c64bf7ca9a8`) so the CPU blur stays
bit-identical to darktable's; those two functions remain © the darktable
developers, and the file's header says so.

## 2. The data pack — CC BY-SA 4.0 + author's preamble

The film and paper profiles embedded in the binary come from
[darktable-org/darktable-spektrafilm](https://github.com/darktable-org/darktable-spektrafilm)
(pinned submodule, `packs/0.3.3`), which repackages Andrea Volpato's spektrafilm
profiles unmodified. The licence text is kept byte-for-byte at
`spektra-core/third_party/darktable-spektrafilm/LICENSE` — its author asks that
the file not be changed — and it requires that every copy and derivative preserve
the author's name, the canonical source URL, the licence notice, and a note of
what was modified and by whom.

The attribution, in the form the licence suggests:

```
Derived from spektrafilm by Andrea Volpato
https://github.com/andreavolpato/spektrafilm
Licensed CC BY-SA 4.0
Modified by this project: the archived Hanatos2025 bandpass carried by the
spektrafilm OFX plug-in's profile archive is folded into the film
sensitivities of 20 profiles (tools/merge_bandpass_hanatos2025.py, run by
tools/sync_pack.py). Everything else is the unmodified 0.3.3 pack.
```

Both About surfaces show this: the dialog's License section (reached from its
About button), and the host-invoked About, which is the plug-in's answer to
`filterSelectorAbout` for hosts that ask a plug-in about itself instead of opening
its dialog.
The share-alike condition is satisfied by the same grant — the pack stays
CC BY-SA 4.0 and the plug-in that embeds it is GPL-3.0-or-later, which the pack's
preamble anticipates for tools built on top of it.

The author's preamble also asks, beyond the legal floor, that the name
"spektrafilm" not be used as branding for a derived product. This project is named
after the engine it implements and says so on the About screen; the plug-in is not
presented as an official spektrafilm product.

## 3. spektrafilm (Andrea Volpato) — GPL-3.0

<https://github.com/andreavolpato/spektrafilm> — the original Python research
implementation and the profile-generation foundation the data pack descends from.
Consulted as a reference implementation (git-ignored clone,
`tools/fetch-sources.sh`); no code from it is compiled into the plug-in.

Its `LICENSE` is the plain GPLv3 text; its `CITATION.cff` declares
`GPL-3.0-or-later`. Either way the plug-in's own grant satisfies it, which matters
because the author's stated boundary for a derivative is wider than the usual one:

> Derivatve [sic] work includes any software, plugin, or tool that incorporates
> spektrafilm code or is directly inspired by its methods.

We ship no spektrafilm code, but the plug-in implements its model, so it is at
least "inspired by its methods" on the author's reading — and this project is
already GPL-3.0-or-later source, which is what that ask comes to.

The same `CITATION.cff` asks for an acknowledgement rather than a fee:

> Acknowledge spektrafilm in plugin descriptions, marketing, or credits (e.g.
> "spectral film modeling powered by `spektrafilm`" …).

The About dialog's first credit line uses that example wording — "Spectral film
modelling powered by spektrafilm." — with the repository URL under it.

`CITATION.cff` also records the author's affiliation as KTH Royal Institute of
Technology, Sweden. That is bibliographic metadata for academic citation: the
`preferred-citation` entry omits it and the requested credit line does not include
it, so it is recorded here rather than taking a line in the dialog. See
[`SOURCES.md`](SOURCES.md) for the same note in context.

## 4. spektrafilm OFX — GPL-3.0

<https://github.com/chaert-s/spektrafilm-ofx>, pinned as a submodule at
`upstream/spektrafilm-ofx` (`86476afc`). We read its
`Resources/data/profiles/archive` at pack-derivation time to fold the archived
`bandpass_hanatos2025` into the pack, and use its parameter surface and tooltips
as the reference for our own dialog. No source file from it is compiled into the
`.8bf`.

Its `Legal/` directory holds three notices that are worth distinguishing, because
two of them do **not** apply to what this project builds:

- `SPEKTRAFILM_OFX_LICENSE.txt` governs *official native OFX binary
  distributions* by Aedan Diez, and says explicitly that nothing in it limits the
  rights granted by the repository's GPL-3.0 source licence for user-built
  binaries. We build from GPL-3.0 source and ship no official binary, so it does
  not reach us.
- `SPEKTRAFILM_OFX_LUT_LICENSE.txt` governs *LUT files exported by that plug-in*.
  We export no LUTs and embed none of theirs.
- `THIRD_PARTY_NOTICES.txt` is the upstream's own third-party list. Three of its
  entries also describe our build — Volpato's spektrafilm, the Hanika Hanatos 2025
  upsampling model, Mallett & Yuksel 2019 — and those acknowledgements are
  reproduced in the sections above and below. Its other two do not reach us: the
  vendored OpenFX SDK headers (BSD-3-Clause, plus an X Consortium notice inside
  `ofxKeySyms.h`) belong to the OFX host layer, and this `.8bf` has none
  [§9](#9-not-used).

## 5. spektrafilm-rs — GPL-3.0 (reference only)

<https://github.com/turbasvin/spektrafilm-rs>, a Rust port, labels its repository
GPL-3.0. It is consulted at maintenance time, not compiled or shipped. One thing
it does supply is provenance: `crates/spektrafilm-core/src/mallett.rs`
(`MALLETT2019_BASIS`) is the table `tools/gen_mallett_basis.py` bakes into
`spektra-core/third_party/darktable/spektra_mallett_basis.h`, which *is* compiled
in. The generated header's banner records that chain, and the underlying numbers
are the published Mallett (2019) sRGB reflectance basis.

## 6. Dear ImGui 1.93 — MIT

The parameter dialog is [Dear ImGui](https://github.com/ocornut/imgui) with the
Win32 and Direct3D 11 backends, vendored under `hosts/ps8bf/third_party/imgui/`
and compiled into the plug-in (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp`, `backends/imgui_impl_win32.cpp`,
`backends/imgui_impl_dx11.cpp`). Those six are what the plug-in is built from:
the preprocessor opens fourteen files in that tree and nothing else in it is
compiled or linked. What is *vendored* is wider — the complete v1.93.0 release
tree, 304 files at upstream commit `c1aaac65`, kept whole so it can be diffed
against an upstream release and upgraded by copying a tree rather than
reconstructing a file list (`SOURCES.md` splits which files are which). Two of
those files are modified here, both backends and both to keep GPU and desktop
composition DLLs out of the static import table; they carry a
`[SPEKTRAFILM PATCH]` comment at the change and `SOURCES.md` gives each reason.
MIT is GPL-compatible, so linking it into a GPL-3.0-or-later work is permitted,
and MIT permits modifying the code; its obligation is to keep the copyright and
permission notice in all copies of the tree, which `LICENSE.txt` does
unmodified, reproduced below. The About dialog credits it as "Dear ImGui: MIT".

The full notice, from `hosts/ps8bf/third_party/imgui/LICENSE.txt`:

```
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The three `imstb_*.h` headers ImGui bundles — Sean Barrett's `stb_rect_pack`,
`stb_textedit` and `stb_truetype` (© 2017) — each end with "This software is
available under 2 licenses -- choose whichever you prefer": MIT, or public domain
under the Unlicense. Either grant covers shipping them in a GPL binary, and each
header carries both texts verbatim, so nothing further is required.

## 7. The `.8bf` interface: no Adobe code

A `.8bf` plug-in and its host agree on a published binary layout: the host fills
in a `FilterRecord` and calls the exported `PluginMain` with a pointer to it.
`hosts/ps8bf/pi/pi_types.h` and `hosts/ps8bf/pi/pifilter.h` declare that layout as
this project's own work, written from Adobe's published SDK headers as the
reference, with 121 compile-time asserts pinning `offsetof(FilterRecord, field)`
so a mismatch breaks the build instead of corrupting pixels. The PiPL resource is
generated locally by `hosts/ps8bf/tools/gen_pipl.py`.

**No Adobe SDK file is vendored, distributed, or needed to build**, so there is
nothing Adobe-licensed to pass on. Adobe's headers and samples are mirrored
publicly at [`AdobeDocs/photoshop-cpp-sdk`](https://github.com/AdobeDocs/photoshop-cpp-sdk)
under a proprietary Adobe SDK License; they served only as the public description
of the layout. See the ABI section of
[`hosts/ps8bf/README.md`](hosts/ps8bf/README.md#about-the-8bf-abi).

## 8. vkdt / Johannes Hanika — BSD-2-Clause (reference only)

The film sensitivities in the data pack follow the Hanatos 2025 spectral
irradiation model from [Johannes Hanika's vkdt](https://github.com/hanatos/vkdt)
(`src/pipe/modules/filmsim/`), which is BSD-2-Clause,
"copyright 2019 johannes hanika". We consult that tree at maintenance time
(`tools/fetch-sources.sh`) and compile nothing from it, so its redistribution
condition is not triggered; the model is acknowledged here and in the data pack's
own provenance because the look depends on it.

Worth spelling out, because vkdt's own readme warns that parts of it are GPL-3
and that its LGPL and public-domain parts should be handled with care: the
repository ships both `LICENCE.bsd2` and `LICENCE.gpl3`, and its rule is that the
BSD grant covers a file "if not clearly marked otherwise in the respective source
files". No file under `src/pipe/modules/filmsim/` carries a per-file notice, so
the BSD default covers the whole module we read; the only licence wording inside
it is in `readme.md` and `mklut-profiles.py`, and both concern the CC BY-SA 4.0
spektrafilm profile data that [§2](#2-the-data-pack--cc-by-sa-40--authors-preamble)
already covers. The parts of vkdt under another grant (`rawspeed` and `ffmpeg`
under LGPLv2, `nuklear` public domain) sit in other directories, are not read by
this project, and are not linked by the binary, whose only static imports are
`USER32`, `GDI32` and `KERNEL32`. The direction of travel is safe either way:
this project is GPL-3.0-or-later, and BSD-2-Clause imposes no term that licence
forbids.

## 9. Not used

Stated because they are easy to assume from the surrounding ecosystem:

- **cimgui** is not vendored and not used. The dialog talks to Dear ImGui's C++
  API directly. The name still greps green in this repository, but only inside
  Dear ImGui's own `docs/`, where upstream offers it as a route to language
  bindings we do not take.
- **No Adobe SDK**, in any form [§7](#7-the-8bf-interface-no-adobe-code).
- **No OFX host layer, no OpenCL, no GPU code path.** The `.8bf` runs the CPU
  engine; Direct3D 11 is used only to draw the dialog, and it is loaded at runtime
  rather than linked.

## Keeping this file honest

When a dependency is added, vendored, updated or dropped, change this file in the
same commit, and re-check the two About surfaces in
`hosts/ps8bf/src/sf_gui_imgui.cpp` and `hosts/ps8bf/src/plugin_main.c`: the pack
attribution in particular is a licence condition, not a courtesy.
