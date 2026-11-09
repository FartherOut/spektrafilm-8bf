# tools/ — build & data machinery

## The update workflow (ROADMAP §5) — "drop in new source, one command"

1. `git submodule update --remote <path>` moves a pin (they are pinned in git;
   see `SOURCES.md`). Nothing else has to notice: the pack the build embeds is
   re-derived from the submodule on the next build.
2. Refresh the pack and its embeddings — all of these run from the build scripts,
   in this order:
   - `tools/sync_pack.py` derives `spektra-core/data/pack` (git-ignored) from the
     pinned `darktable-spektrafilm` submodule pack and folds in the OFX-only
     per-profile `bandpass_hanatos2025`. It is the *only* writer of that
     directory, and it fails the build rather than falling back to the pristine
     pack: a build that quietly used the fallback would render the 20 film
     profiles that ship a bandpass differently, with nothing to indicate it.
     Stamp-based, so a rebuild that changes nothing copies nothing. `--check`
     verifies without writing; `--allow-missing-archive` is the opt-in to build
     without the bandpass.
   - `tools/merge_bandpass_hanatos2025.py` is that merge on its own (called by
     `sync_pack.py`, and usable directly; `--check` reports without writing).
   - `tools/gen_mallett_basis.py` generates the Mallett 2019 basis table into the
     engine (`--check` verifies it is current).
   - `tools/gen_assets.py --win <pack> <out>` embeds the pack profiles + display
     labels into the `.8bf` RCDATA (and emits `pack_list.h`).
3. Algorithm changes in the OFX shaders are *not* auto-imported: the parity
   harness flags drift and the diff is ported by hand into
   `spektra-core/third_party/darktable/*`. (Same tradeoff as darktable /
   spektrafilm-rs.)

## Layout

- `gen_assets.py`   — embed the pack into `.8bf` resources + emit `pack_list.h`
- `sync_pack.py`    — derive `spektra-core/data/pack` from the pinned submodule
                      pack + the archived bandpass (the only writer; stamp-based,
                      `--check`, `--force`)
- `merge_bandpass_hanatos2025.py` — merge the archived Hanatos-2025 film bandpass
                      into a pack (idempotent; `--check`; called by `sync_pack.py`)
- `gen_mallett_basis.py` — generate `spektra_mallett_basis.h`
                      (`SF_MALLETT2019_BASIS`) from the Mallett 2019 sRGB basis
- `refrender/`      — headless reference renderer
                      (`refrender <params.json> <in.ppm> <out.ppm>`)
- `parity/`         — compare core output vs OFX goldens / spektrafilm-rs oracle
- `package-for-windows.sh` — bundle the tracked tree for a Windows build
                      (`git ls-files` + the two pinned upstream trees the pack
                      derives from, flattened in; verifies the archive it wrote)
