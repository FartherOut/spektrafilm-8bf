# Findings — the two About boxes

Status: **verified on Windows 11 + Affinity Photo, 2026-11-06.**

The plug-in exposes two separate About surfaces. They are easy to confuse, and
one of them is invisible on our primary host, so this records what each is,
where it comes from, and how to see it.

## 1. The in-window About modal (the one everyone sees)

The **About** button at the bottom of the plug-in's own settings window opens a
modal dialog (`hosts/ps8bf/src/sf_gui_imgui.cpp`): product name and version,
build date (EN ISO 8601), acknowledgements, the GPL-3.0-or-later notice, and
the data-pack attribution with its modification note. It is wrapped and styled
by the dialog itself and needs the dialog's window and D3D device, so it only
exists while the plug-in window is open.

## 2. The host-side About box (invisible in Affinity)

The `.8bf` interface has its own About channel: the host may call the exported
`PluginMain` with `filterSelectorAbout` (selector 0) at any time the plug-in is
loaded, outside any filter run — no window, no `FilterRecord` content required.
Photoshop wires this to an "About this plug-in" entry under the filter's menu;
the host draws nothing itself, the plug-in answers with its own dialog.

Ours (`sf_about()` in `hosts/ps8bf/src/plugin_main.c`) is deliberately a
pointer, not a copy of the modal: build date, a one-line description, and a
note that full credits and licence live behind the About button of the
spektrafilm window. It is a single `MessageBoxA`, so the answer always works —
no D3D device, no assets, no parameter block — and the import table stays
USER32/GDI32/KERNEL32.

**Affinity never sends the selector.** It has no per-plug-in About menu (its
Photoshop-plug-in support only runs the filter itself), so in Affinity this
box is unreachable through the UI. That is a host omission, not a plug-in
gap: the same `.8bf` in Photoshop, or behind PSPI in GIMP/IrfanView/XnView
hosts that expose the selector, will show it.

## 3. Forcing the host-side box (verification recipe)

To see the box without a host that asks for it, a one-file probe can act as
the host: `LoadLibrary` the `.8bf` (it is a plain DLL), `GetProcAddress`
`PluginMain`, and call it with selector 0 and a NULL `FilterRecord` — safe,
because the About path answers from compile-time strings and never touches
the record. Such a probe (`hosts/ps8bf/test/about_probe.c`) was used for the
Windows verification and then removed from the tree; it lives in the git
history:

    git show bafd64c:hosts/ps8bf/test/about_probe.c > about_probe.c

Build and run from an x64 developer prompt (`vcvars64.bat`), in the folder
beside a built `dist\spektrafilm.8bf`:

    cl /nologo /W4 about_probe.c
    about_probe.exe ..\dist\spektrafilm.8bf

The box appears (title `spektrafilm <version>`), modal to the console; OK
closes it and the probe prints the host result (`0` = `noErr`). Like every
`PluginMain` call it also appends its banner to `%TEMP%\spektrafilm.log`.

Verified 2026-11-06: box appears with the correct ISO build date and pointer
text, result `noErr`, on VS 2026-built `spektrafilm.8bf` (user report).
