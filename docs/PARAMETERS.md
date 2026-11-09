# Parameters: from slider to engine, safely

Status: **current-state, 2026-11-07.** The parameter surface, its lifecycle,
and the four guardrail layers a number passes through.

## One block, four faces

`sf_params_block` (`hosts/ps8bf/src/params.h`) is the single source of truth
for every setting. It is a flat struct (magic `'SFP1'`,
`SF_PARAMS_BLOCK_VERSION` 21) that lives in four places at once:

- **the GUI**, which edits it in place while the dialog is open;
- **the host's parameters Handle** (`FilterRecord::parameters`), so Affinity
  remembers settings between invocations and across sessions - the dialog's
  OK writes the block back, growing the Handle first if the host stored a
  smaller older one;
- **preset files**, `%APPDATA%\spektrafilm\presets\<name>.json`, plus the
  clipboard (same JSON, `CF_TEXT`);
- **the engine**, via `sf_params_to_chain()` into `sp_chain_params_t`, whose
  key names are shared with the OFX side (`sp_params.c`) so a look is
  expressible on both ports.

Rules that keep the four faces consistent:

- Never add a field in the middle of the struct: bump
  `SF_PARAMS_BLOCK_VERSION` and keep the read path compatible (a block of a
  different version is re-inited to defaults - the deliberate vertical-slice
  policy).
- Any field that is part of a *look* must also be added to `sf_pfields[]` in
  `preset.c`; one left out silently keeps the engine default when a preset
  loads (the harness round trip catches most such slips). Derived fields are
  deliberately absent: `input_custom_*` (comes from the document ICC) and
  `auto_exposure_ev` (metered every invocation).
- Preset JSON is `{"format": "spektrafilm-preset", "version": 1, "params":
  {...}}`, written field-by-field through the offset table, hand-editable,
  in the **shortest decimal form that reads back bit-exact** (a load never
  perturbs a value, so the render stays byte-identical), always
  dot-separated regardless of the host's locale - the separators are swapped
  explicitly because the process locale belongs to the host.

## The four guardrail layers

A number can reach the engine through four routes: a slider drag, a numeric
input box, a preset file, or the clipboard/host-persisted block. The last
three never pass a slider, which is why one clamp is not enough.

1. **Slider clamp** (`sf_gui_imgui.cpp`): every row is a slider with a
   comma-aware numeric buddy (`sf_numf`); both clamp to the row's range
   before writing, so interactive editing cannot even *store* an out-of-range
   value.
