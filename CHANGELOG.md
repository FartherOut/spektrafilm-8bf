# Changelog

All notable changes to spektrafilm-8bf (the Affinity/Photoshop `.8bf` film-simulation
plug-in) are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 0.1.0 — 2026-11-09

First feature-complete release. A single self-contained 64-bit `.8bf` that runs the
full spektrafilm film-simulation engine on the CPU and presents it through a Dear
ImGui / Direct3D 11 dialog. Signed off in Affinity Photo on Windows 11.
Published 2026-11-09; the post-review hardening round that followed the
sign-off is folded into the sections below.

### Added

- **CPU film-simulation engine** (`spektra-core`, seeded from the darktable
  `spektrafilm` module): spectral colour chain (RGB→raw, film exposure/develop,
  print, scan, gamut compression) plus the spatial stages — halation/scatter, DIR
  couplers, camera & print diffusion, production grain, scanner blur/unsharp/glare.
- **Full film-simulation parameter surface** (ROADMAP §2.1): process modes + scan
  invert, auto input colour space (document ICC) with a 26-space manual override,
  film/paper stocks, push/pull, exposure/gamma, auto exposure + meter, negative &
  print bleach bypass, leuco-cyan coupling, shadow/highlight shape, print
  push/pull, C/M/Y filtration + preflash, DIR per-channel gamma couplings, the full
  grain group (Preview/Production models, sublayers, per-channel/per-layer overrides,
  micro-structure, saturation, Seed), halation per-channel strength, camera/print
  diffusion, scanner controls, and the RGB→raw method selector (Hanatos 2026 default,
  Hanatos 2025, Mallett 2019).
- **Dear ImGui / Direct3D 11 dialog** (`imgui_impl_win32` + `imgui_impl_dx11`, WARP
  fallback): collapsible control groups, tooltips, middle/right-click reset to the
  engine default, dark/light theme, accent OK button, Enter/Esc keyboard navigation,
  per-monitor DPI, and Windows 11 DWM chrome (rounded corners + immersive title bar).
- **Live preview that matches the final render:** a true native-scale 1:1 crop of the
  pane-sized region (pannable) plus a ≤2× supersampled whole-image view, with a
  colour-correct display transform for any document space.
- **Presets** (`%APPDATA%\spektrafilm\presets\*.json`): save/load/delete/copy/paste a
  whole look as flat JSON keyed on pack stems; locale-proof, byte-exact round-trip.
- **Progress/cancel** reporting through the host's abort mechanism.
- **Threaded render** (`CreateThread` pool, no OpenMP): ~4.9× on 12 cores, byte-identical
  to serial; `SPEKTRAFILM_THREADS` overrides the worker count.
- **About box** as a real modal dialog reporting the real surface and the build date as
  EN ISO 8601 (`YYYY-MM-DD`).
- **`INSTALL.md`** — Windows build + install guide, naming the Affinity v3 plug-in folder
  `%USERPROFILE%\.affinity\Affinity\3.0\Plugins`.

