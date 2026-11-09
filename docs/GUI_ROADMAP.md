# Spektrafilm `.8bf` — GUI roadmap: scroll-flicker fix + Dear ImGui dialog

> **Historical roadmap.** Everything here shipped; the current-state dialog
> architecture is in `docs/ARCHITECTURE.md` and the number-formatting and
> popup rules in `docs/PARAMETERS.md` / `docs/CONVENTIONS.md`. The body is
> kept unmodified as the design record. Two details in it are wrong. §B.1 and
> §11 describe the ImGui vendor as "core + `imgui_impl_win32` +
> `imgui_impl_dx11`", but what was copied in is the complete v1.93.0 release
> tree (304 files at upstream commit `c1aaac65`, six of them compiled, two of
> those patched). `SOURCES.md` is authoritative on what the vendor holds.
> Second: "Enter accepts" / "Enter/Esc keyboard nav" (§B.1, §B.2, §B.4, §13)
> was superseded on 2026-11-08 — no key applies the dialog any more, and OK
> and Cancel are outside keyboard focus. See `docs/CONVENTIONS.md` §*GUI*.

**Status:** Workstream A implemented 2026-10-16; Workstream B phases B0–B6 implemented (B6 — native dialog retired — 2026-10-19). **Bug hunt / hardening pass complete and Windows-verified 2026-10-18.** **Post-B6 GUI/preview polish pass complete and Windows-verified 2026-11-04 — released as v0.1.0 (§13).**
**Date:** 2026-10-16 (renderer revision 2026-10-17)
**Scope:** The Windows `.8bf` host adapter only (`hosts/ps8bf`). No engine or `.8bf` ABI change.
**Related:** `ROADMAP.md` §6/§8/§11; `hosts/ps8bf/README.md` §*Parameter dialog*.

> This roadmap covers two workstreams:
> **A — the interim Win32 scroll-flicker fix** (small, shippable immediately, independent of B);
> **B — replacing the Win32 parameter dialog with a Dear ImGui "GUI engine"**, which also makes the
> flicker structurally impossible.
>
> **Renderer revision (2026-10-16):** the renderer is **Direct3D 11 — ImGui's own default/official
> Windows backend** (`imgui_impl_win32` + `imgui_impl_dx11`), not a hand-written Direct2D backend.
> A GPU is now a requirement in practice (with a WARP software fallback). The GPU DLLs are reached via
> `LoadLibrary` so the plugin's static import table is unchanged (decision **D8**). This supersedes the
> earlier Direct2D+DirectWrite choice (see decision **D2**).

---

## 1. Objectives

1. **Remove the "flickering black" during scrolling** in the current `IDD_PARAMS` dialog — now, with a
   small, low-risk change that ships on its own.
2. **Adopt Dear ImGui** as the dialog's rendering/UI engine for a modern, bespoke look on Windows 11,
   owning the **whole** dialog (live preview + all settings).
3. Keep the engine and the render path **byte-identical**: the harness (`test/run.sh`) stays green and
   the shipped `.8bf` keeps its static CRT (and, if we so choose, its tight import list — see §3).
4. Retain the existing Win32 dialog as a **selectable fallback** until ImGui is proven in Affinity.

## 2. Locked decisions

| # | Decision | Value |
|---|---|---|
| D1 | ImGui scope | **Everything** — ImGui draws the preview image *and* all settings; no native child controls |
| D2 | Renderer backend | **Direct3D 11** — ImGui's **official/default Windows backend**: `imgui_impl_win32` (platform) + `imgui_impl_dx11` (renderer). GPU-backed; requires `d3d11.dll` + `dxgi.dll` (+ `d3dcompiler_47.dll` unless we embed precompiled shaders). *Supersedes the earlier Direct2D+DirectWrite choice.* |
| D3 | C++ integration | **Add a C++ translation unit**; `extern "C"` seam to the existing C; `-fno-exceptions -fno-rtti` (mingw) / `/MT` (MSVC) |
| D4 | Vendoring | **In-tree pinned copy** under `hosts/ps8bf/third_party/imgui` (MIT) — no submodule, no build-time fetch |
| D5 | Flicker fix | **Do it first**, ship it, then migrate (independent of the ImGui work) |
| D6 | UI information architecture | **1:1 port** of the current ~130-control set, tidied into **collapsible groups** (no redesign, no params-block change) |
| D7 | Native fallback | Retired in B6 (2026-10-19): `dialog.c` + the A-fixes + the backend switch are deleted; ImGui is the sole backend |
| D8 | GPU-stack loading | **Dynamically load** `d3d11.dll` / `dxgi.dll` / `d3dcompiler_47.dll` via `LoadLibrary`+`GetProcAddress` — the static import table stays at the original four DLLs and the guard stays green (see §3) |