2. **Validator** (`sf_params_validate()`, `params.c`): runs on every block
   from every route before it reaches the engine; returns 0 or a distinct
   nonzero code. This is the layer that gates presets, clipboard and
   host-persisted blocks. Codes are stable identifiers, not unique per
   check (16/17/18 each cover several related checks):

   | Code | Check |
   |---|---|
   | 1 | null block |
   | 2 | magic |
   | 3 | version |
   | 4 | `film_index` in range |
   | 5 | `paper_index` in range (unless scan mode) |
   | 6 | `print_exposure` 0..10 |
   | 7 | `exposure_ev` -8..8 |
   | 8 | `lut_steps` 0 (exact) or 2..33 (1 is meaningless) |
   | 9 | `pixel_um` 0.5..40 |
   | 10 | `input_space` in range |
   | 11 | `coupler_diffusion_um`: -1 sentinel ("use the pack's") or > 0 |
   | 12-14 | DIR coupler matrix controls, each 0..2 |
   | 15 | `input_transfer` in range |
   | 16 | `film_format` in range; `diffusion_on` 0/1 |
   | 17 | `rgb_to_raw_method` 0..2; `diffusion_family` 0..3 |
   | 18 | the flag/finite sweep: `halation_on`, `process_mode`, scan/timing/printer flags, camera filter flags, printer lights finite, UV cut 280..460 nm, IR cut 600..760 nm |
   | 19 | `grain_on` 0/1 |
   | 20-21 | `print_diffusion_on` 0/1; family 0..3 |
   | 22 | the big finiteness sweep: every strength, radius and shaping value |
   | 23-24 | scan black/white correction flags 0/1 |
   | 25 | finiteness of DIR gammas, halation strengths/radii, filter and preflash shifts, scan levels |
   | 26 | `film_push_pull_stops` -4..4 |
   | 27-28 | `film_gamma`, `print_gamma` 0.25..4 |
   | 29 | `filter_c_shift` 0..120 CC |
   | 30 | `print_push_pull_stops` -2..2 |
   | 31 | print toe/shoulder shaping -1..1 |
   | 32-35 | bleach bypass: finite, then amounts 0..1 / leuco coupling 0..2 |
   | 36-38 | `grain_model`, `grain_sublayers_enabled` 0/1; `grain_sublayer_count` 0..8 |
   | 39 | grain per-channel scales/uniformities finite; um overrides finite (negative = stock-scaled sentinel) |
   | 40 | per-channel halation first-bounce radius <= its cap (the Sigma Max, else the stock's 65 um) |
   | 41 | `halation_sigma_max_um`: < 0 = stock ceiling, else 0 < x <= 1000 um |
   | 42 | `auto_exposure_ev` finite (it is a computed field, not user input) |
   | 43 | the four blur scale multipliers within `SF_BLUR_SCALE_MIN`..`MAX` (0..4) |

   Code 43 is the instructive one: each multiplier multiplies a radius in
   micrometres, finiteness is not enough, and a preset from disk never passed
   a slider - and halation runs `(sigma_px / 150)^2` recursive passes, so an
   unbounded multiplier is an unbounded render time.
3. **Chain clamps** (`sp_chain.c`, engine side): the last line of defence,
   independent of the surface. The halation first-bounce sigma clamps to
   `SP_HALATION_FIRST_SIGMA_UM` (65.0, the stock's ceiling); the recursive
   Gaussian clamps per-pass sigma at `SF_GAUSS_MAX_IIR_SIGMA` (150.0) and the
   pass count at `SF_GAUSS_MAX_IIR_PASSES` (65536) - both in the vendored
   `spektra_core.h`, both exercised at the cap by the harness.
4. **Slow-preview note** - *not* a guardrail, and deliberately so: when a
   blur's preview sigma exceeds `SF_PREVIEW_SLOW_SIGMA_PX` (1200 px) the
   dialog draws one grey line ("Wide ... makes the preview slow to update")
   at the end of the section. It informs; it never blocks, because the wide
   blur is a legitimate final render.

Sentinels are part of the contract: `-1.0` coupler diffusion = "the pack's",
negative halation radii = "the stock's own", negative grain scale overrides =
"the stock-scaled path". The validator encodes exactly which fields use them.

## European number formatting

The GUI and its tooltips are for a European audience: every displayed number
carries a **comma** decimal separator. The machinery (`sf_gui_imgui.cpp`):

- `sf_num_txt()` formats with a comma; `sf_slider_text()` draws what a slider
  would have drawn; `sf_numf()` is the comma-aware `InputFloat` wrapper
  (clamp before write, then display with all its decimals).
- Keyboard entry accepts the comma through
  `ImGuiPlatformIO::Platform_LocaleDecimalPoint = ','` - ImGui itself only
  understands `.`.
- Static tooltip strings are written with commas already ("Default 1,000",
  "range 0,50..40,00 um"); dynamic values in inline notes go through
  `sf_num_txt` before display.
- The value-column width is measured from the literal `"-1,0000"` precisely
  because numbers carry a comma.

The preset file is the one place a **dot** is mandatory: it is JSON, and a
host may have called `setlocale()` - see the lifecycle rules above.

## Auto exposure

`auto_exposure` (off by default) is a look field; `auto_exposure_ev` is not -
it is *measured* from the still-encoded frame at the start of every
invocation (`filter_loop.c`), stored on the block for display (validator code
42 only checks finiteness), and the render chain is rebuilt with it before
the dialog runs, so preview and final render share one measured value.
Distinct from it: `print_auto_exposure` is an engine-side look flag
(re-normalise mid-grey through the print stage, also off by default,
matching the upstream engine's choice).