- **`THIRD-PARTY-NOTICES.md` at the repository root.** Every licence that touches
  a distribution now says, in one place, what it asks for and where that is
  satisfied: the darktable `spektra` stage the engine is ported from
  (GPL-3.0-or-later, notices kept verbatim, including the two gaussian functions
  that stay © the darktable developers); the film and paper data pack (CC BY-SA
  4.0, with the attribution block in the form its author suggests and the
  modification note the licence requires); spektrafilm itself (GPL-3.0, with the
  acknowledgement its `CITATION.cff` asks for, the KTH affiliation it records but
  does not ask plug-ins to display, and its unusually wide reading of "derivative
  work" answered); the OFX plug-in (GPL-3.0 source — its `Legal/`
  binary-distribution and exported-LUT notices are spelled out as *not* reaching a
  build made from that source); spektrafilm-rs (GPL-3.0, reference-only, and the
  provenance of the Mallett basis table we bake from it); vkdt (BSD-2-Clause,
  reference only); and Dear ImGui 1.93 with its full MIT text reproduced. It also
  records what is *not* used — cimgui, any Adobe SDK, any GPU render path —
  because those are the assumptions the surrounding ecosystem invites. `build.ps1`
  stages it and `LICENSE` into `dist\` beside the `.8bf`, so the notices travel
  with the binary, and the build report gained a `notices` line naming what was
  staged and where.
- **The settings pane warns before a slow preview, not during it.** Halation and
  diffusion now end their sections with a grey line — "Wide halation makes the
  preview slow to update." — drawn only while the current values ask for a blur
  wide enough that the preview stops feeling instant. The preview renders inside
  the dialog's own frame, so while it runs nothing repaints and nothing takes
  input: a caption shown during the wait would be drawn once and then sit under a
  frozen window, and a duration guessed beforehand would be a number with nothing
  behind it. What *is* known before the render is the radius the stage will ask
  for, and the cost follows that as a square — the recursive Gaussian runs one
  pass per `150²` of variance — so the line is drawn from the radius the block
  implies, at the pitch the render will actually use (a chosen film format's own,
  otherwise the manual pixel size). The threshold, 1,200 px, sits above the
  widest radius any stage physically models at a normal pitch — cinebloom's
  4,330 um bloom is 1,083 px at 4 um — so it stays out of the way of real looks
  and appears exactly when the sliders have been pushed somewhere expensive.
  Diffusion's widest Gaussian now comes from the engine
  (`sf_diffusion_widest_sigma_um()`), so a fifth family cannot make the note
  disagree with the bank it builds.

### Changed

- MSVC shipping build defaults to **Visual Studio 2026 Community** (generator
  *Visual Studio 18 2026*, toolset v145, standalone CMake ≥ 4.2); VS 2022 is the fallback.
- Adopted **GPL-3.0-or-later** with the full license text.
- Print **auto-exposure now defaults OFF**.
- Vendoring split by what the build reads: the two trees it compiles are real
  git submodules (`upstream/spektrafilm-ofx` @86476af,
  `spektra-core/third_party/darktable-spektrafilm` @b15cfe1), so a fresh clone
  needs only `git submodule update --init`. The reference-only trees
  (`spektrafilm`, `vkdt`, `spektrafilm-rs`) are git-ignored clones that
  `tools/fetch-sources.sh` fetches on demand, `spektrafilm-rs` at the tag
  `gen_mallett_basis.py --check` compares against.
- Host menu shows spektrafilm **once**: the PiPL `catg` is "Film Simulation" and the item
  `name` is "spektrafilm" (Affinity always nests each `.8bf` under a category submenu).
- Header shows the product name + version only.

- **The factory look is Kodak Ektar 100 on Kodak Ektacolor Edge.** A first open
  of the settings pane, and the pane's **Defaults** button, restored Kodak
  Portra 400 on Kodak Vision 2383: the pairing the engine was developed and
  measured against, rather than a look picked for a first impression. The pair
  is held in two places that have to move together, `sf_params_init()` in the
  `.8bf` host, which is what a first open and **Defaults** read, and
  `sp_params_defaults()` in the shared port layer, which is what
  `tools/refrender` resolves an absent key to. The end-to-end gate renders
  through both from an empty parameter file and compares them byte for byte, so
  it stays green only while both name the same stocks. Presets are unaffected:
  they store the profile stem, so a look saved against Portra still loads
  Portra.
- **Numbers in the GUI are written European-style, with a comma.** Every value
  the settings pane shows now reads `1,00` rather than `1.00`: the number fields,
  the value drawn on each slider, and the ranges and defaults quoted in the
  tooltips (`Default 1,000, range 0,250..4,000`). Typing a comma into a number
  field works, and so does a dot — it is rewritten as you type — so neither
  keyboard layout is a trap. Dear ImGui formats and parses numbers with the C
  locale's dot, so this needed a formatting helper plus a slider and a number
  field of our own; the preset JSON on screen and on disk keeps its dots, since
  that is machine data and shared with other people's files. Identifiers keep
  theirs too: the version stays `0.1.0`, the licences `GPL-3.0-or-later` and
  `CC BY-SA 4.0`, and the build date stays the ISO 8601 form it was asked for.
- **The host's About selector now shows a minimal pointer box.** The native
  MessageBox that answers the host's About selector carried a second, longer
  About text — feature list, film and paper counts, licence lines — that
  drifted from the dialog's About modal and duplicated it badly. It now shows
  the build date, the one-line description, and a pointer to the About button
  in the plug-in window; the full credits and licence stay on the modal and in
  the `THIRD-PARTY-NOTICES.md` that ships beside the binary.
- **The GUI shows the name lowercase, as `spektrafilm`.** The upstream project
  describes its own name that way, and this plug-in continues it, so every
  place the name is shown now matches: the product-name macro behind the
  window title, the About dialog and the MessageBox titles, the PiPL menu
  label Affinity and Photoshop read from the file, the error-message prefixes,
  the presets folder `%APPDATA%\spektrafilm\presets\` (Windows paths are
  case-insensitive, so looks saved under the old spelling are still found),
  and the VERSIONINFO strings a file's Properties sheet shows. Only the
  never-displayed Win32 window-class name keeps the old capitalisation.
- **The project is named `spektrafilm-8bf`; the filter is still `spektrafilm`.**
  The repository, the CMake `project()` and the VERSIONINFO `ProductName` now
  carry the `-8bf` suffix, the way the sibling ports are named after their own
  repositories (`spektrafilm-ofx`, `spektrafilm-rs`), so the three are told
  apart wherever projects are named: a file's Properties sheet reads
  `spektrafilm-8bf` while its description, the host menu and the About dialog
  keep the brand. The artifact stays `spektrafilm.8bf` and the build target
  stays `spektrafilm`, because a host loads a filter rather than a repository.
  Presets stay in the bare `%APPDATA%\spektrafilm\` folder: the OFX plug-in
  already writes `ofx-defaults-v1.spkdefaults` into it and `spektrafilm-rs`
  uses `%LOCALAPPDATA%\spektrafilm`, so the ports share the brand folder and
  separate by subfolder instead of by a suffix. `docs/CONVENTIONS.md` records
  the rule. Two doc errors went with it: the install guide printed
  `%APPDATA%\Spektrafilm\` where the code writes lowercase, and named the menu
  path without the `Film Simulation` submenu Affinity inserts for every `.8bf`.
- **The About dialog's description is now one line, "Spectral film
  simulations of analog photography."** It replaces the longer sentence that
  listed the hosts and the render target. Re-measured with the project's own
  ImGui at 24 px, the fixed dialog is 697x700 px in Liberation Sans and
  697x748 px in the much wider DejaVu Sans against the 792 px default client
  area — neither scrolls any more, not even the wide metric proxy; Segoe UI,
  the font that ships, is a little narrower than Liberation.
- **The About dialog's licence grant stands on its own line.** The first licence
  line is now just `GPL-3.0-or-later` — the trailing "like spektrafilm" went, as
  the derivative credit already sits under Acknowledgements — and the pointer
  sentence, `LICENSE and THIRD-PARTY-NOTICES.md carry the full text and
  notices.`, moved to its own line below it.
- **The About dialog's text was shortened by about a third, so the fixed-size
  dialog fits the plug-in window instead of scrolling.** Fixing the width left
  the text 1028 px tall at the shipped 24 px UI font, against the ~741 px of
  client area the 780 px window of the time had. The description lost its
  feature list (the
  README carries it), the derivative credit one clause, the four credit bullets
  their lead-in and role notes, and the licence lines their subordinate clauses;
  the `(Dear ImGui: MIT)` aside went, as `THIRD-PARTY-NOTICES.md` carries it.
  Everything the licences ask for is still on screen: the credit line
  `CITATION.cff` requests, the statement that this is a derivative of Andrea
  Volpato's spektrafilm, the repository as the data pack's canonical source, the
  CC BY-SA 4.0 attribution with the note that the pack is modified and how, the
  GPL-3.0-or-later grant pointing at `LICENSE` and the notices, and the
  declaration that the `.8bf` interface is our own reading of the documented
  binary layout with no Adobe SDK and no Adobe code. The text column floor went
  from 22 em to 28 em, which gives the remaining paragraphs their comfortable
  measure.
- **The About surfaces point at the notices.** The dialog's licence line now
  names `THIRD-PARTY-NOTICES.md`, and
  `hosts/ps8bf/README.md` gained a *Licence and notices* section.
- **Every file we write now declares its licence.** The 60 C, C++, Python, shell,
  PowerShell, resource and CMake files this project owns gained
  `SPDX-License-Identifier: GPL-3.0-or-later` on their first line. Nothing in the
  tree had one, so the licence was only discoverable from `LICENSE` at the root
  plus prose — which is exactly what a downstream packager greps for. Vendored
  trees were left alone: darktable's files state their own grant, Dear ImGui
  carries its MIT `LICENSE.txt`, and the data pack's `LICENSE` is a text its
  author asks not to be modified. `spektra-core/shims/dt_stubs.c` now says
  explicitly that the two functions copied verbatim from darktable remain © the
  darktable developers, and `tools/gen_mallett_basis.py` stamps the header it
  generates with the same identifier plus where the numbers come from (its
  `--check` still passes, and all 81 rows are byte-identical to before).
- **About box credits.** The original spektrafilm project is now acknowledged by
  name and author at the head of the section, in the wording its `CITATION.cff`
  asks for, instead of appearing as one bullet in a source list under a sentence
  that read as though the licence were the thing being derived. Two of the
  remaining credits were also corrected to what those repositories actually
  contribute here.
- **Both About surfaces show the build date the same way.** The dialog shows the
  build date alone as EN ISO 8601 (`YYYY-MM-DD`); the host's plug-in About
  selector still printed the raw `__DATE__ __TIME__` stamp ("Nov  5 2026
  14:03:11"), which reads oddly in a dialog and is awkward to compare against a
  report. The conversion moved out of the dialog's translation unit into
  `src/build_date.h`, shared with the C About selector. The diagnostic log keeps
  the full stamp: there the point is telling a loaded DLL from a stale copy on
  disk.
- **The `.8bf` ABI claim says what was actually done.** The About dialog, the
  headers and the docs called `pi/` a "clean-room re-derivation" of Adobe's
  headers. *Clean-room* is the term of art for an implementation written by
  someone kept away from the original, which overstates it — the accurate
  description is that the documented binary layout was re-declared from its
  published description, only the parts a stills filter needs written down, with
  121 compile-time asserts pinning the `FilterRecord` offsets. The claim that
  matters is unchanged and is now stated plainly instead of behind the jargon: no
  Adobe SDK file is in the tree, nothing Adobe-licensed is redistributed, and no
  Adobe download is needed to build. `pi/pifilter.h` also said "the ~150 offset
  asserts below"; there are 121.
- **Upstream checkouts now live in `upstream/`, named after what they are.** The
  vendored trees sat in `spektrafilm_source/` under names that did not match
  their repositories (`spektrafilm_OFX` for `chaert-s/spektrafilm-ofx`), and one
  of them was a submodule without needing to be: `spektrafilm-rs` is read only by
  `tools/gen_mallett_basis.py`, a maintenance-time generator that no build runs.
  The OFX plug-in is `upstream/spektrafilm-ofx`, still a pinned submodule;
  `spektrafilm-rs` is now a reference-only tree that `tools/fetch-sources.sh`
  clones at `v0.1.2` — the tag the committed Mallett basis header was generated
  from, so `gen_mallett_basis.py --check` still compares it against its actual
  source instead of a moving default branch. Building from a fresh clone is
  unchanged: `git submodule update --init`.

- **The build reports everything and suppresses nothing.** Every route now shows
  the standard output of every tool it runs: the pack derivation no longer runs
  with `--quiet`, the CMake configure step no longer captures its stderr for the
  failure path only, and `build-mingw.sh` prints each compile's diagnostics whether
  or not that compile succeeded. Nothing is silenced either — `-Wno-unknown-pragmas`
  and MSVC's `/wd4068` and `/wd4121` are gone, so the four `-Wunknown-pragmas` /
  `C4068` notices for `#pragma STDC FP_CONTRACT` in the vendored `grain.h` appear on
  every build. They come from code we keep unmodified, so the honest report is four
  notices rather than a filtered zero; `/wd4121` was speculative anyway, as nothing
  in the compiled set uses bit-fields. Each route ends with a `== build report ==`
  block: toolchain, source counts, the warning count broken out per diagnostic code,
  the embedded pack's provenance, the artifact's size and hash, and every DLL it
  imports. `build.ps1` gets its counts from a complete MSBuild log
  (`build\msbuild.log`, written by MSBuild's own file logger) rather than piping the
  build through PowerShell, which would strip MSBuild's console output.

### Fixed

- 1:1 crop no longer renders almost entirely black (a blit destination pointer advanced
  once per row instead of once per pixel).
- 1:1 pan drag now tracks the cursor exactly 1:1 (the grabbed pixel stays under the
  cursor; previously ~2×).
- Checkbox selected state keeps its blue tick at all times (accent at low alpha, not a
  solid fill); hovering no longer changes the indicator.
- About modal opens reliably; Esc over it closes only the modal instead of cancelling the
  plug-in window.

- **A slider no longer renders the preview every time the pointer pauses, and a
  rolling mouse wheel renders once the rolling stops.** Both were one debounce
  asked to cover two different shapes of gesture. A drag is a single edit with
  stops in it, made while the button is held, and the 120 ms timer took every
  one of those stops for the end of the edit: hold a slider still and a render
  started with the button still down. The settings pane now asks Dear ImGui
  whether a slider holds the active id, which it keeps for as long as the
  button is down no matter where the pointer wanders, and while it does the
  deadline is pushed out again each frame instead of being allowed to expire,
  with the preview pane told to keep waiting. The frame after the release stops
  pushing, the deadline goes off, and what appears is the value the drag ended
  on. A wheel notch is the other shape: the user rolls, stops to look, rolls
  on, and no single notch is a decision either, so a notch now arms a 700 ms
  settle window and starting again inside it is still the same edit. Everything
  else - a committed number field, a dropdown, a right-click reset - keeps the
  120 ms debounce, and the 1:1 preview drag is untouched: its own debounce,
  re-armed by every frame of the drag, already renders when the drag stops
  rather than under the cursor.
- **A number field no longer re-renders the preview on every keystroke, and no
  key presses OK.** Two halves of one complaint. Dear ImGui applies an
  `InputText` to its backing value as you type, because
  `ImGuiItemFlags_LiveEditOnInputText` is on by default, so every digit typed
  into a number box moved the parameter and re-rendered the preview; the flag
  is now pushed off on both kinds of number box, so the value lands when the
  field is finished with instead - Enter, Tab, or a click elsewhere - and the
  preview updates once. `InputInt` already held the *value* back until the
  field was deactivated, so the integer rows only needed the flag for
  consistency, but they now defer by the same rule rather than by which widget
  happens to be underneath. Second half: Enter activated OK and started the
  render. The dialog's global key handler did that itself, and it could not
  tell a free Enter from one typed into a field, because a text field releases
  its focus inside the same frame in which Enter finishes the edit - the
  "is anything active" test at the bottom of the frame was already false. The
  handler now reads that state at the top of the frame, and it only cancels on
  Esc. OK and Cancel are additionally given `ImGuiItemFlags_NoNav`, so they
  cannot become the keyboard-navigation focus and neither Enter nor Space can
  press either: applying the dialog takes a click on OK, on Cancel or on the
  title-bar X. The accepted cost is that a keyboard-only user cannot press OK
  either; Tab and the arrows still walk every setting, and Esc still cancels.
- **The host decoded 8-bit samples with a rounded reciprocal, and the parity
  gate could not see it.** `sf_pixels_to_float()` scaled each byte by
  `* (1.0f / 255.0f)` where `tools/refrender` divides by `255.0f`. That
  reciprocal is itself a rounded float, so the two disagree by 1 ulp for 126 of
  the 256 codes, and the engine amplifies it: on a 64x64 ramp through the sRGB
  cube at the shipped defaults, 10194 of 12288 rendered floats differ, spread
  over the whole tonal range rather than clustered in the toe, by up to
  5.7e-05 relative, about a thousand times the input error. That is only
  ~0.014 of an 8-bit level, so a pixel changes only when it sits within that of
  a rounding boundary, which is rare but not rare enough: probed over the pack,
  each of the 22 films against Kodak Vision 2383 and each of the 9 papers
  against Kodak Portra 400, all 31 pairings differ in the float output (3282 to
  10664 of 12288 samples, up to 4313 ulp) and 13 of them differ once quantised,
  19 samples in all, every one by exactly one level. The end-to-end gate
  compares the two paths byte for byte on one pairing, and Portra 400 on Vision
  2383 is one of the 18 that happen to quantise equal, which is why the gate has
  been green. `pix_convert.h` already documented the scale as `v / 255` and said
  that sharing it is what makes the parity oracle hold; the code now does what
  the header says, and the same probe afterwards reports all 31 pairings
  bit-identical in float and byte-identical at 8-bit, so parity is a property of
  the code rather than of the pairing chosen. The division costs 15 ms on a
  full-image decode, best of five on a 12 Mpx image: 18.0 ms with the
  reciprocal, 33.3 ms with the division, against a render of that size measured
  in tens of seconds. 16-bit input is numerically unaffected either way, because
  `SF_SAMPLE_MAX_16` is a power of two, and is written as a division only so
  both branches read like the header.
- **The cross-toolchain hash claim in the Windows validation record was
  ambiguous.** `hosts/ps8bf/README.md` said a Windows configure "derived the pack
  with the same content hash as the Linux machine", which reads as the build
  output; the `.8bf` cannot hash equal across two compilers, because it embeds
  its own build date and time — the date the About popup shows — so its `sha256`
  changes on every build and is not a parity signal. What is equal is the derived
  data pack, and the record now names both of its hashes
  (`e6ba3f78c114` / `f812b3f4e225`) and says so. The same section is refreshed
  with the 2026-11-07 run, which is the one that proves the release path: the
  `tools/package-for-windows.sh` bundle, checked to be exactly the tracked file
  list, extracted into an empty directory and built with no arguments on
  VS 18.11 / MSVC 19.51 / CMake 4.3.1 / SDK 10.0.26100 — pack derived from
  scratch, five `C4068` warnings, errors 0, imports `USER32 GDI32 KERNEL32` only.
  It also records the version resource read back from that artifact and the
  in-host About-popup and Esc behaviour, replacing prose that had accumulated one
  bullet per round.
- **The documented menu path was missing its top level.** `hosts/ps8bf/INSTALL.md`
  and `hosts/ps8bf/README.md` told users to look under **Filters → Plugins →
  Film Simulation → spektrafilm**. Affinity Photo has no top-level `Filters`
  menu: the chain starts at **Pixel**, so the printed path could not be followed
  as written. Confirmed in-host on Windows 11 against the `dist` build:
  **Pixel → Filters → Plugins → Film Simulation → spektrafilm**, shown once. The
  install step now also says which levels are whose, because the text had
  explained the PiPL category while implying the levels above it were all part of
  the same mechanism: the three leading levels are Affinity's own menus, and only
  `Film Simulation` (PiPL `catg`) and the item name come from this plug-in.
- **The docs understated what the Dear ImGui vendor holds.** `SOURCES.md` said
  "Dear ImGui (MIT)" and `THIRD-PARTY-NOTICES.md` §6 named the six compiled
  translation units; both read as "the folder contains what is compiled", and it
  does not. The vendor is the complete v1.93.0 release tree, 304 files, of which
  the preprocessor opens fourteen and the other 289 (51 more files under
  `backends/`, `examples/`, `docs/`, `misc/`, `imgui_demo.cpp`) are read by
  nothing in the build. The tree stays whole on purpose — upgrading is then
  copying a release over the folder and reading the `git diff` — but both files
  now separate what is vendored from what is built. `docs/GUI_ROADMAP.md` §B.1
  and §11 claim the vendor is "core + `imgui_impl_win32` + `imgui_impl_dx11`",
  which is what was planned and not what was copied in; the historical banner
  names that as wrong, since a historical body is not rewritten.
- **The Dear ImGui vendor had no recorded upstream commit, and two of its files
  were modified without any document saying so.** The copy arrived with its
  history stripped, so the only version in the tree was the `IMGUI_VERSION`
  string in `imgui.h` and nothing named a commit to diff the next upgrade
  against. It now does: `v1.93.0`, commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859`
  (2026-10-14, `master`). Checking that claim properly surfaced the second half:
  `diff -r` against a checkout of that commit is not empty. Two backend files are
  patched here — `imgui_impl_win32.cpp` (drops the `dwmapi` link directive, stubs
  `ImGui_ImplWin32_EnableAlphaCompositing()`) and `imgui_impl_dx11.cpp` (drops the
  `d3dcompiler` link directive) — both to keep `dwmapi.dll` and
  `d3dcompiler_47.dll` out of the static import table under decision D8. Each
  change carries a `[SPEKTRAFILM PATCH]` comment in the source and the design note
  behind it existed, but no provenance document mentioned that the vendor was
  modified at all, which matters for a tree described as an upstream release: a
  reader diffing it against upstream would find unexplained edits. `SOURCES.md`
  now lists both patches with their reasons and `THIRD-PARTY-NOTICES.md` §6 says
  the tree is not pristine, so the MIT notice covers modified code knowingly.
  Every other file is byte-identical to `c1aaac65`, and one file that had gone
  missing is restored: upstream tracks
  `examples/example_android_opengl3/android/gradle/libs.versions.toml`, but
  ImGui's own `.gitignore` arrived with the tree and lists `gradle/`, so the
  original vendoring pass could not see it. That is why the count is 304, not 303.
- **The build log mangled sync_pack's message on Windows.** The script's "pack
  is current — nothing to do" line reached the console as "…ù nothing to do"
  whenever CMake or MSBuild captured its output: with stdout on a pipe Python
  writes UTF-8, and the console then renders those bytes through the OEM
  codepage. The three tools the build runs (`sync_pack.py`,
  `merge_bandpass_hanatos2025.py`, `gen_assets.py`) now print plain ASCII `--`
  wherever they speak, and the C comments `gen_assets.py` generates are ASCII
  too, so the generated sources are safe for a compiler reading them under any
  codepage. (The stamp records the derivation scripts' own hashes, so this
  edit re-derived the pack once — byte-for-byte identical, verified by content
  hash.)
- **The About popup's data-pack attribution broke its line at an em dash.**
  The sentence naming the pack's author, licence and modification note wrapped
  exactly at its em dash, leaving the dash dangling at the end of a line in a
  popup barely wider than the text. It is now two plain clauses: "Data pack:
  spektrafilm by Andrea Volpato, CC BY-SA 4.0. Modified by this project: the
  archived OFX bandpass merged into 20 film profiles."
- **Lowering the halation ceiling overwrote the per-channel radii.** The ceiling
  ("Sigma max um") and the three first-bounce radii are separate fields, and the
  radii must stay under the ceiling for `OK` to accept the block — so the
  settings pane folded them down to it *every frame* the ceiling was lower.
  Dragging the ceiling slider down to see what it did therefore destroyed
  radii the user had set channel by channel, and dragging it back did not bring
  them back; a halation profile applied under a low ceiling was rewritten the
  same instant. The fields now keep what they hold and say what is happening
  instead — a note under the three rows gives the radius the render will use —
  while the preview and `OK` fold the block the way the chain already does, so
  what is previewed, what is rendered and what is stored in the document stay
  the same image. The ceiling, the validator's rejection and the chain's clamp
  all read the one expression that produces it.
- **A wide-enough blur silently did nothing.** Any stage wider than 150 px runs
  as a cascade of recursive Gaussian passes, one per `150²` of variance, and the
  pass count was derived straight into an `int`: past a sigma of about 6.95e6 px
  the square overflows, the cast is undefined, and in practice comes out
  negative — so the pass loop ran zero times and the stage returned the
  unblurred plane without an error or a word. The count is now clamped in float
  before the cast, at `SF_GAUSS_MAX_IIR_PASSES` (65 536, so its square root is
  exact in float), and the per-pass sigma is clamped with it, which makes the
  saturation explicit instead of something the coefficient builder applies
  quietly. The cap leaves every radius the sliders can reach exact — halation at
  its 1000 um ceiling with the scale multiplier at maximum is 13 856 px and
  still composes to exactly that — and only bites a request past 38 400 px, a
  blur six times wider than a 6000 px frame, where the edge-replicating
  recursion is a flat field either way. The same helper is what the OpenCL
  host's cascade calls, so the two stay identical.
- **A pasted preset could ask for a blur the render cannot finish.** The four
  spatial radius multipliers (scatter, halation, camera and print diffusion)
  stopped at 4,000 on screen but were only checked for being *finite*, and a
  preset read from a file or the clipboard never passes a slider: its numbers go
  straight into the block. Cost follows the multiplier — halation runs
  `(sigma_px / 150)²` recursive passes of the recursive Gaussian — so a
  hand-edited `halation_scale` of `1e12` asked for a blur that would not finish,
  with no way out: the host's cancel is only polled between pipeline stages, and
  the preview renders at reduced resolution with the pixel size scaled to match,
  which makes the same setting cheap there. The validator now requires the
  sliders' own range for all four, through `SF_BLUR_SCALE_MIN` /
  `SF_BLUR_SCALE_MAX` that the sliders use as well so the two cannot drift, and
  the GUI refuses a preset whose values do not validate instead of adopting it
  and then quietly refusing to render it.
- **The About dialog resized and reflowed while it was dragged.** It asked ImGui
  for `ImGuiWindowFlags_AlwaysAutoResize` and then wrapped its text at
  `GetCursorScreenPos().x + 480` — but `PushTextWrapPos()` takes a *window-local*
  x (`CalcWrapWidthForPos()` adds `window->Pos.x` back to it), so the wrap width
  was really `480 + window->Pos.x`: dragging the dialog right lengthened every
  line, auto-fit grew the window to hold them, and the new position changed the
  wrap width again. Measured headlessly against the project's own ImGui copy:
  780 px wide at x=302, 983 px at x=502 — "moving it right squeezes it flat and
  wide". The height drifted the other way because `SeparatorText()` extends the
  content to the inner right edge, which for an auto-fit width is a moving
  target. The wrap position is now window-local and the width is pinned from the
  text: the widest line that must not break (title, build date, presets path,
  repository URL), floored at 28 em and capped to the viewport, applied with
  `SetNextWindowSize(ImVec2(width, 0))` — which pins only that axis, leaving the
  height auto-fitted — plus `ImGuiWindowFlags_NoResize`. The four credit bullets
  became `Bullet()` + `TextWrapped()`, since `BulletText()` never wraps and would
  clip at a fixed width. The width now measures identical across 1000 px of
  horizontal drag in both directions, at 96-dpi geometry, and identical in both
  fonts tried (696 px, which is the floor) — DejaVu Sans and Liberation Sans;
  the height is whatever the text needs at that width.
- **Settings rows drew their label over the slider and clipped their value.**
  The row columns were fixed pixels — label 112–196 px, value 62 px — sized for
  the 12 px font of the first ImGui dialog, but the Phase 4 font is 24 px at
  96 dpi and scales with DPI. Measured through the real layout chain at the
  default window size: the label column came out 183 px while the longest of
  the 140 labels needs 231 px (Liberation Sans; 253 px in DejaVu), and the value
  field 62 px against the 91 px `-1.0000` needs. The columns are now em-based:
  label 11–12 em, value measured from `-1.0000` itself in the current font plus
  the frame's padding and border, slider floor 4.5 em. A row wants 21.4 em, so
  `sf_imgui_build()` now guarantees the settings pane that much — the preview
  pane gives up width first, since a narrower preview is still a preview while
  a row without room draws its label over its slider. Verified with the same
  harness against the shipping `sf_grid_measure()`: label, value and row all fit
  at 12, 24 and 36 px in both font proxies, at the default and the minimum
  window size.
- **The window size did not follow DPI.** Phase 4 doubled the UI font (24 px at
  96 dpi, scaling with DPI) but left the window at a fixed 1100x780 px creation
  size and 800x560 px minimum, so on a 150% display the text was half again as
  wide while the window was the same size — a third fewer characters per line,
  and the settings grid squeezed worse than ever. The default and minimum
  client sizes are now in em (50x33 and 33x23): the creation size is computed
  from the owner window's monitor DPI, turned into an outer size with
  `AdjustWindowRectEx` and clamped to the monitor work area; after creation the
  window's own DPI is re-queried and, if it differs, the window is resized
  before the swap chain takes its initial size; `WM_GETMINMAXINFO` scales the
  minimum with the live DPI and adds the frame thickness measured from the
  window's own rects. The minimum is what a settings row needs
  (`SF_ROW_MIN_EM`) plus the pane chrome, and the default is what the About
  dialog needs (718 px against a 792 px client at 96 dpi) — both in em, so the
  proportions hold at any DPI. The stale "9 pt" note on `SF_UI_FONT_BASE_PX`
  and in `docs/GUI_ROADMAP.md` now says what it has been since Phase 4: 24 px,
  18 pt.
- **The build report undercounted repeated warnings.** The MSBuild-log scraper
  deduplicated diagnostics by message text — that was there to drop the copies
  the end-of-build summary repeats — but it also collapsed genuine repeats:
  the vendored `grain.h(97,9)` earns the same C4068 in four translation units,
  which read alike once the `1>` event prefix is stripped, so the first Windows
  run reported `warning C4068 x1` where its own log had four. The counter now
  counts the live event stream and stops at the `Build succeeded.` /
  `Build FAILED.` banner, so every diagnostic counts exactly once — and the
  report matches the mingw side's four `-Wunknown-pragmas`. Verified with
  PowerShell against synthetic logs: four repeats, a failed build (error plus
  the repeats), and a banner on the first line.
- **`tools/package-for-windows.sh` produced a bundle that could not build.** It
  bundled `hosts/ps8bf spektra-core tools/gen_assets.py ROADMAP.md` and excluded
  `upstream/`, so the tree it made had no `tools/sync_pack.py` or bandpass merge
  for `build.ps1` to run, no OFX profile archive for the pack to derive from, and
  none of the notice files `build.ps1` now refuses to stage without. It bundles
  the tracked tree instead (`git ls-files`, which keeps build output, the derived
  pack and stray renders out without an exclude list to maintain), plus the two
  pinned upstream trees the derivation reads, flattened to plain files at the
  paths its defaults expect — so no `git submodule update` is needed on Windows.
  The archive is then checked against its own listing: required files present,
  both pack sources non-empty, no path listed twice, nothing generated. That last
  check earned its place on the first run, which duplicated every submodule file
  because `git ls-files` reports a gitlink as a plain path and tar archived that
  directory recursively. Verified end to end: extracted to a tree with no `.git`,
  the bundle derives a byte-identical pack and links the same 17,430,933-byte
  artifact with `build-mingw.sh`.
- **Two stale claims in the docs.** `hosts/ps8bf/INSTALL.md` credited "cimgui
  (MIT)" as a GPL-compatible dependency. cimgui is not in the tree: the dialog
  calls Dear ImGui's C++ API directly, so the credit named a library whose notice
  nobody had to satisfy. (Grepping still finds the word, in Dear ImGui's own
  `docs/`, where it is offered as a route to language bindings we do not take.)
  And `hosts/ps8bf/README.md` twice said `pi/pifilter.h` pins "all ~150 offsets";
  the file contains 121 `SF_OFF` asserts plus the `sizeof == 608` check, so 121 is
  now the counted number in both places.
- **The licence we state was the ambiguous one.** Sources said plain `GPL-3.0`
  (`SF_COPYRIGHT`, which lands in the binary's `LegalCopyright`, and several doc
  lines) while `INSTALL.md` and the About box said `GPL-3.0-or-later`. Plain
  `GPL-3.0` does not say which grant is meant — SPDX deprecated that identifier
  for exactly this reason — and the two possibilities differ: `GPL-3.0-only` locks
  recipients to version 3, `GPL-3.0-or-later` lets each of them choose any version
  the FSF publishes afterwards. Every place that means our own licence now reads
  `GPL-3.0-or-later`, which is ours to grant: the code we derive from is itself "or
  later" (darktable's notices read "either version 3 of the License, or (at your
  option) any later version"). Plain `GPL-3.0` is kept only where a line quotes how
  an upstream labels its own repository.
- **The data pack's attribution now says what its licence requires.** The pack's
  CC BY-SA 4.0 licence names what must travel with every copy and derivative —
  "an app's About screen" is one of the places it lists: the author, the canonical
  URL, the licence notice, and for modified files a note that they were modified
  and by whom. The pack we embed is modified (20 film profiles carry the
  `bandpass_hanatos2025` folded in from the OFX archive) and nothing said so: the
  About dialog listed "Film and paper data pack: CC BY-SA 4.0" and the host's
  plug-in About selector said as much without even the author or the URL. Both now
  give the attribution block upstream suggests plus the modification note. Andrea
  Volpato's KTH affiliation, which `CITATION.cff` carries as bibliographic metadata
  rather than as part of the credit line it asks for, moved to `SOURCES.md`.
- **`LNK4070` at the end of every MSVC link.** `res/plugin.def` carried a
  `LIBRARY spektrafilm` statement, which the linker takes as the image's internal
  name and writes into the `.exp` as `/OUT:spektrafilm.dll`. Because the target's
  suffix is `.8bf`, every build finished with "`/OUT:spektrafilm.dll` directive in
  `.EXP` differs from output filename …`spektrafilm.8bf`; ignoring directive". The
  statement is removed instead of the warning silenced, so the linker derives the
  name from the output file and the two cannot disagree. (`LIBRARY spektrafilm.8bf`
  would also please `link.exe`, but GNU ld rejects a dot in a `LIBRARY` name and the
  `.def` is shared with the mingw route; the exported `PluginMain` is the same
  either way.)
- **Compiler warnings in the mingw build.** `build-mingw.sh` sent each compile's
  diagnostics to a log it only printed when the compile failed, so a build could
  accumulate warnings unnoticed; it now prints them. With them visible: the eight
  `-Wmisleading-indentation` hits were the `if(a<0)a=0; if(a>1)a=1;` clamp idiom
  written on one line — split onto two lines, with the emitted code verified
  byte-identical before and after — and the log path in `plugin_main.c` now declines
  to write a truncated file name instead of relying on the receiver to truncate.
  What remains is the four unknown-pragma notices from the vendored darktable
  headers, which are reported rather than silenced: that code is kept unmodified,
  so a clean build reports four notices and nothing else.
- **The embedded film pack is derived on every build, so a fresh clone reproduces
  the released render.** The build read its film/paper pack from a git-ignored
  working copy, `spektra-core/data/pack`, and fell back to the pristine submodule
  pack whenever that copy was absent — which is what every fresh checkout sees. The
  fallback also skipped the archived `bandpass_hanatos2025` fold, so a fresh clone
  configured, compiled and passed the harness while the 20 film profiles that ship
  a bandpass rendered differently from the released plug-in, with nothing to
  indicate it. `tools/sync_pack.py` is now the only writer of that directory: it
  copies the pinned submodule pack, folds in the bandpass, verifies the result and
  records a stamp, so an unchanged rebuild copies nothing. All three build routes
  (`build.ps1`, `build-mingw.sh`, CMake) run it and stop on failure rather than
  picking a different pack; `--allow-missing-archive` is the explicit opt-out.

### Security / hygiene

- Dropped every claim that the Adobe Photoshop SDK is required — the `.8bf` ABI is a
  clean-room re-derivation in `hosts/ps8bf/pi/`; no Adobe-licensed source is in the tree.
- Shipping import table limited to KERNEL32/USER32/GDI32; static CRT; no external DLLs.
- Stopped tracking build junk.

### Known limitations (by design / optional)

- OFX-GPU golden-image parity (Mallett 2019 / Hanatos 2025) is optional and needs a
  Windows GPU capture; the CPU engine is byte-identical to the `tools/refrender` Python
  reference on the Linux harness.
- Printer-timing APD spectral calibration is blocked on the licensed ST 2065-2 table
  (the non-spectral trim works).
- GPU-only features are intentionally not ported: grain **Synthesis** family, the OFX
  **Effects** family (lens distortion, vignette, bokeh, …), HDR/delivery output, film
  plane, and LUT export.

## 0.0.x — pre-release

Iterative development: extraction spike and `spektra-core` scaffolding, the colour
chain, spatial stages, advanced parameters, the ImGui migration (native Win32 dialog
retired in B6), and the pre-ship bug-hunt / hardening pass. Tracked in `ROADMAP.md`
and `docs/`.
