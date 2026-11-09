# spektra-core — the engine layer

Status: **current-state, 2026-11-07.** This directory is the shipping engine
of the `.8bf` (and of the Linux harness): the darktable `spektra` stage
extracted to run standalone, plus the port layer that the host adapter talks
to. It began life as the Phase 0 extraction spike; that history is kept at
the bottom, because its lessons are still load-bearing.

## Layout

```
spektra-core/
  third_party/darktable/            vendored engine sources, kept unmodified
    spektra_core.{c,h}              spatial effects (halation, DIR blur,
                                    diffusion, scanner, glare, boost) + shared maths
    spektra_sim.{c,h}               per-pixel colour chain (the engine proper)
    grain.h                         production-grain primitives (CPU/GPU shared)
    gaussian.{c,h}, imagebuf.c      darktable references (parity source of truth)
    spektrafilm.c                   dt module glue (reference only, not built)
  third_party/darktable-spektrafilm/  data-pack repo (submodule, pinned)
  data/pack                         active pack - DERIVED, git-ignored, written
                                    only by tools/sync_pack.py (see pack.stamp)
  sim_port/                         the port layer the adapter builds on
    sp_chain.{c,h}                  stage orchestration + chain-side clamps
    sp_color.{c,h}                  colour spaces and transfers
    sp_params.{c,h}                 the shared parameter key surface (JSON keys
                                    match the OFX side; refrender reads the same)
  shims/                            darktable-compat layer
    common/                         dt-compat headers
    dt_stubs.c                      alloc / copy / log / gaussian kernels
    portability/                    the standalone runtime: json_mini (replaces
                                    json-glib), glib_mini, pc_json, and
                                    stdio_asset.c + assets.h, which map the
                                    engine's file reads onto the .8bf's embedded
                                    RCDATA pack at runtime
  spike/spike_main.c                historical Phase 0 driver (pack->profiles->
                                    sim->pixels->PPM); still builds
  tests/                            blur/unsharp harnesses from the spike
  Makefile                          gcc build (Linux): build/spike,
                                    build/refrender (the parity target)
```

The `.8bf` itself is built from here by `hosts/ps8bf/CMakeLists.txt`
(MSVC) and `hosts/ps8bf/build-mingw.sh`; the architecture doc
(`docs/ARCHITECTURE.md`) shows how the layers connect.

## Provenance

| piece | source | ref |
|---|---|---|
| engine + spatial + grain | `darktable-org/darktable` master | `8c64bf7ca9a8` (2026-10-09) |
| data pack (0.3.3) | `darktable-org/darktable-spektrafilm` | `b15cfe1` (packs/0.3.3) |
| pack content | spektrafilm 0.3.3 runtime pack, `pack_format 2`, 31 profiles (22 film / 9 paper), Hanatos irradiation spectral table | — |

Licences: engine code is GPL-3.0-or-later (darktable's spektra stage; the
upstream spektrafilm film model it implements is GPLv3).
The **pack data is CC BY-SA 4.0** - keep attribution; it is *not* GPL.
The pack this repo embeds is modified (an archived OFX bandpass merged into
20 film profiles); the modification note travels with the attribution - see
`THIRD-PARTY-NOTICES.md` and the About dialog.

## What the port layer adds

- **`sp_chain`** owns the stage order (the colour chain expose -> lograw ->
  develop_corr -> develop -> print_expose -> print_develop -> scan, with the
  spatial look stages - halation, DIR coupler diffusion, print diffusion,
  grain, scanner/glare - placed in the reference's order around it) and
  the chain-side clamps that guard the CPU path (`SP_HALATION_FIRST_SIGMA_UM`
  etc., `docs/PARAMETERS.md` layer 3). Progress and abort are callbacks,
  bridged to the host by the adapter.
- **`sp_params`** is the single JSON key surface shared with the OFX side and
  read by `tools/refrender` - the reason the parity target and the plug-in
  cannot drift apart silently.
- **`shims/portability`** replaced the spike's two runtime deps: `json_mini`
  parses pack and profiles with no glib, and `stdio_asset.c` redirects the
  engine's file reads to the embedded resources, so the shipping DLL loads no
  data files from disk.

## History: the Phase 0 extraction spike (2026-10)

The text below is the spike's own record, kept because its findings still
constrain the code. The spike proved the engine survives extraction: full
spectral colour chain plus one spatial filter, standalone, deterministic
(two runs byte-identical), no GPU, no OpenCL, no darktable host.

`spektra_sim.c` needed only two coupling changes: a `dt_print` logging stub,
and a JSON layer for pack/profile parsing (since replaced by `json_mini`).
`spektra_core.c` needed four dt shims:

- `dt_alloc_align_float` / `dt_free_align` - `posix_memalign` 64 B;
  `_aligned_malloc` on MSVC.
- `dt_iop_image_copy(out, in, n)` - **takes a float count, not bytes**
  (darktable `imagebuf.c` `nfloats`). A naive byte-memcpy copies a quarter of
  the buffer and silently corrupts the spatial path - this exact bug was
  found and fixed during the spike via the unsharp smoke.
- `dt_gaussian_kernel_1d` / `dt_gaussian_yvv_coeffs` - copied **verbatim**
  from darktable `gaussian.c` (both kernels must stay byte-identical for GPU
  parity).
- `DT_OMP_FOR()` etc. - expand to nothing when not built with `-fopenmp`;
  the parity harness runs single-threaded and deterministic.

Spike results (gcc, 2026-10): full chain at exact spectral resolution
(`lut_steps = 0`), filmic behaviour confirmed (1.0 input rolls off to ~0.72,
black lands at D-min ~0.001-0.002, Portra+2383 cross-channel contamination
visible), spatial smoke correct after the copy fix.

Decisions the spike left open, and where they landed: the pack question was
settled in favour of the darktable pack plus the merged OFX bandpass
(`tools/sync_pack.py`); the RGB->raw method variants (Mallett2019,
Hanatos2026) are implemented in `sp_params`/the engine and covered by the
harness parity checks; the grain render loop was ported (see
`docs/GRAIN_SYSTEM_ROADMAP.md`); `development_min` is a key on the shared
`sp_params` surface (reachable from `refrender` JSON), not a silent constant.
The remaining open item - OFX GPU goldens - is recorded in
`tools/parity/README.md`.