## 3. Invariants (must not break)

- **Engine byte-identity.** `hosts/ps8bf/test/run.sh` must stay green. The harness builds **portable host
  code only** and deliberately excludes `dialog.c` (`test/Makefile`) — the GUI work must stay on the
  Windows-only side of that line and must never touch `sp_chain`, `sp_params`, `params.c` semantics, or
  the params-block layout.
- **Static CRT.** `build.ps1` fails on CRT/runtime imports (`ucrtbase`, `vcruntime`, `msvcp`, `api-ms-win-crt-*`).
  Keep MSVC `/MT` for C **and** C++ so no `msvcp140.dll`/`vcruntime140.dll` is required.
- **Import list — dynamically loaded GPU stack (decided, D8).** The shipping `.8bf` keeps importing
  only `KERNEL32/USER32/GDI32` (COMDLG32 was dropped in B6, when the native dialog went). The D3D11
  renderer reaches the GPU stack through
  `LoadLibrary`/`GetProcAddress` for `d3d11.dll`, `dxgi.dll`, and (if shaders are not embedded)
  `d3dcompiler_47.dll` — **no `target_link_libraries` entries** for these. This keeps the existing guard
  green and keeps plugin load light; on a machine without the GPU DLLs (and no WARP) `sf_gui.c`
  reports an error and cancels instead of falling back to a removed dialog. If a symbol is missing at
  runtime, treat it exactly like a D3D11
  init failure (§B.2) — report + cancel (no native fallback since B6).
  **ImGui/D3D is only touched while the dialog is open**; the render path never does.
- **One modal invocation at a time**, driven from `filterSelectorContinue` (the `gmic-8bf` pattern),
  owner = the host window. The dialog must not assume an event loop from the host.
- **The ports/host contract is unchanged**: PiPL, `PluginMain`, `FilterRecord`. The GUI is internal.
- **Licensing.** Project is GPL-3.0-or-later; Dear ImGui is MIT — compatible. Keep ImGui's `LICENSE.txt`
  unmodified and reproduce its notice in `THIRD-PARTY-NOTICES.md` (done; see §10).

## 4. Workstream A — interim Win32 scroll-flicker fix

> **Status: implemented 2026-10-16, verified in Affinity on Windows 11 (2026-10-18).** Tasks 1–4 plus
> the no-op-move guard below. Builds clean with `build-mingw.sh`; the portable harness is untouched
> and still green. Scroll, resize, light/dark and 100–200% DPI confirmed in-host — task 5 was not
> needed for the first cut.

### A.1 Root cause (already diagnosed)
`hosts/ps8bf/res/resource.rc` (`IDD_PARAMS`) has `WS_VSCROLL` but **no `WS_CLIPCHILDREN`**; there is no
double-buffering. The scroll path (`WM_VSCROLL`/`WM_MOUSEWHEEL` → `sf_lay_scroll` `dialog.c:1520` →
`sf_lay_relayout` `dialog.c:1436`) calls
`RedrawWindow(dlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN)` (`dialog.c:1517`) on every
step, and `WM_ERASEBKGND` (`dialog.c:1691`) fills the **whole** client with the dark brush
(`RGB(32,32,32)`, `dialog.c:135/192`). Result: full-frame dark wipe per scroll tick.

### A.2 Tasks
1. **Clip children:** add `WS_CLIPCHILDREN | WS_CLIPSIBLINGS` to the `IDD_PARAMS` style
   (`res/resource.rc`). Single highest-value change; also fixes resize flicker.
2. **Stop the blanket erase:** replace the per-step
   `RedrawWindow(..., RDW_ERASE | RDW_ALLCHILDREN)` with targeted invalidation of only the newly
   exposed strips (or `ScrollWindowEx` for the scrolling pane). Drop `RDW_ERASE`.
3. **Batch the moves:** move pane children with `BeginDeferWindowPos`/`DeferWindowPos`/
   `EndDeferWindowPos` using `SWP_NOREDRAW`, then a single `UpdateWindow`.
4. **Narrow the erase:** in `WM_ERASEBKGND` (`dialog.c:1691`) fill only `GetUpdateRect`, or return
   `TRUE` without filling (children clip already).
