# docs/ - index

Status: **current-state, 2026-11-09.** If you are an agent (or human)
catching yourself up on this project: start here, then read
`ARCHITECTURE.md` and `CONVENTIONS.md`. That trio is the whole current
truth; everything else is detail or history.

## Current-state docs (describe the tree as it is)

| Doc | What it answers |
|---|---|
| `ARCHITECTURE.md` | How the thing works: layers, the .8bf contract, render/preview/pack data flows, threading, the load-bearing invariants, where each concern lives. |
| `CONVENTIONS.md` | What you must not break: output style, warnings policy, git and changelog discipline, licensing wording, locked GUI decisions, doc rules - and the release process with the honest publication state. |
| `BUILDING.md` | How to build on Windows (shipping) and Linux (validation), what the build embeds and enforces, the warning baseline, known traps. |
| `TESTING.md` | What verifies what: the two Linux gates and every check inside them, the Windows gate, optional probes, which guarantee rests on which test. |
| `PARAMETERS.md` | The parameter contract: one block with four faces, the complete validator code table, the four guardrail layers, European number formatting. |
| `ABOUT_BOXES.md` | Findings: the plug-in's two About surfaces, why one is invisible in Affinity, how to force it anyway. |

## Historical docs (dated records; bodies never rewritten)

The four roadmaps carry a status banner saying what shipped and where
current state lives now; the two findings docs carry a dated status
header of their own.

| Doc | Record of |
|---|---|
| `GUI_ROADMAP.md` | The scroll-flicker fix and the Dear ImGui dialog workstreams (shipped; v0.1.0 polish in §13). |
| `GRAIN_SYSTEM_ROADMAP.md` | Porting the grain surface to the CPU path (shipped). |
| `HALATION_SYSTEM_ROADMAP.md` | Porting halation/scatter (shipped). |
| `PORT_PARITY_ROADMAP.md` | Inventorying the gaps to upstream (largely closed; the genuinely-open items are named in its banner). |
| `EFFECTS_FAMILY_FINDINGS.md` | Evidence that the OFX "Effects" family is GPU-only and out of scope. |
| `OFX_FEATURES.md` | Extraction of the OFX user-facing parameter surface (reference for what upstream exposes). |

## Suggested reading order

1. This index.
2. `ARCHITECTURE.md` - the mental model.
3. `CONVENTIONS.md` - before changing anything.
4. `BUILDING.md` / `TESTING.md` - when you are about to build or verify.
5. `PARAMETERS.md` - before touching the parameter surface.
6. Then, as needed: `hosts/ps8bf/README.md` (adapter hub + validation log),
   `hosts/ps8bf/INSTALL.md` (installing on Windows), `CHANGELOG.md` (what
   changed when), `SOURCES.md` (upstreams and pins), `tools/README.md`
   (data machinery), `spektra-core/README.md` (engine layer),
   `THIRD-PARTY-NOTICES.md` (every licence, satisfied where).

## Where the root docs live

- `README.md` (repository root) - the project front page, maintainer-authored:
  what the plug-in is, the GUI screenshot, build requirements, workflow tips
  and the references list. Its images live in `img/readme/`.
- `ROADMAP.md` - the research report and plan of record (historical, with a
  current status header).
- `CHANGELOG.md` - Keep a Changelog history; `SOURCES.md` - upstream provenance
  and pins; `LICENSE` + `THIRD-PARTY-NOTICES.md` - licensing.
- `hosts/ps8bf/INSTALL.md` - the end-user install guide (Affinity Photo,
  `%USERPROFILE%\.affinity\Affinity\3.0\Plugins`).

## House rules for this folder

New findings get their own focused doc (question, answer, evidence,
reproduction recipe - `ABOUT_BOXES.md` is the exemplar) and are
cross-referenced from `hosts/ps8bf/README.md`. A doc that goes stale gets a
status banner; its body is never rewritten. Current-state docs are updated
in place, with a `Status:` line carrying the date they were last checked.
