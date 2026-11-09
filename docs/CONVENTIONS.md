# Conventions

Status: **current-state, 2026-11-09.** The rules a contributor (human or
agent) must follow, and the release process. If you are catching yourself up
on this project: read `docs/README.md`, then this file, before changing
anything.

## Output the user or operator can see

- **No em dashes or non-ASCII punctuation in console output, build logs,
  tool output or About text.** Tools the build runs print plain `--`;
  generated C comments are ASCII too. Cause: with stdout on a pipe Python
  writes UTF-8 and the Windows console renders it through the OEM codepage,
  so a dash arrives as mojibake on exactly the captured lines (commit
  `e87c584`). Docstrings count as output - argparse prints them as `--help`.
- **Full reports, nothing hidden.** No `-Quiet`, no captured stderr, no
  summarising flags, no piped-through PowerShell that strips MSBuild's
  console. What the toolchain reported is what you show.
- **Dates**: build dates display as EN ISO 8601 (`YYYY-MM-DD`) only, never
  raw `__DATE__`. Commit dates and changelog dates are plain calendar dates.
- **Decimal separators**: GUI values and tooltips use the European comma
  (see `docs/PARAMETERS.md`); preset JSON and every machine-readable format
  use the dot.

## Naming

- **The brand is `spektrafilm`, all lowercase**, everywhere a person reads it -
  GUI, host menu, About box, docs, licence text. Upstream writes its own name
  that way and asks spin-offs to attribute "film modeling powered by
  spektrafilm". Sentence position does not capitalise it.
- **The project is `spektrafilm-8bf`**, named after the repository the way
  `spektrafilm-ofx` and `spektrafilm-rs` are. Use it only where the *project*
  is named: the repository, CMake's `project()`, the VERSIONINFO `ProductName`,
  `THIRD-PARTY-NOTICES.md`, the `CHANGELOG.md` intro.
- **Never where the *filter* is named.** The artifact is `spektrafilm.8bf`, the
  CMake target is `spektrafilm`, the PiPL name and the GUI show `spektrafilm`.
  A host loads the filter, not the repository.
- **User data goes in the bare brand folder**: `%APPDATA%\spektrafilm\presets`.
  That is the family convention - the OFX plug-in writes
  `ofx-defaults-v1.spkdefaults` into the same `%APPDATA%\spektrafilm`, and
  `spektrafilm-rs` uses `%LOCALAPPDATA%\spektrafilm`. Ports tell each other
  apart by subfolder and file name, not by a folder suffix, so do not invent a
  `spektrafilm-8bf` data folder.
- **Not name displays, so they keep their existing spelling**: the
  `SPEKTRAFILM_*` environment variables, the Win32 window class
  `SpektrafilmImGuiWnd`, upstream file names (`SpektraFilmPlugin.cpp`,
  `SpektraFilm.metal`), the `spektrafilm-preset` format id, and the bodies of
  historical docs.
- **Prose says `plug-in`**, hyphenated: that is the Oxford spelling for the
  noun ("a piece of computer software that can be added to a system"), with
  `plug in` as the verb. It applies to the README and to every current-state
  doc; the bodies of historical docs are never rewritten for it. Names
  keep their own spelling and are not renamed for it: `res/plugin.def`,
  `PluginMain`, and quoted host UI such as "Plugin search folders".

## Compiler warnings

Fix causes, never suppress. The baseline is exactly **five `warning C4068` /
`-Wunknown-pragmas`** from the vendored `grain.h` pragmas in the five
translation units that include it. The vendored engine stays unmodified, so
those five stay in the report; **any other code means new diagnostics -
investigate, do not silence**. `build.ps1` breaks counts out per code so a
new class cannot hide among old ones.