5. *(done as an extra)* **Skip no-op moves:** `sf_lay_place()` returns early when the target rect equals
   the current one, so a scroll step no longer re-invalidates (and repaints) the preview bitmap and the
   pinned buttons.
6. *(optional, higher effort)* Move the settings pane into a dedicated child pane window with
   `WS_CLIPCHILDREN | WS_VSCROLL | WS_EX_CONTROLPARENT` and scroll it with
   `ScrollWindowEx(..., SW_INVALIDATE | SW_SCROLLCHILDREN)`.

### A.3 Exit criteria
- No dark flash while wheeling / dragging the scrollbar / resizing; light **and** dark mode; 100–200% DPI.
- `test/run.sh` green (unaffected — dialog isn't in the harness) and `build-mingw.sh` clean.
- No change to any control id, layout, or `sf_gather` semantics.

### A.4 Estimate / risk
**~0.5 day.** Low risk. Purely additive to the current dialog. This is throwaway once B lands, but it
de-risks the (unknown-length) interval before then.

## 5. Workstream B — Dear ImGui dialog (architecture)

> **Status: B0–B5 implemented 2026-10-17.** ImGui v1.93.0 is vendored under
> `hosts/ps8bf/third_party/imgui` (shallow clone, `.git` removed). Both build paths compile the
> C++ TUs and stay clean — `build-mingw.sh` and `cmake --preset mingw64` — with the shipping import
> table still exactly the four DLLs (D8: `d3d11`/`dxgi`/`d3dcompiler_47` are `LoadLibrary`'d; `dwmapi`,
> `imm32` and `shell32` are avoided by a one-line backend patch + build defines; the Win11 DWM chrome is
> also reached through `LoadLibrary`). `test/run.sh` is untouched and green. The dialog shows a live
> preview as an RGBA8 D3D11 texture (fit / 1:1 crop + pan) through the existing `sf_preview` transform,
> and **B3 ports every `dialog.c` control** into collapsible ImGui groups (label | slider |
> editable buddy box; middle/right-click resets to the engine default; tooltips on every control;
> the document-profile detection is shared by both backends in `sf_gui.c`). **B4 adds the polish**:
> a flat, Win11-ish dark/light theme (`SPEKTRAFILM_GUI_THEME=dark|light`, or the *Theme* button in the
> header), an accent OK button, keyboard navigation (Enter accepts / Esc cancels), a theme-matched
> clear colour, per-monitor DPI handling (`WM_DPICHANGED` rescales the style and the per-monitor font
> size) and the Win11 DWM chrome (rounded corners + immersive title bar). **B5 flipped the default** to
> ImGui; **B6 (2026-10-19) retires the native dialog**: the backend switch and `SPEKTRAFILM_GUI=native`
> are gone, and a failed D3D11/WARP init now reports an error and cancels rather than falling back.
> **Verified in Affinity on Windows 11 (2026-10-18):** window, live preview, control round-trip, theme
> toggle, DWM chrome and DPI change all behave. The bug hunt / hardening pass (MSVC link blockers + engine
> edge cases) is complete — see §12.

```
filter_loop.c  (Continue)
   └─ sf_run_params_dialog(owner, fr, block, src, sw, sh)     ← unchanged seam (sf_gui.h)
        └─ sf_run_dialog_imgui()   (C++, sf_gui_imgui.cpp)     [sole backend since B6]
              ├─ ImGui core (vendored) + imgui_impl_win32 (platform/input)
              ├─ imgui_impl_dx11 (official renderer) + sf_d3d11 (device/swap-chain)
              ├─ sf_preview.c     (existing; output expanded to RGBA8 by the ImGui layer)
              └─ params.c / sf_gather  (existing, UNCHANGED — same sf_params_block)
```

### B.1 Module layout (new / changed)
| File | Role |
|---|---|
| `hosts/ps8bf/third_party/imgui/**` | **New**, vendored pinned ImGui: core + `backends/imgui_impl_win32.*` + `backends/imgui_impl_dx11.*` |
| `hosts/ps8bf/src/sf_gui.c` | **New** (C): backend selection + the `extern "C"` dispatch; keeps the `dialog.h` seam. Also hosts the shared document-profile detection (`sf_detect_document_profile`) both backends use |
| `hosts/ps8bf/src/sf_gui.h` | **New**: the `extern "C"` native/imgui backend declarations + `sf_detect_result` |
| `hosts/ps8bf/src/sf_gui_imgui.cpp` | **New** (C++): window/modal loop, frame build, widget map, params gather/apply |
| `hosts/ps8bf/src/sf_d3d11.cpp/.h` | **New** (C++, so the COM class methods + `__uuidof` are usable): D3D11 device + DXGI swap chain + RTV, WARP fallback, the preview texture, and the dynamic loader shims (`d3d11.dll`/`d3dcompiler_47.dll`) incl. the `D3DCompile` shim |
| `hosts/ps8bf/src/dialog.c` | **Deleted in B6** (2026-10-19): the native fallback and the A-fixes were removed once ImGui was signed off in Affinity |
| `hosts/ps8bf/src/dialog.h` | **Deleted in B6** (2026-10-19): the `sf_run_params_dialog` seam declaration moved into `sf_gui.h` |
| `hosts/ps8bf/res/resource.rc` | **Changed in B6**: the `IDD_PARAMS` dialog template removed; only the PiPL + VERSIONINFO remain |
| `hosts/ps8bf/src/sf_preview.c/.h` | **Unchanged** engine-wise (the ImGui layer expands RGB→RGBA); only `extern "C"` guards added to the header |
| `hosts/ps8bf/CMakeLists.txt` | **Changed**: enable CXX, add ImGui + the new TUs, add options |
| `hosts/ps8bf/build-mingw.sh` | **Changed**: compile the C++ TUs, static libstdc++/libgcc/winpthread |
| `hosts/ps8bf/test/Makefile` | **Unchanged** (must stay C-only; the GUI is host-only) |

### B.2 Renderer backend — ImGui's official Direct3D 11 backend
Use the pairing from ImGui's own `examples/example_win32_directx11`: **`imgui_impl_win32`** for
platform/input and **`imgui_impl_dx11`** for rendering. No custom renderer is needed — this is the
maintained, canonical Windows path and removes the biggest risk of the earlier Direct2D plan.

- **Device + swap chain.** Create with `D3D11CreateDeviceAndSwapChain` (through our dynamically loaded
  `d3d11.dll`/`dxgi.dll`) using a `DXGI_SWAP_CHAIN_DESC` with `BufferCount = 2`, format
  `DXGI_FORMAT_R8G8B8A8_UNORM`, and a flip/discard model. Bind an RTV per frame, hand the device and
  context to `ImGui_ImplDX11_Init`, and call `ImGui_ImplDX11_RenderDrawData`.
- **WARP fallback.** Try `D3D_DRIVER_TYPE_HARDWARE` first; on failure fall back to
  `D3D_DRIVER_TYPE_WARP` (software D3D11, available on every supported Windows). This covers VMs, RDP,
  and GPU-less machines — so "even if it needs a GPU" is safe. If *both* fail, fall through to the
  **native dialog** so the plugin still works.
- **Render on demand, not at 60 fps.** A settings dialog is static most of the time. Drive a frame only
  when input arrives or state is dirty (message-driven loop), presenting with vsync — avoids burning GPU
  while the user is idle. (If animations/fades are added later, switch that region to a timed loop.)
- **Shader-compilation dependency.** `imgui_impl_dx11` calls `D3DCompile` by default (→ `d3dcompiler.lib`
  → `d3dcompiler_47.dll`). To keep the import list clean, take the ImGui-documented route and
  **precompile the two tiny shaders once, embed the bytecode**, and pass it to
  `CreateVertexShader`/`CreatePixelShader` (this is ImGui's own "preferred solution" for removing the
  dependency). Fallback: `LoadLibrary("d3dcompiler_47.dll")` + `GetProcAddress("D3DCompile")`.
- **Device-lost / TDR.** Handle `DXGI_ERROR_DEVICE_REMOVED`/`RESET` and presentation failures by
  recreating the device + swap chain (and re-uploading the font atlas + preview texture); if recreation
  fails, report + cancel (no native fallback since B6).
- **Feature level.** Request 11.0 with a 10.0 fallback — more than enough for ImGui.
- **Multi-window / adapter.** Bind to the adapter owning our HWND if needed; a single adapter and one
  swap chain per dialog window is sufficient.

### B.3 Window, modal loop, input
- Create our own top-level window (`RegisterClass` + `CreateWindow`), style
  `WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX`, so we get the **native frame**
  (Win11 rounded corners + dark title bar via DWM, per the earlier modernization notes) with the client
  area fully ImGui-drawn.
- WndProc forwards to `ImGui_ImplWin32_WndProcHandler`; handle `WM_SIZE` → resize the swap chain /
  RTV and set `io.DisplaySize`, `WM_DPICHANGED`, `WM_CLOSE` (cancel), `VK_ESCAPE`/`VK_RETURN`.
- **Modal loop:** `EnableWindow(owner, FALSE)` → message loop (`PeekMessage`/`TranslateMessage`/
  `DispatchMessage` + an ImGui frame when dirty) until done → re-enable + restore focus. This is the
  `DialogBoxParam` replacement; it must not block the host's own message flow beyond the modal scope
  (same as today).

### B.4 Fonts, DPI, theme
- Bake **Segoe UI** (`%WINDIR%\Fonts\segoeui.ttf`) at `24 px (18 pt) × dpi/96` into the ImGui atlas
  (ImGui's built-in stb_truetype — DirectWrite is no longer involved); fall back to Segoe UI Variable if present,
  then ImGui's built-in font. This is the single biggest "modern" lever. (Shipped at 12 px first, doubled to
  24 px in Phase 4; the window and the settings grid are sized in ems of this font.)
- Per-monitor DPI from `GetDpiForWindow` (user32; resolved dynamically so the import stays optional),
  with a `GetDeviceCaps` fallback; `WM_DPICHANGED` rescales `ScaleAllSizes` + `style.FontScaleDpi`
  between frames. ImGui 1.92+'s dynamic font system rasterises at the effective size, so there is no
  atlas reload (the font is loaded once at a fixed 96-dpi base).
- Theme: a light and a dark ImGui style. **Dark is the default** (matching the native dialog, which
  enables dark whenever the OS supports it) with a *Theme* button in the header and a
  `SPEKTRAFILM_GUI_THEME=dark|light` override. Rounded corners, generous spacing, a single
  accent-coloured primary (OK) button. The Win11 DWM chrome — rounded frame corners + an immersive
  title bar/caption colour — is applied per theme via `LoadLibrary("dwmapi.dll")`.

### B.5 Preview as a texture
`sf_preview.c` already produces a colour-correct preview (display transform). Refactor it to hand back
the **RGBA8 buffer + size**; upload it as an `ID3D11Texture2D` + `ID3D11ShaderResourceView` (ImGui's
DX11 `ImTextureID`), and draw it with `ImGui::Image`. Re-upload only when the preview changes (debounced,
as now). The 1:1/whole-image modes stay as they are.

### B.6 Parameter seam (keeps `params.c` untouched)
Reimplement the two adapter functions against ImGui widgets over the **same** `sf_params_block`:
- `sf_gather`-equivalent: read ImGui widget state → `block` (identical field mapping).
- `sf_dispatch`-equivalent: `block` → widget state on open.
The detected-space read-out becomes ImGui text; the custom tooltip timer becomes `ImGui::SetTooltip`.
Numeric fields become `SliderFloat`/`DragFloat` with min/max/default; middle/right-click-on-marker
resets to default (`IsItemHovered` + `IsMouseClicked(Middle/Right)`). **Params block stays byte-for-byte
the same version.**

### B.7 Fallback switch — removed in B6 (2026-10-19)
- ImGui is the sole backend: `SF_GUI_BACKEND` and `SPEKTRAFILM_GUI=native` are gone, and a
  D3D11/WARP init failure now reports an error and cancels (`sf_gui.c`) instead of falling back.
- Theme override kept: `SPEKTRAFILM_GUI_THEME = dark | light` (or the header's *Theme* button).

## 6. Phased plan

| Phase | Title | Exit criteria | Est. |
|---|---|---|---|
| **A** | Win32 flicker fix | §4.3 satisfied; ships — **implemented 2026-10-16**, verified in Affinity 2026-10-18 | 0.5 d |
| **B0** | D3D11 spike | Build ImGui's `example_win32_directx11`-style window from `sf_gui_select`: demo window renders (`imgui_impl_win32` + `imgui_impl_dx11`), WARP fallback works, DPI + input OK, resize/present OK — **implemented 2026-10-17** (folded into B1) | 0.5–1 d |
| **B1** | Build integration | CMake + mingw build a C-only `.8bf` that *also* compiles the ImGui TUs; empty ImGui window comes up; import table still exactly the four DLLs (D8); harness untouched — **implemented 2026-10-17** | 1–2 d |
| **B2** | Window + preview | Own window + modal loop + font atlas + DPI; live preview renders as an ImGui image via the existing `sf_preview` transform; OK/Cancel/Enter/Esc — **implemented 2026-10-17** (preview as an RGBA8 D3D11 texture, fit/1:1 view modes + pan; the settings pane is still a stub) | 3–5 d |
| **B3** | 1:1 control port | Every current control exists as an ImGui widget in collapsible groups; `sf_gather`/apply parity verified against `dialog.c` field-by-field; params block unchanged — **implemented 2026-10-17** (all ~130 controls ported; document-profile detection factored into shared `sf_gui.c`; history/grain animation still out of scope) | 5–10 d |
| **B4** | Polish | Tooltips, middle/right-click default reset, light/dark theme, accent button, keyboard nav, scroll behaviour, Win11 DWM chrome (corners + dark title bar) — **implemented 2026-10-17** (tooltips + middle/right-click reset landed with B3; B4 adds a flat Win11-ish dark/light theme + `SPEKTRAFILM_GUI_THEME`, accent OK button, Enter/Esc keyboard nav, `WM_DPICHANGED` style/font rescale, and DWM rounded corners + immersive title bar via `LoadLibrary`) | 3–5 d |
| **B5** | Default flip + fallback + docs | `SF_GUI_BACKEND` default = `imgui`; env override documented; README/ROADMAP updated; native dialog still builds — **implemented 2026-10-17** (CMake + `build.ps1` + `build-mingw.sh` default to ImGui; `SPEKTRAFILM_GUI=native` and `-DSF_GUI_BACKEND=native` still select the native dialog, and a D3D11 init failure falls back automatically) | 1–2 d |
| **B6** | retire native dialog | Delete `dialog.c` + the A-fixes and the switch, once ImGui is signed off in Affinity — **done 2026-10-19** (`dialog.c`/`dialog.h`/`resource_ids.h` deleted, `IDD_PARAMS` removed, backend switch + `SPEKTRAFILM_GUI=native` dropped; a D3D11 init failure now reports and cancels) | 0.5 d |
| **H** | Bug hunt / hardening | Every bug found in the pre-ship review fixed (two MSVC link blockers, ICC bounds reads, `plugin_main` null-deref order, auto-exposure drift, C4244 casts, `profiles/*.json` build deps); harness green (byte-identical) + ASan/UBSan clean; import list still exactly the four DLLs; Windows-verified — **done 2026-10-18** | — |

Estimates assume one developer, part-time. B0 is now low-risk (stock backend). B3 dominates because
~130 controls must be mapped and parity-checked.

## 7. Build-system changes (concrete)

**`CMakeLists.txt`**
- `project(spektrafilm C CXX)`, `set(CMAKE_CXX_STANDARD 17)`.
- New sources: ImGui core (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`),
  `backends/imgui_impl_win32.cpp`, `backends/imgui_impl_dx11.cpp`, and the new `sf_gui.c` /
  `sf_gui_imgui.cpp` / `sf_d3d11.c`.
- Defines: `IMGUI_DISABLE_DEMO_WINDOWS`, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, `IMGUI_DEFINE_MATH_OPERATORS`.
- MSVC: `MSVC_RUNTIME_LIBRARY "MultiThreaded"` (`/MT`) for C **and** C++ (no `/MD`, no CRT DLL).
- GPU libs: **do not** add `d3d11`/`dxgi`/`d3dcompiler` to `target_link_libraries` (dynamic-load policy,
  D8). Keep the SDK headers (`d3d11.h`, `dxgi.h`, `d3dcompiler.h`) for the interface/typedef declarations;
  the DLLs are opened with `LoadLibrary` in `sf_d3d11.c`.
- ~~Option `SF_GUI_BACKEND` with `target_compile_definitions`~~ — removed in B6 (ImGui is the sole backend).

**`build-mingw.sh`**
- Compile the C++ TUs with `x86_64-w64-mingw32-g++`, link `-lstdc++ -static-libgcc -static-libstdc++`
  (avoid adding `libstdc++-6.dll` to the validation build). Keep the import-table print.

**`build.ps1` / guard**
- CRT check stays. The import allow-list is now `KERNEL32/USER32/GDI32` (COMDLG32 dropped in B6) — a stray `D3D11.DLL`/`DXGI.DLL`/`D3DCOMPILER_47.DLL`/`libstdc++`
  import (i.e. an accidental static link) fails loudly. This is the enforcement arm of decision D8.

**`CMakePresets.json`**
- ~~Extend `mingw64`/`msvc` presets with `SF_GUI_BACKEND`; add an optional `*-native` variant~~ —
  removed in B6 (no backend switch remains).

## 8. Testing & verification

- **Engine parity (gates everything):** `hosts/ps8bf/test/run.sh` green after every phase; confirm the
  harness `Makefile` still excludes all GUI sources (it compiles C only).
- **DLL/import guard:** `build.ps1` (Windows) + `build-mingw.sh` objdump (Linux) show the intended set.
- **GUI smoke (Windows, manual checklist):** open in Affinity; OK/Cancel/Enter/Esc; every control maps
  to the right param; sliders/typed edit + middle/right-click reset; preview correct in both view
  modes; light/dark; 100/150/200% DPI; resize; the exact modal-in-Continue flow.
- **GPU robustness matrix:** hardware GPU, WARP (force via env override / GPU-less VM), and
  RDP/VirtualBox; confirm no `DXGI_ERROR_DEVICE_REMOVED` leaks and that init failure surfaces the
  error dialog (`sf_gui.c`) rather than silently doing nothing (no native fallback since B6).
- **Optional automated visual regression (Windows CI):** render one ImGui frame offscreen (an offscreen
  D3D11 render target instead of the swap chain), dump PNGs, diff against goldens — the only realistic
  way to catch visual regressions without a human.
- **Fallback parity:** for a matrix of params, assert the native and ImGui dialogs produce an identical
  `sf_params_block` (same gather result).

## 9. Risks & mitigations

| # | Risk | Mitigation |
|---|---|---|
| **R1** | GPU/display stack inside a plugin host (device creation, device-lost/TDR, RDP/VM/headless) | WARP soft-render fallback; handle `DEVICE_REMOVED`/`RESET` by recreating; report + cancel if init fails (no native fallback since B6); test the GPU matrix (§8). *(The old "no official D2D backend" risk is gone — D3D11 is ImGui's maintained default.)* |
| R2 | `d3dcompiler_47.dll` dependency and version skew | Embed precompiled shader bytecode (ImGui's preferred solution); otherwise `LoadLibrary` + `GetProcAddress` |
| R3 | ImGui modal loop / input / focus / IME inside a plugin host | Own WndProc forwarding to `imgui_impl_win32`; test early and often in Affinity (the native fallback that used to de-risk this was retired in B6) |
| R4 | Per-monitor DPI on a host-owned window | `GetDpiForWindow` + font-atlas rebuild + `ScaleAllSizes`; validate at 100/150/200% |
| R5 | C++ runtime leaking into the `.8bf` | MSVC `/MT`; mingw static libstdc++/libgcc; import-guard assertion in `build.ps1` |
| R6 | Binary size growth (ImGui ≈ +0.3–0.5 MB) | Acceptable; measure; strip demo/support code |
| R7 | ~130-control port drift vs `dialog.c` | Drive the **same** `sf_params_block`; add a native-vs-ImGui gather-parity test (no engine change) |
| R8 | Accessibility / keyboard-nav regression | Enable ImGui keyboard nav + nav highlight; document the gap |
| R9 | Host rejects a custom top-level window | Keep the native (dialog-template) path as fallback; probe in Affinity during B2 |

## 10. Licensing / notices

- Dear ImGui: MIT — vendored with `LICENSE.txt` unmodified, its full text
  reproduced in `THIRD-PARTY-NOTICES.md` at the repository root, and credited in
  the About dialog ("Dear ImGui: MIT"). Done.
- No Adobe SDK files are vendored (unchanged; `pi/` is our own declaration of the ABI).
- `LegalCopyright` in `res/resource.rc` states the engine licence and the data
  pack licence. The earlier plan to squeeze the ImGui MIT notice into it was the
  wrong place: a version-info string is neither readable nor a place a licence
  notice can be reproduced in, so the notice lives in `THIRD-PARTY-NOTICES.md`
  and `build.ps1` stages that file next to the `.8bf`. Done.

## 11. File map (new / changed)

**New**
- `hosts/ps8bf/third_party/imgui/**` (vendored, pinned; core + `imgui_impl_win32` + `imgui_impl_dx11`)
- `hosts/ps8bf/src/sf_gui.c` (C dispatch/selection)
- `hosts/ps8bf/src/sf_gui_imgui.cpp` (C++ dialog)
- `hosts/ps8bf/src/sf_d3d11.c` / `sf_d3d11.h` (device/swap-chain + WARP + dynamic loader shims)
- `hosts/ps8bf/src/imgui_impl_dx11_shaders.h` (optional precompiled shader bytecode)
- `docs/GUI_ROADMAP.md` (this file)

**Changed**
- `hosts/ps8bf/res/resource.rc` (A: `WS_CLIPCHILDREN`) — the whole `IDD_PARAMS` template went in B6
- `hosts/ps8bf/src/dialog.c` + `dialog.h` (deleted in B6; the native fallback is gone)
- `hosts/ps8bf/src/sf_preview.c` / `.h` (expose RGBA8 buffer)
- `hosts/ps8bf/CMakeLists.txt`, `build-mingw.sh`, `build.ps1`, `CMakePresets.json`
- `hosts/ps8bf/README.md`, `ROADMAP.md` (status + pointers)

**Unchanged (by contract)**
- `spektra-core/**`, `src/params.c` semantics, the params-block layout, `test/**`

## 12. Immediate next steps

Workstream A and phases B0–B5 have landed, and the pre-ship **bug hunt / hardening pass is
complete (2026-10-18)**:

- [x] **Verified in Affinity on Windows 11 (2026-10-18)** — the flicker fix, and the ImGui dialog
  (default): window comes up, preview is colour-correct, every control round-trips, the theme
  toggle and the DWM chrome behave, DPI changes when dragging between monitors. `run.sh` stays
  green.
- [x] **Fallback check** — retired in B6: `SPEKTRAFILM_GUI=native` and the auto-fallback no longer
  exist; a machine without a usable D3D11/WARP device now gets an error + cancel from `sf_gui.c`.
- [x] **Bug hunt / hardening pass (2026-10-18).** Every bug found in the pre-ship review is
  fixed: two MSVC link blockers (an unmangled C++ reference to `g_sf_hinst` → `extern "C"`; the
  asset stdio shim colliding with the static CRT → `sf_asset_*`, mapped onto the CRT names for the
  engine TUs only, which also fixed the host transcript-log hijack); several out-of-bounds
  ICC-tag reads in `icc_profile.c`; a `sf_parameters` null-deref ordering bug in `plugin_main.c`;
  auto-exposure drift (metering now writes a dedicated `auto_exposure_ev` carved from tail
  padding, so `sizeof(sf_params_block)` stays 840); two C4244 casts; and build-dependency
  staleness for the pack's `profiles/*.json` (CMake `GLOB CONFIGURE_DEPENDS` + Makefile wildcard).
  The Linux harness is green (byte-identical to `tools/refrender`) and clean under ASan/UBSan;
  `build-mingw.sh` + `cmake --preset mingw64` are clean; the shipping build imports
  KERNEL32/USER32/GDI32 (COMDLG32 was required only by the retired native dialog).
- [x] **B6** — retire `dialog.c` + the A-fixes and the switch once ImGui is signed off —
  **done 2026-10-19**.

---

## 13. Post-B6 GUI/preview polish — v0.1.0 (2026-11-04)

A final GUI/preview pass, all **verified in Affinity Photo on Windows 11**, closing the gap
between "the dialog works" and "the dialog ships". Folded into **v0.1.0**.

- [x] **Header** shows the product name + version only (no build stamp).
- [x] **About** is a real modal dialog: it dims/blocks the settings dialog, Esc over it closes
  only the modal (no longer cancels the plug-in window), and it reports the build date as
  **EN ISO 8601 (`YYYY-MM-DD`)** — `sf_build_date_iso()` parses `__DATE__`; the diagnostic log
  keeps the full date+time stamp. The About button has no tooltip.
- [x] **Preview matches the final render:** a true native-scale **1:1 crop** of the pane-sized
  region plus a **≤2× supersampled whole-image** view (rendered supersampled, then downsampled).
- [x] **1:1 crop no longer renders black** — the blit advanced its destination pointer once per
  row instead of once per pixel (`d += 3`); the harness now asserts a high non-black pixel
  fraction on the preview.
- [x] **1:1 pan drag tracks the cursor exactly 1:1** — the cursor delta is converted to source
  pixels and divided by the pan slope, so the grabbed pixel stays under the cursor (was ~2×).
- [x] **Checkbox selected state** keeps its blue tick at all times (accent at low alpha, not a
  solid fill); hovering does not change the indicator.
- [x] **Host menu** shows Spektrafilm **once**: Affinity always nests each `.8bf` under a submenu
  labelled by the PiPL `catg`, so `catg` = "Film Simulation" with the item `name` = "Spektrafilm"
  (an absent `catg` renders an empty submenu, not a flat entry).
- [x] **Build:** the MSVC shipping build defaults to **Visual Studio 2026** (v145); VS 2022 is the
  fallback.

**Status: complete.** This closes the GUI workstream; the plug-in is released as **v0.1.0**.
