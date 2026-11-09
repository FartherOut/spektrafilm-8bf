# Installing spektrafilm in Affinity Photo

spektrafilm ships as a single self-contained 64-bit Windows filter plug-in,
`spektrafilm.8bf`. There are no separate runtime DLLs, presets, or data files
to install — everything the engine needs is baked into the `.8bf`.

## 1. Build (developer)

From `hosts/ps8bf` on a Windows machine with Visual Studio 2026 or 2022
(MSVC; the free Community edition is enough) and the Windows SDK:

```powershell
.\build.ps1
```

The script produces `dist\spektrafilm.8bf`, together with `LICENSE` and
`THIRD-PARTY-NOTICES.md` so the licences travel with the binary. Before it
compiles anything it derives the embedded data pack (`tools\sync_pack.py`) from
two pinned upstream trees — `spektra-core\third_party\darktable-spektrafilm\packs`
and `upstream\spektrafilm-ofx\Resources\data\profiles\archive` — so a fresh clone
needs `git submodule update --init` first. A bundle made with
`tools/package-for-windows.sh` on the Linux side carries both as plain files, and
is verified to build from as it is written. The MinGW cross-build
(`build-mingw.sh`) is for development only and is not the shipped binary.

## 2. Install into Affinity Photo

1. Copy `spektrafilm.8bf` into Affinity's plug-in folder:
   - **Affinity (v3) on Windows 11:** `%USERPROFILE%\.affinity\Affinity\3.0\Plugins\`
     (the `.affinity` folder is hidden — type the path into the Explorer
     address bar to reach it).
   - **Affinity Photo 2:** `C:\Program Files\Affinity\Photo 2\Plugins\`.
   Any other folder works too; then just add it in step 4.
2. In Affinity Photo: **Edit → Preferences → Photoshop Plug-ins**.
3. Enable **"Allow 'Unknown' plugins to be used"**.
4. If the file does not live in the plug-in folder above, add its folder
   under "Plugin search folders".
5. Restart Affinity Photo.
6. The filter appears under **Pixel → Filters → Plugins → Film Simulation →
   spektrafilm**. The first three levels are Affinity's own menus; the last two
   come from this plug-in. `Film Simulation` is its PiPL category, which Affinity
   shows as a submenu, and `spektrafilm` is the item itself.

## 3. Usage

- The plug-in runs its own modal dialog with a live preview of the document.
- **OK** commits the result; **Cancel** (or Esc) discards it. No key commits:
  OK and Cancel are not reachable from the keyboard, so applying the filter
  always takes a click on one of them or on the window's close box.
- In a number field, **Enter** or **Tab** finishes that field - the value is
  applied and the preview redrawn once, not on every keystroke. **Tab** and the
  arrows move focus between controls.
- Presets are stored in `%APPDATA%\spektrafilm\presets\*.json` and can be
  saved/loaded/copied/pasted from the Presets group at the top of the dialog.
- Middle- or right-click any slider to reset it to the engine default.

## 4. Uninstall

Delete `spektrafilm.8bf` and restart Affinity Photo. Presets in
`%APPDATA%\spektrafilm\presets\` are left behind and can be removed manually.
That folder is shared with the other spektrafilm tools (the OFX plug-in keeps
its own defaults one level up), so delete the `presets` subfolder rather than
the whole `spektrafilm` folder.

## 5. Requirements

- Windows 10/11, 64-bit.
- Affinity Photo v2 or v3 (or any host that loads 64-bit Photoshop-compatible
  `.8bf` filters: Photoshop, IrfanView, XnView, GIMP with PSPI).
- No .NET, no Visual C++ Redistributable, no GPU required — the render is CPU-only.

## 6. Licence

spektrafilm is licensed under the **GNU General Public License v3.0 or later
(GPL-3.0-or-later)** — the full text is in [`LICENSE`](../../LICENSE) at the
root of this repository, and `build.ps1` copies it into `dist\` next to the
`.8bf`.

Everything the plug-in embeds or derives from, and what each of those licences
asks for, is listed in
[`THIRD-PARTY-NOTICES.md`](../../THIRD-PARTY-NOTICES.md) (also staged into
`dist\`): the darktable `spektra` stage the engine comes from
(GPL-3.0-or-later), the film and paper data pack (CC BY-SA 4.0, © Andrea
Volpato — its attribution and modification note are shown in the About dialog),
Dear ImGui (MIT, vendored under `third_party/imgui`), and the upstream
repositories whose terms apply:

- https://github.com/chaert-s/spektrafilm-ofx
- https://github.com/andreavolpato/spektrafilm
- https://github.com/darktable-org/darktable-spektrafilm

The `.8bf` plug-in interface is a published binary layout, not source: the host
fills in a `FilterRecord` struct and calls our exported `PluginMain` with a
pointer to it, so the struct must match Adobe's byte for byte. The declarations
under `pi/` are written by this project from Adobe's **published** SDK headers
(e.g. `PIFilter.h`, `PITypes.h`, mirrored publicly at
`AdobeDocs/photoshop-cpp-sdk`), and 121 compile-time asserts pin the
`FilterRecord` field offsets so a mismatch fails the build. The PiPL resource is
generated at build time by `tools/gen_pipl.py`. **No Adobe SDK files are vendored
or distributed, and no Adobe SDK download is required to build.**