The one deliberate exception, stated so it cannot spread: `pi/pifilter.h`
wraps the `FilterRecord` declaration in a scoped
`#pragma warning(push/disable:4121/pop)`. C4121 ("alignment of a member was
sensitive to packing") fires *because* the `pack(4)` layout is intentional -
the byte-exact layout is the requirement, so there is no cause left to fix;
the disable covers that declaration only and cannot leak.

## Git

- One commit per logical change; each builds on its own. Subject line:
  imperative, says what changed; body: **why**, with the cause and the
  evidence, not a restatement of the diff. Messages are ASCII.
- `CHANGELOG.md` follows Keep a Changelog + SemVer; user-visible changes get
  an entry under `[Unreleased]` as part of the change, not at release time.
- Moving a submodule pin: `git submodule update --remote <path>` and commit
  the gitlink; nothing else has to notice, because the embedded pack is
  re-derived from the pin on the next build. Explain *why* the new pin in the
  commit body.

## Data and submodules

- `spektra-core/data/pack` is **derived and git-ignored**. `tools/sync_pack.py`
  is its only writer; never hand-edit or commit anything under it, and never
  add a fallback to the pristine pack (that fallback once built fine while
  silently changing 20 film profiles' renders).
- Anything the **build reads** is a pinned submodule
  (`spektra-core/third_party/darktable-spektrafilm`,
  `upstream/spektrafilm-ofx`). Anything **reference-only** lives in the
  git-ignored `upstream/` via `tools/fetch-sources.sh` and stays unpinned -
  except `spektrafilm-rs`, pinned to the tag `gen_mallett_basis.py --check`
  compares against, so a moving default branch cannot fake drift.

## Licensing and wording

- The project is **GPL-3.0-or-later**; every file carries its SPDX header.
- Wording about Adobe: this is *our own declaration of the documented .8bf
  binary layout*; the Adobe SDK is **never described as required** (it is
  not - nothing Adobe is vendored, linked or needed). Full rationale:
  "About the .8bf ABI" in `hosts/ps8bf/README.md`.
- No mentions of "agx-emulsion" (a retired upstream naming); describe the
  engine as the ported darktable `spektra` stage.
- Notices travel with every copy: `build.ps1` stages `LICENSE` and
  `THIRD-PARTY-NOTICES.md` into `dist\`, and the About dialog carries the
  data-pack attribution **with its modification note** (the pack is modified:
  the archived OFX bandpass merged into 20 film profiles - CC BY-SA requires
  saying so where the attribution is shown).

## GUI

- **Preview must match the final render** - same `sp_chain`, same parameters.
  A preview-only approximation is a bug.
- **No key applies the dialog.** Applying is a click on OK, Cancel or the
  title-bar X: OK and Cancel take `ImGuiItemFlags_NoNav`, so neither Enter nor
  Space can reach them, and the global key handler only cancels on Esc. Esc
  also stands down while a popup is open and while a control held focus going
  into the frame - Enter and Esc both end a text field's edit *inside* that
  frame, so the focus state has to be read at the top of `sf_imgui_build()`.
- **A number field commits when it is finished with**, not per keystroke:
  `ImGuiItemFlags_LiveEditOnInputText` is pushed off on every number box, so
  the value lands on Enter, Tab or a click away and the preview is not
  re-rendered as fast as the user types.
- **A slider renders when its gesture ends, not at a pause inside it.** While a
  slider holds the active id - which it does for as long as the mouse button is
  down, wherever the pointer wanders - the preview deadline is pushed out every
  frame and nothing renders; the render runs on the frame after the release. A
  wheel notch arms a longer settle window (700 ms), because rolling is a series
  of notches with looks in between and the value that counts is the one the
  series ends on. Every other change takes the 120 ms debounce.
- The About popup: fixed size that fits its content, truly modal, Esc closes
  the popup **without** cancelling the plug-in (the consuming frame is
  recorded so the global Esc handler does not act on the same key), and
  no tooltip on the About button.
- The plug-in appears **once** in the host's menu: the PiPL `catg` must
  differ from the display name or the same word shows twice
  (`tools/gen_pipl.py`).
- Slow-preview notes are information, never guardrails, and are drawn at the
  **end** of a section so an appearing row cannot move a row out from under
  the cursor being dragged.

## Documentation

- `docs/` mixes two kinds of file and says which: **current-state** docs
  (this one, `ARCHITECTURE`, `BUILDING`, `TESTING`, `PARAMETERS`) describe
  the tree as it is; **historical** docs (the dated roadmaps, findings) are
  the record. A historical doc gets a status banner when it goes stale; its
  body is never rewritten.
- New findings go into a `docs/<TOPIC>_FINDINGS.md` or a focused doc like
  `ABOUT_BOXES.md`: question, direct answer, evidence, and the recipe to
  reproduce - written so the next reader need not redo the investigation.
- Cross-reference new docs from `hosts/ps8bf/README.md` (the technical hub)
  so the graph is connected from both entry points. There is no separate
  `RELEASE.md`; the release process is this file's next section.

## Testing expectations

Both Linux gates (`test/run.sh`, `build-mingw.sh`) pass before a change is
called done; the Windows `build.ps1` report is checked against the baseline.
Windows-only behaviour is verified by the maintainer on Windows 11 + Affinity
Photo - never claim a visual or in-host verification you did not perform;
say what is pending and what the user should look at.

## Release process (and where we are)

**Versioning.** `hosts/ps8bf/src/version.h` (`SF_VERSION`) is the product
identity; keep `res/resource.rc` (`FILEVERSION`/`PRODUCTVERSION`, "keep in
sync" comment), the README and the CHANGELOG heading in step with it. The
build stamp is `__DATE__ " " __TIME__` unless overridden with
`-DSF_BUILD_STAMP=...` for a reproducible shipping build; the About surfaces
show only the ISO date.

**Pre-release checklist.**

1. Both Linux gates green.
2. `build.ps1` on Windows: report shows the five-warning baseline, 0 errors,
   imports `KERNEL32/USER32/GDI32` only, pack line matches the expected
   stamp hashes; `dist\` holds the `.8bf` plus both notice files.
3. In-host checklist from `hosts/ps8bf/INSTALL.md` on Affinity Photo (menu
   entry once, dialog, preview-equals-render, persistence).
4. CHANGELOG: `[Unreleased]` folded into a dated version section.
5. Version bump committed as its own commit.
6. Tag `vX.Y.Z` on that commit.

**Transfer to the Windows machine**: `tools/package-for-windows.sh` builds
the source bundle (tracked tree + the two pinned upstream trees flattened
in); the Windows side builds from it, which also proves the derivation route.

**Publication state (2026-11-09).** v0.1.0 publishes to GitHub as
`FartherOut/spektrafilm-8bf` from a single squashed root commit: the published
history starts with the release, and the detailed development history stays
local. The reasoning trail is the documentation, which is complete without it
- `ROADMAP.md` (the report of record), the four `docs/` roadmaps and the
`CHANGELOG.md`. The git identity is the maintainer's GitHub noreply address.
A root `CITATION.cff` is deliberately **not** wanted: citation belongs to the
original spektrafilm, whose asks the About dialog's credit line and
`THIRD-PARTY-NOTICES.md` already satisfy. From the first push on, `main` is
published history: do not rewrite it.
