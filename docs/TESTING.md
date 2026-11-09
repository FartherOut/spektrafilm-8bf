# Testing

Status: **current-state, 2026-11-09.** What verifies what, how to run it, and
what only Windows can verify.

## The two Linux gates

Run both before declaring a change done; both are fast and non-interactive.

### `hosts/ps8bf/test/run.sh` - behaviour

Builds the mock-host harness (`test/Makefile`) and the reference renderer
(`make -C spektra-core build/refrender`), then runs, in order:

1. **`selftest`** - property checks inside the harness binary: coupler
   diffusion (A2 channel isolation, A3 tail), the host<->engine adapter
   (params block, sample<->float round trips incl. 16-bit clamping, the
   one-tile loop against a direct chain render), the film-format table and
   micron derivation, the preset round trip (a wide perturbed sample, which
   is what catches a field left out of the preset field table), the Gaussian
   IIR cascade at its pass cap, and an all-stages serial render.
2. **End-to-end 8-bit render vs `tools/refrender`** on a fixed 64x64 probe,
   byte-for-byte. `refrender` is the locked parity target: the headless
   reference renderer driven by a JSON parameter file
   (`refrender <params.json> <in.ppm> <out.ppm>`).
3. **RGB->raw method parity**: Mallett2019 and Hanatos2025 each byte-match
   `refrender` *and* each actually changes the render versus the Hanatos2026
   default - so a method that is not wired, or a bandpass that was not
   merged, fails instead of passing quietly. (The Hanatos2025 leg only bites
   when the pack carries the archived bandpass; without it the script prints
   a note and the engine falls back to 2026.)
4. **Threaded identity**: a 260x260 frame rendered serially, with
   `SPEKTRAFILM_THREADS=4`, and with `SPEKTRAFILM_THREADS=1` - all three
   byte-identical.

The harness and `refrender` resolve the data pack under the relative name
`pack`, so both run from `spektra-core/data`; `run.sh` handles this and runs
`tools/sync_pack.py` first so the pack is current before anything reads it.
Work directory: `${TMPDIR:-/tmp}/opencode/sf-harness`.

Harness subcommands (for manual runs): `selftest`, `render <in> <out>`,
`render-method <in> <out> <n>`, `render-parallel <in> <out>`.

### `hosts/ps8bf/build-mingw.sh` - the Windows build compiles

Cross-compiles the shipping DLL with mingw-w64 from Linux. It exercises the
same sources, the same x64 `FilterRecord` ABI and the same embedded
resources, so anything that breaks the Windows compile (a header, a generated
resource, a new source file not added to the build) shows up here without a
Windows machine. The resulting DLL imports the mingw runtime and is never
the shipping artifact.

## The Windows gate (user-run)

`hosts/ps8bf/build.ps1` on Windows is itself a gate: the warning baseline
(exactly five `C4068`, nothing else - see `docs/BUILDING.md`), the `dumpbin`
import guards, and the pack provenance line in its report. Then the in-host
checklist from `hosts/ps8bf/INSTALL.md`: the plug-in appears **once** in
Affinity's filter menu, the dialog opens with a live preview, a render
matches the preview, settings persist across invocations. Visual and
host-interop behaviour (ImGui/D3D11 presentation, DPI, host tile behaviour,
the About modal) can only be confirmed on Windows 11 + Affinity Photo; the
build report and the log (`%TEMP%\spektrafilm.log`) are what to ask for when
something looks wrong there.

## Optional probes

- **`tools/parity/parity.py`** - machine-readable PASS/FAIL wrapper over the
  same checks as `run.sh` (selftest, 64x64 e2e with sha256, 260x260 thread
  identity), for scripting. The OFX golden capture (GPU goldens under
  `tools/parity/goldens/` from a Windows v0.3.x session) is **not present in
  the tree** - that comparison is a roadmap item, see `tools/parity/README.md`.
- **Pack checks without writing**: `tools/sync_pack.py --check`,
  `tools/merge_bandpass_hanatos2025.py --check`, and
  `tools/gen_mallett_basis.py --check` (compares the committed basis header
  against the pinned `spektrafilm-rs` tree, which is why that clone is pinned
  to a tag).
- **Fresh-clone simulation** (proves a first checkout builds from scratch):
  clone the repo to a temp directory - from a local path this needs
  `git -c protocol.file.allow=always clone ...` because git blocks file://
  submodule clones by default - then `git submodule update --init` and run
  `test/run.sh`. The derived pack is not in git, so this is also the test
  that the derivation route alone is sufficient. Related:
  `tools/package-for-windows.sh` bundles the tracked tree plus the two pinned
  upstream trees flattened in (a tar has no submodules), and verifies the
  archive it wrote.

## What each layer's guarantee rests on

| Guarantee | Enforced by |
|---|---|
| Port renders like the reference engine | e2e + method parity vs `refrender` |
| Threading changes nothing | 260x260 byte-identity in both gates |
| Presets never lose or perturb a field | round trip in `selftest` |
| The `.8bf` struct layout matches the ABI | 121 `SF_OFF` asserts (compile time, MSVC x64) |
| The DLL imports only what it may | `dumpbin` guards in `build.ps1` |
| The embedded pack is the derived pack | `sync_pack.py` stamp + `--check` |
| Preview matches the final render | structural (same `sp_chain`), plus in-host eyeball |
| Host behaviour, dialog look, keyboard and slider gestures, About boxes | user on Windows 11 + Affinity Photo |

The last row is not a formality: the mock-host harness drives the dialog through
a hook and sends no input events, so keyboard, slider and wheel behaviour can
only be checked in the host.
