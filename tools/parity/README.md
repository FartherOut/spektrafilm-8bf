# parity harness — core vs OFX goldens (status)

Local (`tools/parity/parity.py`) — **done** for the CPU side:
- Fixed 64×64 tone/colour probe × params × engine selector bodies.
- `harness` selftest + 8-bit e2e vs `refrender` + threaded identity (260×260).
- Determinism cross-run.
- Full validation matrix recorded in `hosts/ps8bf/README.md` (Validation status).

Still not written (Phase 0 items outstanding):
- parameter-matrix coverage of every in-scope feature and Flow/Pro/Dev tier
  beyond the empty-params / method probe today exercised.
- OFX golden capture from `SpektraVulkanCopyHarness` (Windows GPU) stored under
  `tools/parity/goldens/` — needs a Windows session with the v0.3.x builds,
  which are not currently present in the tree.
- `tools/parity/goldens/` and a per-case `max-Δ`/PSNR reporter with CI-friendly
  exit codes.