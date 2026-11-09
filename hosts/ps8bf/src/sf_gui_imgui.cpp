/* sf_gui_imgui.cpp — Dear ImGui + D3D11 dialog.
 *
 * Owns the whole dialog: a native-framed top-level window whose client area is
 * drawn entirely by ImGui through imgui_impl_win32 + imgui_impl_dx11. The
 * device/swap-chain come from sf_d3d11 (dynamic D8 loading); the D3DCompile
 * call inside imgui_impl_dx11 is redirected to sf_d3d_compile_shim.
 *
 * Phases B2–B4: the preview is rendered through the *same* engine as the final
 * render (sf_preview), uploaded to an RGBA8 D3D11 texture and drawn with
 * ImGui::Image. Two view modes mirror the old native dialog (whole-image fit vs
 * a 1:1 centre crop that can be panned). The settings pane is the full 1:1 port
 * of the retired dialog.c (phase B3). Phase B4 adds the dark/light theme, the
 * accent OK button, keyboard navigation (Tab and the arrows move focus, Esc
 * cancels, and no key presses OK: dismissing the dialog takes a click on OK,
 * Cancel or the title-bar X), per-monitor DPI handling and the Windows 11 DWM
 * chrome (rounded corners + immersive title bar). DWM is reached through
 * LoadLibrary, so the shipping static import table stays within the standard
 * Win32 DLLs (decision D8).
 */
#include <windows.h>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "sf_d3d11.h"
#include "sf_gui.h"
#include "sf_preview.h"
#include "sp_color.h"
#include "spektra_core.h" /* sf_diffusion_widest_sigma_um, for the preview cost note */
#include "version.h"
#include "build_date.h"
#include "params.h"
#include "pack_list.h"
#include "preset.h"

/* g_sf_hinst is defined (C linkage) in plugin_main.c. Name it with extern "C" so
 * MSVC does not mangle the reference: GCC/Clang leave global-scope variable names
 * unmangled, but MSVC mangles C++ globals, which broke the MSVC link (LNK2019). */
extern "C" {
extern HINSTANCE g_sf_hinst;
}

static int g_done = 0;
static int g_result = 0;
static int g_dirty = 1;
static int g_need_resize = 0;
static int g_resize_w = 0;
static int g_resize_h = 0;

#define SF_TMR_PREVIEW 1
static HWND g_hwnd = NULL;
static int g_timer_on = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Phase B4 presentation state. `g_theme` is 0 = dark, 1 = light. `g_dpi` drives
 * the ImGui style scale and the per-monitor font scale factor; a WM_DPICHANGED
 * stashes the new value in `g_pending_dpi` to be applied between frames. The
 * theme button sets `g_style_dirty`; the modal loop applies the new style
 * between frames and forces one more render, so the whole UI (and the DWM
 * chrome) updates consistently. */
static UINT g_dpi = 96;
static int  g_theme = 0;
static int  g_style_dirty = 0;
static int  g_pending_dpi = 0;
static bool g_about_open = false; /* About modal state, shared with BeginPopupModal */
static int g_about_key_frame = -1; /* frame the About modal took Enter/Esc */

/* The UI font base size at 96 dpi (18 pt). DPI is applied as a per-monitor
 * scale factor (style.FontScaleDpi) rather than by reloading the atlas:
 * ImGui's dynamic font system (1.92+) rasterises glyphs at the effective
 * size, so text stays crisp at any DPI without touching the font atlas.
 * Introduced at 12 px and doubled in Phase 4 so the dialog reads at a normal
 * viewing distance; the settings grid and the window geometry are all
 * measured in ems of this font. */
#define SF_UI_FONT_BASE_PX 24.0f

/* The dialog's default and minimum client size, in em, so the window keeps
 * the same proportions at any DPI: Phase 4 doubled the font with DPI but
 * left the window at a fixed 1100x780 pixels, so a 150% display got a third
 * fewer characters per line than a 100% one. The minimum is what a settings
 * row needs (SF_ROW_MIN_EM, sf_grid_measure) plus the header, the footer and
 * the pane chrome; the default is that split with room to spare. */
#define SF_WIN_DEF_EM_X 50.0f
#define SF_WIN_DEF_EM_Y 33.0f
#define SF_WIN_MIN_EM_X 33.0f
#define SF_WIN_MIN_EM_Y 23.0f

/* One em in pixels at a given DPI: what the font, and everything measured in
 * em, actually costs on screen there. */
static float sf_em_px(UINT dpi)
{
  return SF_UI_FONT_BASE_PX * (float)dpi / 96.0f;
}

static void sf_imgui_apply_style(UINT dpi);
static void sf_imgui_load_font(void);
static void sf_imgui_dwm_chrome(HWND hwnd, int light);
/* Defined with the halation rows further down; the preview and accept paths
 * below need them first. */
static double sf_halation_cap_um(const sf_params_block *b);
static int sf_halation_reconcile(sf_params_block *b);

/* ------------------------------------------------------------------------- */
/* dialog state                                                               */
/* ------------------------------------------------------------------------- */

struct sf_imgui_ctx
{
  sf_d3d11 *d3d;
  sf_tex *tex;
  sf_params_block *out; /* committed on OK */
  sf_params_block work; /* live copy the widgets edit */
  const float *src;
  int sw, sh;

  FilterRecordPtr fr;      /* for document-profile auto-detect */
  sf_detect_result detect; /* resolved once at open */

  int view_fit; /* 1 = whole image, 0 = 1:1 crop */

  int preview_dirty;
  unsigned long long preview_at; /* GetTickCount64() debounce deadline */
  int preview_hold;              /* a slider was held: keep waiting */
  int tex_w, tex_h;              /* current texture size */
  int shown_w, shown_h;          /* size the texture was rendered for */

  int pan_drag;
  ImVec2 pan_mouse0;
  double pan_base_x, pan_base_y;

  int hal_profile; /* last halation-profile preset applied */

  char err[160];
  int has_err;
  char ok_err[160];
  int has_ok_err;
};

static sf_imgui_ctx g_ctx;

/* A change arms a deadline and the preview renders when it expires, so a stream
 * of changes renders once, at the end of the stream. How long the end is
 * depends on where the change came from: a committed text field, a dropdown or
 * a reset is a single edit and wants the shortest wait that still coalesces a
 * burst; a mouse-wheel notch is one of a series the user rolls at will, so the
 * wait has to outlast the rolling; a slider held under the mouse button is one
 * edit with pauses in it, and a pause is not a value worth rendering, so the
 * deadline is pushed out for as long as the button stays down. */
#define SF_PREVIEW_DEBOUNCE_MS 120
#define SF_WHEEL_SETTLE_MS 700
#define SF_SLIDER_HOLD_MS 100

/* How a slider moved in the current frame, which the tail of the settings pane
 * reads to choose how long the preview waits. Both are cleared at the start of
 * every frame: g_slider_held is set by a slider under the mouse button,
 * g_wheel_notch by a wheel notch that moved one. */
static int g_slider_held;
static int g_wheel_notch;

static void sf_preview_mark_dirty(unsigned int delay_ms)
{
  g_ctx.preview_dirty = 1;
  g_ctx.preview_at = GetTickCount64() + (unsigned long long)delay_ms;
  /* A debounce delay needs a wake-up: the modal loop blocks in WaitMessage()
   * once the input stops. (delay 0 renders in the same frame — no timer.) */
  if(delay_ms > 0 && !g_timer_on && g_hwnd)
  {
    SetTimer(g_hwnd, SF_TMR_PREVIEW, 30, NULL);
    g_timer_on = 1;
  }
}

/* Render the current *work* block into the preview texture at w x h (physical
 * pixels). Falls back to the built-in chart when no host pixels are available.
 * Never touches the host frame. */
static void sf_imgui_render_preview(sf_imgui_ctx *c, int w, int h)
{
  unsigned char *rgb = NULL;
  unsigned char *rgba;
  char *err = NULL;
  int i, n;
  sf_params_block b;

  if(w < 8) w = 8;
  if(h < 8) h = 8;

  c->has_err = 0;
  c->err[0] = '\0';

  b = c->work;
  /* A first-bounce radius above the ceiling renders at the ceiling, so fold it
   * here rather than letting validate() refuse the block: that would stop the
   * preview updating with no explanation while the user drags the ceiling
   * slider. The rows say what is clamped; the fields keep what was typed. */
  sf_halation_reconcile(&b);
  if(sf_params_validate(&b) != 0)
  {
    /* keep the last good frame while the user is mid-edit / out of range */
    return;
  }

  if(c->src && c->sw > 0 && c->sh > 0)
  {
    if(c->view_fit)
      rgb = sf_preview_render_fit(c->src, c->sw, c->sh, &b, w, h, &err);
    else
      rgb = sf_preview_render_image(c->src, c->sw, c->sh, &b, w, h, &err);
    if(!rgb && err) { free(err); err = NULL; }
  }
  if(!rgb)
    rgb = sf_preview_render(&b, w, h, &err);
  if(err) free(err);
  if(!rgb)
  {
    c->has_err = 1;
    strncpy(c->err, "preview: render failed", sizeof(c->err) - 1);
    c->err[sizeof(c->err) - 1] = '\0';
    return;
  }

  /* Expand RGB -> RGBA for the GPU texture. */
  n = w * h;
  rgba = (unsigned char *)malloc((size_t)n * 4u);
  if(!rgba)
  {
    free(rgb);
    c->has_err = 1;
    strncpy(c->err, "preview: out of memory", sizeof(c->err) - 1);
    c->err[sizeof(c->err) - 1] = '\0';
    return;
  }
  for(i = 0; i < n; i++)
  {
    rgba[i * 4 + 0] = rgb[i * 3 + 0];
    rgba[i * 4 + 1] = rgb[i * 3 + 1];
    rgba[i * 4 + 2] = rgb[i * 3 + 2];
    rgba[i * 4 + 3] = 255;
  }
  free(rgb);

  if(sf_d3d11_tex_update(c->tex, w, h, rgba))
  {
    c->tex_w = w;
    c->tex_h = h;
  }
  else
  {
    c->has_err = 1;
    strncpy(c->err, "preview: texture upload failed", sizeof(c->err) - 1);
    c->err[sizeof(c->err) - 1] = '\0';
  }
  free(rgba);
}

/* The preview pane: view-mode radios, the image, and 1:1 pan dragging. Renders
 * into the preview texture at the pane's physical pixel size (debounced). */
static void sf_imgui_preview_pane(sf_imgui_ctx *c)
{
  ImGuiIO &io = ImGui::GetIO();
  int w, h;
  ImTextureID id;

  ImGui::TextUnformatted("Preview");
  if(c->preview_dirty)
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(updating...)");
  }
  ImGui::SameLine();
  if(ImGui::RadioButton("Whole image", c->view_fit == 1))
  {
    if(c->view_fit != 1) { c->view_fit = 1; sf_preview_mark_dirty(0); }
  }
  ImGui::SameLine();
  if(ImGui::RadioButton("1:1 crop", c->view_fit == 0))
  {
    if(c->view_fit != 0) { c->view_fit = 0; sf_preview_mark_dirty(0); }
  }

  {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float sx = io.DisplayFramebufferScale.x;
    float sy = io.DisplayFramebufferScale.y;
    if(sx <= 0.0f) sx = 1.0f;
    if(sy <= 0.0f) sy = 1.0f;

    w = (int)(avail.x * sx + 0.5f);
    h = (int)(avail.y * sy + 0.5f);
    if(w < 8) w = 8;
    if(h < 8) h = 8;

    /* Re-render when the pane size changed (immediate), else on the debounce. */
    if(c->tex_w != w || c->tex_h != h)
      sf_preview_mark_dirty(0);

    /* A slider under the button holds the preview: the deadline is renewed here
     * for as long as it is held, so a drag that pauses on the way renders
     * nothing. preview_hold is the settings pane's answer from the previous
     * frame - this pane is built first - so the frame the button comes up in
     * still waits and the one after it renders. Renewing it here rather than
     * only where the change was seen matters when a frame runs late: the
     * wake-up timer retires itself once the deadline passes, so a render
     * blocked here must not be left with nothing to fire. */
    if(c->preview_dirty && !c->preview_hold &&
       GetTickCount64() >= c->preview_at)
    {
      sf_imgui_render_preview(c, w, h);
      c->preview_dirty = 0;
    }
    else if(c->preview_dirty && c->preview_hold)
      sf_preview_mark_dirty(SF_SLIDER_HOLD_MS);

    id = (ImTextureID)(intptr_t)sf_d3d11_tex_srv(c->tex);

    if(id && c->tex_w > 0 && c->tex_h > 0)
    {
      ImGui::Image((ImTextureRef)id, ImVec2(avail.x, avail.y));

      if(!c->view_fit)
      {
        if(ImGui::IsItemHovered())
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
          c->pan_drag = 1;
          c->pan_mouse0 = io.MousePos;
          c->pan_base_x = sf_preview_pan_x();
          c->pan_base_y = sf_preview_pan_y();
        }
      }

      if(c->pan_drag)
      {
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          /* 1:1 grab: the crop window must follow the cursor exactly. The pane
           * shows cw x ch source pixels across avail.x x avail.y logical
           * pixels, so the cursor delta in source px is delta * cw/avail.x.
           * sf_preview maps pan [-1,1] onto the crop slack (sw-cw), i.e. a
           * slope of (sw-cw)/2 source px per pan unit; dividing by that turns
           * the source-pixel move into a pan delta. (Using the pane fraction
           * directly, as this once did, moves the crop by slack/2 per pane
           * width instead of cw - the "image moves twice as far" bug.) */
          double cw = (c->sw < w) ? (double)c->sw : (double)w;
          double ch = (c->sh < h) ? (double)c->sh : (double)h;
          double slack_x = (double)c->sw - cw;
          double slack_y = (double)c->sh - ch;
          double dx = (double)(io.MousePos.x - c->pan_mouse0.x);
          double dy = (double)(io.MousePos.y - c->pan_mouse0.y);
          double nx = c->pan_base_x;
          double ny = c->pan_base_y;
          if(slack_x > 0.0 && avail.x > 0.0f)
            nx -= dx * (cw / (double)avail.x) * 2.0 / slack_x;
          if(slack_y > 0.0 && avail.y > 0.0f)
            ny -= dy * (ch / (double)avail.y) * 2.0 / slack_y;
          sf_preview_set_pan(nx, ny);
          sf_preview_mark_dirty(SF_PREVIEW_DEBOUNCE_MS);
        }
        else
        {
          c->pan_drag = 0;
        }
      }
    }
    else
    {
      ImGui::TextUnformatted(c->has_err ? c->err : "No image available.");
    }
  }
}

/* --------------------------------------------------------------- controls -- */
/* Phase B3: the full 1:1 port of the (now retired) native dialog's control set,
 * laid out in collapsible groups and mapping one-for-one onto sf_params_block.
 *
 * Every numeric control is a slider with an editable buddy input box; a middle
 * or right click on the slider resets it to the engine's neutral default (the
 * value sf_params_init / sp_params_defaults produces), so the reset restores a
 * byte-identical no-op. Ranges are the upstream OFX parameter ranges, widened
 * only where the .8bf's own sentinel/default lies outside them (e.g. the -1
 * "use the stock's own" halation/diffusion fields); the film/print exposure
 * ranges follow the .8bf's own validated surface. Group order mirrors the OFX
 * defineGroup() order so the two UIs read the same. */

static const char *const SF_PROCESS_MODE_LABELS[] = {
  "Print simulation", "Scan negative", "Process negative"
};
#define SF_PROCESS_MODE_COUNT 3

static const char *const SF_PRINT_TIMING_LABELS[] = {
  "Filtered enlarger", "APD printer density"
};
#define SF_PRINT_TIMING_COUNT 2

/* Diffusion families, index-aligned with sp_diffusion_family_t (sp_chain.h):
 * 0 Glimmerglass, 1 Black Pro-Mist, 2 Pro-Mist, 3 CineBloom. */
static const char *const SF_DIFF_LABELS[] = {
  "Glimmerglass", "Black Pro-Mist", "Pro-Mist", "CineBloom"
};
#define SF_DIFF_COUNT 4

static const char *const SF_GRAIN_MODEL_LABELS[] = { "Preview", "Production" };
#define SF_GRAIN_MODEL_COUNT 2

static const char *const SF_PUSH_PULL_MODE_LABELS[] = { "Standard", "Experimental" };
#define SF_PUSH_PULL_MODE_COUNT 2

static const char *const SF_AUTO_EXPOSURE_METHOD_LABELS[] = { "Centre-weighted", "Median" };
#define SF_AUTO_EXPOSURE_METHOD_COUNT 2

/* RGB -> raw, index-aligned with the .8bf enum (0 Hanatos2025, 1 Mallett2019,
 * 2 Hanatos2026); 2 is the reference default. */
static const char *const SF_RGB_TO_RAW_METHOD_LABELS[] = {
  "Hanatos 2025", "Mallett 2019", "Hanatos 2026"
};
#define SF_RGB_TO_RAW_METHOD_COUNT 3

static const char *const SF_HALATION_PROFILE_LABELS[] = {
  "Stock (film's own)", "Off / none", "Still-film (65 um)",
  "Cine / tungsten (50 um)", "Double-X (50 um, balanced)",
  "Strong red-remjet (65 um)", "Custom"
};
#define SF_HALATION_PROFILE_COUNT 7

/* Settings-row geometry, in em, so a row holds its text at any font size or
 * DPI (the UI font scales with DPI, and these columns used to be fixed pixels
 * sized for the 12 px font of the first ImGui dialog):
 *
 *   label   the longest of the 140 settings labels is 10.6 em in a wide font
 *           ("Auto-detect input profile"; 9.6 em in Liberation Sans, and Segoe
 *           UI, which ships, is narrower still)
 *   value   measured from the widest value any row shows, "-1,0000" (numbers
 *           carry a comma, see sf_num_txt), in the current font -- 3.3 em of
 *           glyphs in a wide font, plus the frame's padding and border, which
 *           is why this is a measurement and not a constant: at the 12 px
 *           fallback font the padding alone is 1.5 em
 *   slider  below 4.5 em there is nothing to drag
 *
 * SF_ROW_MIN_EM is what a row needs, from those parts plus the two gaps between
 * the columns; sf_imgui_build() gives the settings pane that much before the
 * preview pane is asked to give up width. */
#define SF_LAB_MIN_EM 11.0f
#define SF_LAB_MAX_EM 12.0f
#define SF_NUM_MIN_EM 4.0f /* value-column floor; the measurement is usually more */
#define SF_NUM_WID_EM 4.9f /* what the measurement can grow to, for the row budget */
#define SF_SL_MIN_EM  4.5f
#define SF_ROW_MIN_EM (SF_LAB_MIN_EM + SF_NUM_WID_EM + SF_SL_MIN_EM + 1.0f)

static float g_lab_w = 140.0f, g_sl_w = 150.0f, g_num_w = 62.0f;

static void sf_grid_measure(void)
{
  ImGuiStyle &st = ImGui::GetStyle();
  const float em = ImGui::GetFontSize();
  float avail = ImGui::GetContentRegionAvail().x;
  if(avail < em * SF_ROW_MIN_EM) avail = em * SF_ROW_MIN_EM;
  g_lab_w = avail * 0.42f;
  if(g_lab_w < em * SF_LAB_MIN_EM) g_lab_w = em * SF_LAB_MIN_EM;
  if(g_lab_w > em * SF_LAB_MAX_EM) g_lab_w = em * SF_LAB_MAX_EM;
  g_num_w = ImGui::CalcTextSize("-1,0000").x + st.FramePadding.x * 2.0f +
            st.FrameBorderSize * 2.0f;
  if(g_num_w < em * SF_NUM_MIN_EM) g_num_w = em * SF_NUM_MIN_EM;
  g_sl_w = avail - g_lab_w - g_num_w - st.ItemSpacing.x * 2.0f - 4.0f;
  if(g_sl_w < em * SF_SL_MIN_EM) g_sl_w = em * SF_SL_MIN_EM;
}

static void sf_lab(const char *label)
{
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(g_lab_w);
}

static void sf_tip(const char *tip)
{
  if(tip && tip[0] && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tip);
}

static int sf_reset_clicked(void)
{
  return ImGui::IsItemHovered() &&
         (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
          ImGui::IsMouseClicked(ImGuiMouseButton_Right));
}

static void sf_clamp_f(float *v, float lo, float hi)
{
  if(*v < lo) *v = lo;
  if(*v > hi) *v = hi;
}

/* Mouse-wheel adjustment for the slider that was just submitted. Must be called
 * with the slider as the last item. Claiming ImGuiKey_MouseWheelY (even while
 * the wheel is idle) stops the settings pane from scrolling whenever the cursor
 * hovers a slider, so the pane only scrolls when the wheel is over empty space.
 * One notch moves by 1% of the range (Shift = 0.1%). Returns non-zero when the
 * value changed, and notes that the change came from the wheel: a rolling wheel
 * is a series of notches with pauses between them, and the preview waits for
 * the series instead of rendering every notch. */
static int sf_slider_wheel_f(float *v, float lo, float hi)
{
  ImGuiIO &io = ImGui::GetIO();
  float step, nv;
  if(!ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)) return 0;
  if(io.MouseWheel == 0.0f) return 0;
  step = (hi - lo) * (io.KeyShift ? 0.001f : 0.01f);
  if(step <= 0.0f) return 0;
  nv = *v + step * io.MouseWheel;
  if(nv < lo) nv = lo;
  if(nv > hi) nv = hi;
  if(nv == *v) return 0;
  *v = nv;
  g_wheel_notch = 1;
  return 1;
}

/* Integer counterpart of sf_slider_wheel_f: one notch moves by at least one
 * unit (or 1% of the range for wide ranges). */
static int sf_slider_wheel_i(int *v, int lo, int hi)
{
  ImGuiIO &io = ImGui::GetIO();
  int step, nv, dir;
  if(!ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)) return 0;
  if(io.MouseWheel == 0.0f) return 0;
  step = (hi - lo) / 100;
  if(step < 1) step = 1;
  dir = (io.MouseWheel > 0.0f) ? 1 : -1;
  nv = *v + dir * step;
  if(nv < lo) nv = lo;
  if(nv > hi) nv = hi;
  if(nv == *v) return 0;
  *v = nv;
  g_wheel_notch = 1;
  return 1;
}

/* European number formatting: every number the GUI shows takes ',' as the
 * decimal separator, and the number fields accept one when typed.
 *
 * Dear ImGui formats slider and input values with ImFormatString() - printf(),
 * so always '.' - and reads a number field with sscanf("%f"), which only
 * understands '.'. Its locale hook, ImGuiPlatformIO::Platform_LocaleDecimalPoint
 * (set to ',' where the context is created here), only rewrites the input text
 * filter: a typed '.' or ',' becomes the configured separator, so the keyboard
 * side is covered by that one line and the rest is done by the three helpers
 * below. sf_num_txt() formats a number with a comma, sf_slider_text() draws the
 * value a slider would have drawn, and sf_numf() is a comma-aware InputFloat. */

/* printf() into out, then rewrite the decimal point as a comma. The value is
 * passed as a double, so fmt must be a floating-point format ("%.2f"). */
static void sf_num_txt(char *out, size_t out_size, const char *fmt, double v)
{
  char *p;
  snprintf(out, out_size, fmt, v);
  for(p = out; *p != '\0'; p++)
    if(*p == '.') *p = ',';
}

/* The value text of the slider that was just submitted, with a comma. ImGui
 * centres it in the frame, so this reproduces what it draws itself. */
static void sf_slider_text(const char *fmt, float v)
{
  char txt[32];
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const ImVec4 clip(a.x, a.y, b.x, b.y);
  ImVec2 pos, size;
  sf_num_txt(txt, sizeof txt, fmt, (double)v);
  size = ImGui::CalcTextSize(txt);
  pos.x = a.x + (b.x - a.x - size.x) * 0.5f;
  pos.y = a.y + (b.y - a.y - size.y) * 0.5f;
  ImGui::GetWindowDrawList()->AddText(NULL, 0.0f, pos,
                                      ImGui::GetColorU32(ImGuiCol_Text),
                                      txt, NULL, 0.0f, &clip);
}

/* Notes that the slider just submitted is under the mouse button. A slider
 * keeps its active id for as long as the button is down, even after the pointer
 * has drifted off its frame, so this reports the button rather than the pointer
 * - which is what "until the button is let go" asks for. Call it straight after
 * the widget: drawing the value text above submits no item, so the item is
 * still the slider. */
static void sf_slider_hold(void)
{
  if(ImGui::IsItemActive())
    g_slider_held = 1;
}

/* SliderFloat with its value drawn in European format. The built-in value text
 * is hidden by making the text colour transparent for the call - a slider draws
 * nothing else with that colour, and ImGui renders the value through
 * RenderTextClippedEx(), which takes GetColorU32(ImGuiCol_Text) - and the comma
 * form is redrawn on top. The original format string still goes to SliderFloat:
 * it sets the drag step and the rounding to the displayed precision, so those
 * keep working. */
static int sf_slider_f(const char *label, float *v, float lo, float hi,
                       const char *fmt)
{
  int changed;
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
  changed = ImGui::SliderFloat(label, v, lo, hi, fmt) ? 1 : 0;
  ImGui::PopStyleColor();
  sf_slider_hold();
  sf_slider_text(fmt, *v);
  return changed;
}

/* InputFloat that shows and accepts a comma. The value is staged in a buffer
 * of its own, re-seeded from *v whenever anything else moved the value (the
 * slider, a preset, a reset) and written back once the edit is committed -
 * with the comma turned back into a dot for strtod().
 *
 * "Committed" is the point of the LiveEditOnInputText push below. That flag is
 * on by default, and with it on ImGui copies the text to the buffer and returns
 * non-zero on every keystroke, which would re-render the preview as fast as the
 * user types. With it off, InputText returns non-zero only when the edit is
 * finished: Enter, Tab, or losing focus to a click anywhere else. That is also
 * when it copies the edited text into the buffer, so the parse below sees a
 * whole number rather than a half-typed one. While the field is being edited
 * ImGui keeps its own copy of the text, so re-seeding the buffer here cannot
 * disturb a half-typed number. The field clamps to the row's range, as the
 * slider does. */
static int sf_numf(const char *label, float *v, const char *fmt,
                   float lo, float hi)
{
  static char txt[64];
  static float txt_v = 0.0f;
  static ImGuiID txt_id = 0;
  const ImGuiInputTextFlags flags =
      (ImGuiInputTextFlags)(ImGuiInputTextFlags_CharsDecimal |
                            ImGuiInputTextFlags_AutoSelectAll);
  char fix[64];
  char *end;
  ImGuiID id = ImGui::GetID(label);
  size_t i;
  float f;
  int ch;

  if(id != txt_id || *v != txt_v)
  {
    sf_num_txt(txt, sizeof txt, fmt, (double)*v);
    txt_id = id;
    txt_v = *v;
  }
  ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputText, false);
  ch = ImGui::InputText(label, txt, sizeof txt, flags) ? 1 : 0;
  ImGui::PopItemFlag();
  if(!ch) return 0;

  for(i = 0; i + 1 < sizeof fix && txt[i] != '\0'; i++)
    fix[i] = (txt[i] == ',') ? '.' : txt[i];
  fix[i] = '\0';
  f = (float)strtod(fix, &end);
  if(end == fix) /* nothing readable: put the stored value back on screen */
  {
    sf_num_txt(txt, sizeof txt, fmt, (double)*v);
    return 0;
  }
  if(f < lo) f = lo;
  if(f > hi) f = hi;
  ch = (f != *v);
  *v = f;
  txt_v = f;
  sf_num_txt(txt, sizeof txt, fmt, (double)f); /* show all its decimals */
  return ch;
}

/* float slider + buddy input box. */
static void sf_row_f(const char *label, float *v, float lo, float hi, float def,
                     const char *fmt, const char *tip, int *changed)
{
  ImGui::PushID(label);
  sf_lab(label);
  ImGui::SetNextItemWidth(g_sl_w);
  if(sf_slider_f("##s", v, lo, hi, fmt)) *changed = 1;
  if(sf_slider_wheel_f(v, lo, hi)) *changed = 1;
  if(sf_reset_clicked()) { *v = def; *changed = 1; }
  sf_tip(tip);
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::SetNextItemWidth(g_num_w);
  if(sf_numf("##n", v, fmt, lo, hi)) *changed = 1;
  sf_tip(tip);
  sf_clamp_f(v, lo, hi);
  ImGui::PopID();
}

/* double field through a float temp (no type punning); writes back only on a
 * committed change. */
static void sf_row_d(const char *label, double *v, float lo, float hi, float def,
                     const char *fmt, const char *tip, int *changed)
{
  float f = (float)*v;
  int ch = 0;
  ImGui::PushID(label);
  sf_lab(label);
  ImGui::SetNextItemWidth(g_sl_w);
  if(sf_slider_f("##s", &f, lo, hi, fmt)) ch = 1;
  if(sf_slider_wheel_f(&f, lo, hi)) ch = 1;
  if(sf_reset_clicked()) { f = def; ch = 1; }
  sf_tip(tip);
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::SetNextItemWidth(g_num_w);
  if(sf_numf("##n", &f, fmt, lo, hi)) ch = 1;
  sf_tip(tip);
  ImGui::PopID();
  sf_clamp_f(&f, lo, hi);
  if(ch) { *v = (double)f; *changed = 1; }
}

static void sf_row_i(const char *label, int *v, int lo, int hi, int def,
                     const char *tip, int *changed)
{
  int ch;

  ImGui::PushID(label);
  sf_lab(label);
  ImGui::SetNextItemWidth(g_sl_w);
  if(ImGui::SliderInt("##s", v, lo, hi, "%d")) *changed = 1;
  sf_slider_hold();
  if(sf_slider_wheel_i(v, lo, hi)) *changed = 1;
  if(sf_reset_clicked()) { *v = def; *changed = 1; }
  sf_tip(tip);
  ImGui::SameLine(0.0f, 4.0f);
  ImGui::SetNextItemWidth(g_num_w);
  /* The same deferred commit as sf_numf(). InputScalar already holds the value
   * back until the field is deactivated, so this row was not re-rendering the
   * preview on every keystroke; the flag below defers the text inside it the
   * same way, so both kinds of number box follow one rule rather than the
   * accident of which widget happens to be underneath. */
  ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputText, false);
  ch = ImGui::InputInt("##n", v, 0, 0) ? 1 : 0;
  ImGui::PopItemFlag();
  if(ch) *changed = 1;
  sf_tip(tip);
  if(*v < lo) *v = lo;
  if(*v > hi) *v = hi;
  ImGui::PopID();
}

static void sf_row_combo(const char *label, int *v, const char *const *items,
                         int n, const char *tip, int *changed)
{
  const char *cur = (*v >= 0 && *v < n) ? items[*v] : "?";
  ImGui::PushID(label);
  sf_lab(label);
  ImGui::SetNextItemWidth(g_sl_w + g_num_w + 4.0f);
  if(ImGui::BeginCombo("##c", cur))
  {
    for(int i = 0; i < n; i++)
    {
      const int sel = (*v == i);
      if(ImGui::Selectable(items[i], sel)) { *v = i; *changed = 1; }
      if(sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  sf_tip(tip);
}

/* A combo whose entries are produced by an engine label function (colour
 * spaces, transfer curves, film formats). */
static void sf_row_combo_dyn(const char *label, int *v, int n,
                             const char *(*name)(int), const char *tip, int *changed)
{
  const char *items[40];
  int i;
  if(n > 40) n = 40;
  for(i = 0; i < n; i++) items[i] = name(i);
  sf_row_combo(label, v, items, n, tip, changed);
}

static void sf_row_check(const char *label, int *v, const char *tip, int *changed)
{
  bool b = (*v != 0);
  ImGui::PushID(label);
  if(ImGui::Checkbox(label, &b)) { *v = b ? 1 : 0; *changed = 1; }
  ImGui::PopID();
  sf_tip(tip);
}

/* A read-only text box (used for the auto-detected document profile name). */
static void sf_row_readonly(const char *label, const char *text)
{
  char buf[160];
  strncpy(buf, text ? text : "", sizeof buf - 1);
  buf[sizeof buf - 1] = '\0';
  ImGui::PushID(label);
  sf_lab(label);
  ImGui::SetNextItemWidth(g_sl_w + g_num_w + 4.0f);
  ImGui::InputText("##ro", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly);
  ImGui::PopID();
}

/* Halation profile presets: the film's own weights/first-sigma (all sentinels)
 * down to the strong remjet preset. Mirrors the retired dialog.c's sf_apply_halation_profile. */
static void sf_apply_halation_profile(sf_params_block *b, int idx)
{
  switch(idx)
  {
    case 0: /* Stock: the film's own values. */
      b->halation_strength[0] = b->halation_strength[1] = b->halation_strength[2] = -1.0f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = -1.0;
      break;
    case 1: /* Off. */
      b->halation_strength[0] = b->halation_strength[1] = b->halation_strength[2] = 0.0f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = -1.0;
      break;
    case 2: /* Still film, 65 um. */
      b->halation_strength[0] = 0.015f; b->halation_strength[1] = 0.005f;
      b->halation_strength[2] = 0.0f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = 65.0;
      break;
    case 3: /* Cine / tungsten, 50 um. */
      b->halation_strength[0] = 0.015f; b->halation_strength[1] = 0.005f;
      b->halation_strength[2] = 0.0f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = 50.0;
      break;
    case 4: /* Double-X (balanced, 50 um). */
      b->halation_strength[0] = b->halation_strength[1] = b->halation_strength[2] = 0.015f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = 50.0;
      break;
    case 5: /* Strong remjet (65 um). */
      b->halation_strength[0] = 0.08f; b->halation_strength[1] = 0.02f;
      b->halation_strength[2] = 0.0f;
      b->halation_first_sigma_um[0] = b->halation_first_sigma_um[1] =
          b->halation_first_sigma_um[2] = 65.0;
      break;
    case 6: /* Custom - leave values as-is. */
    default: break;
  }
}

/* The ceiling the per-channel first-bounce radii are held to: the user's own
 * "Sigma max um" while it carries a radius, the stock ceiling while it carries
 * its -1 sentinel. One expression for the three places that need it — the
 * slider's end, sf_params_validate()'s rejection and the chain's clamp, which
 * all agree through SP_HALATION_FIRST_SIGMA_UM. */
static double sf_halation_cap_um(const sf_params_block *b)
{
  const double mx = b->halation_sigma_max_um;
  const double cap = (mx >= 0.0) ? mx : SP_HALATION_FIRST_SIGMA_UM;
  return (cap > 0.0) ? cap : SP_HALATION_FIRST_SIGMA_UM;
}

/* Fold any first-bounce radius above that ceiling down to it; non-zero if
 * anything moved.
 *
 * This is not what enforces the ceiling — the chain clamps the radii too — it
 * is what makes the block that gets previewed, and the block OK hands the host,
 * describe what is actually rendered. So it runs on the preview copy and on
 * accept, never on the block being edited: folding the radii in place would
 * quietly destroy a per-channel radius the moment the user nudged the ceiling
 * below it, and raising the ceiling back would not bring it back. The settings
 * pane shows a note over the rows instead. */
static int sf_halation_reconcile(sf_params_block *b)
{
  const double cap = sf_halation_cap_um(b);
  int moved = 0;
  int k;
  for(k = 0; k < 3; k++)
    if(b->halation_first_sigma_um[k] >= 0.0 && b->halation_first_sigma_um[k] > cap)
    {
      b->halation_first_sigma_um[k] = cap;
      moved = 1;
    }
  return moved;
}

/* The pitch the render will actually use, in micrometres: a film format's own
 * (gauge over the document's long edge) when one is chosen, the manual field
 * otherwise. Mirrors sp_chain_create()'s resolution so a width the pane works
 * out is the width the stage gets; with no document behind it, the chain's own
 * fallback. */
static double sf_effective_pixel_um(const sf_params_block *b, int sw, int sh)
{
  const long le = (sw > sh) ? sw : sh;
  const double um = sp_pixel_um_for_format(sp_film_format_mm((int)b->film_format),
                                           (double)le);
  if(um > 0.0) return um;
  return (b->pixel_um > 0.0) ? b->pixel_um : 4.0;
}

/* Is a blur this wide enough that previewing it stops feeling instant? */
static int sf_sigma_is_slow(double sigma_px)
{
  return sigma_px > SF_PREVIEW_SLOW_SIGMA_PX;
}

/* The standing note: one grey line, drawn only while `sigma_px` is past the
 * threshold, saying which stage it is and what it will cost you.
 *
 * It is a consequence, not a measurement: the dialog cannot time a preview it
 * has not run, and the render blocks the frame it is drawn in, so a duration
 * shown during the wait would be unreadable and one guessed before it would be
 * a number with nothing behind it. What is known exactly is the radius the
 * stage will ask for, and the cost follows that as a square.
 *
 * Call it at the END of a section. The line takes no space when it is not
 * drawn, so a row appearing under the slider being dragged would move that row
 * out from under the cursor; at the section's end, everything above it -- the
 * rows you are editing -- stays where it is. */
static void sf_slow_preview_note(double sigma_px, const char *what)
{
  if(sf_sigma_is_slow(sigma_px))
    ImGui::TextDisabled("Wide %s makes the preview slow to update.", what);
}

/* Map an RGB pick onto the per-channel return weights, keeping the current peak
 * weight (or a 0.05 default when the weights are the -1 sentinels). */
static void sf_apply_halation_colour(sf_params_block *b, float r, float g, float bl)
{
  double maxw;
  double rr = b->halation_strength[0], gg = b->halation_strength[1],
         bb = b->halation_strength[2];
  if(rr < 0.0 && gg < 0.0 && bb < 0.0) maxw = 0.05;
  else
  {
    maxw = (rr > gg) ? rr : gg;
    if(bb > maxw) maxw = bb;
    if(!(maxw > 0.0)) maxw = 0.05;
  }
  b->halation_strength[0] = (float)((double)r * maxw);
  b->halation_strength[1] = (float)((double)g * maxw);
  b->halation_strength[2] = (float)((double)bl * maxw);
}

/* The settings pane: the full parameter surface in collapsible groups. */
/* ------------------------------------------------------------- presets -- */
/* Saved looks live in %APPDATA%\spektrafilm\presets\<name>.json. The JSON
 * itself comes from preset.c, which the Linux harness exercises; only the
 * disk and clipboard live here. Everything reached is KERNEL32 (environment,
 * directories, files, FindFirstFile, clipboard memory) or USER32 (clipboard),
 * so the shipping import table stays inside the allow-list (decision D8). */

#define SF_PRESET_MAX  128
#define SF_PRESET_NAME 64

static char g_preset_dir[MAX_PATH];
static int  g_preset_dir_ok = -1;   /* -1 = not tried, 0 = unusable, 1 = ready */
static char g_preset_names[SF_PRESET_MAX][SF_PRESET_NAME];
static int  g_preset_n;
static int  g_preset_sel = -1;
static char g_preset_edit[SF_PRESET_NAME];
static char g_preset_msg[160];
static int  g_preset_msg_err;
static int  g_preset_list_dirty = 1;

/* Resolve (and create) the preset directory once. An unusable %APPDATA% — a
 * stripped environment, or a read-only profile — is reported per action
 * rather than taken on faith here. */
static int sf_preset_dir(void)
{
  char base[MAX_PATH];
  char parent[MAX_PATH];
  DWORD n;
  size_t len;

  if(g_preset_dir_ok >= 0) return g_preset_dir_ok;
  g_preset_dir_ok = 0;

  n = GetEnvironmentVariableA("APPDATA", base, MAX_PATH);
  if(n == 0 || n >= MAX_PATH) return 0;

  len = (size_t)snprintf(g_preset_dir, sizeof g_preset_dir,
                         "%s\\spektrafilm\\presets", base);
  if(len == 0 || len >= sizeof g_preset_dir) return 0;

  /* CreateDirectoryA fails when a level is missing, so make the parent first;
   * an existing directory is not an error. */
  if(snprintf(parent, sizeof parent, "%s\\spektrafilm", base) > 0)
    CreateDirectoryA(parent, NULL);
  if(!CreateDirectoryA(g_preset_dir, NULL) &&
     GetLastError() != ERROR_ALREADY_EXISTS)
    return 0;

  g_preset_dir_ok = 1;
  return 1;
}

/* A preset name becomes a filename, so keep it to characters that are legal
 * and readable on Windows, refuse the reserved device names, and never let a
 * path separator escape the directory. Load and Delete sanitise too, which
 * makes "../.." a no-op instead of a traversal. */
static void sf_preset_sanitize(char *s, size_t cap)
{
  static const char *const reserved[] =
  {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
  };
  size_t i, o = 0, r;

  if(cap == 0) return;

  for(i = 0; s[i] && o + 1 < cap; i++)
  {
    const char c = s[i];
    const int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                   c == '_' || c == '(' || c == ')';
    s[o++] = ok ? c : '_';
  }
  s[o] = 0;

  /* A trailing dot or space is invisible in Explorer and stripped by the
   * filesystem, which would let the name and the file disagree. */
  while(o > 0 && (s[o - 1] == ' ' || s[o - 1] == '.')) s[--o] = 0;
  i = 0;
  while(s[i] == ' ') i++;
  if(i > 0)
  {
    memmove(s, s + i, o - i + 1);
    o -= i;
  }
  if(o == 0) snprintf(s, cap, "preset");

  /* CON, prn, Com9 ... are reserved whatever the extension. */
  for(r = 0; r < sizeof reserved / sizeof reserved[0]; r++)
  {
    char head[SF_PRESET_NAME];
    size_t h = 0;
    while(h + 1 < sizeof head && s[h] && s[h] != '.')
    {
      char c = s[h];
      if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
      head[h++] = c;
    }
    head[h] = 0;
    if(strcmp(head, reserved[r]) == 0)
    {
      memmove(s + 1, s, o + 1);
      s[0] = '_';
      return;
    }
  }
}

static void sf_preset_path(char *dst, size_t cap, const char *name)
{
  snprintf(dst, cap, "%s\\%s.json", g_preset_dir, name);
}

static int sf_preset_cmp(const void *a, const void *b)
{
  return strcmp((const char *)a, (const char *)b);
}

static void sf_preset_refresh(void)
{
  WIN32_FIND_DATAA fd;
  HANDLE h;
  char pat[MAX_PATH];

  g_preset_n = 0;
  g_preset_sel = -1;
  if(!sf_preset_dir()) return;

  snprintf(pat, sizeof pat, "%s\\*.json", g_preset_dir);
  h = FindFirstFileA(pat, &fd);
  if(h == INVALID_HANDLE_VALUE) return;

  do
  {
    size_t n;
    if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    n = strlen(fd.cFileName);
    if(n > 5 && _stricmp(fd.cFileName + n - 5, ".json") == 0) n -= 5;
    if(n == 0 || n >= SF_PRESET_NAME) continue;
    if(g_preset_n >= SF_PRESET_MAX) break;
    memcpy(g_preset_names[g_preset_n], fd.cFileName, n);
    g_preset_names[g_preset_n][n] = 0;
    g_preset_n++;
  } while(FindNextFileA(h, &fd));

  FindClose(h);

  if(g_preset_n > 1)
    qsort(g_preset_names, (size_t)g_preset_n, sizeof g_preset_names[0],
          sf_preset_cmp);
}

static int sf_preset_load(const char *name, sf_params_block *out)
{
  char path[MAX_PATH];
  char *json;
  long sz;
  FILE *f;
  int rc = -1;

  if(!sf_preset_dir()) return -1;
  sf_preset_path(path, sizeof path, name);

  f = fopen(path, "rb");
  if(!f) return -1;
  if(fseek(f, 0, SEEK_END) || (sz = ftell(f)) <= 0 ||
     sz > SF_PRESET_JSON_MAX || fseek(f, 0, SEEK_SET))
  {
    fclose(f);
    return -1;
  }
  json = (char *)malloc((size_t)sz + 1);
  if(!json) { fclose(f); return -1; }
  if(fread(json, 1, (size_t)sz, f) == (size_t)sz)
  {
    json[sz] = 0;
    rc = sf_preset_read(json, (int)sz, out);
  }
  free(json);
  fclose(f);
  return rc;
}

static int sf_preset_save(const char *name, const sf_params_block *b)
{
  char path[MAX_PATH];
  char *json;
  FILE *f;
  int n, rc = -1;

  if(!sf_preset_dir()) return -1;
  sf_preset_path(path, sizeof path, name);

  json = (char *)malloc(SF_PRESET_JSON_MAX);
  if(!json) return -1;
  n = sf_preset_write(b, name, json, SF_PRESET_JSON_MAX);
  if(n > 0)
  {
    f = fopen(path, "wb");
    if(f)
    {
      rc = (fwrite(json, 1, (size_t)n, f) == (size_t)n) ? 0 : -1;
      fclose(f);
      if(rc) DeleteFileA(path);   /* never leave a half-written preset behind */
    }
  }
  free(json);
  return rc;
}

/* Clipboard is the other half of "copy-paste": Copy publishes the current
 * settings as preset JSON, Paste applies whatever preset text is there. */
static int sf_clip_set(const char *text)
{
  HGLOBAL h;
  char *p;
  size_t n = strlen(text) + 1;

  if(!OpenClipboard(g_hwnd)) return -1;
  EmptyClipboard();
  h = GlobalAlloc(GMEM_MOVEABLE, n);
  if(!h) { CloseClipboard(); return -1; }
  p = (char *)GlobalLock(h);
  if(!p) { GlobalFree(h); CloseClipboard(); return -1; }
  memcpy(p, text, n);
  GlobalUnlock(h);
  /* After a successful SetClipboardData the block belongs to the system. */
  if(!SetClipboardData(CF_TEXT, h)) { GlobalFree(h); CloseClipboard(); return -1; }
  CloseClipboard();
  return 0;
}

static int sf_clip_get(char *buf, size_t cap)
{
  HANDLE h;
  const char *p;
  size_t n;

  if(cap == 0) return -1;
  buf[0] = 0;
  if(!OpenClipboard(g_hwnd)) return -1;
  h = GetClipboardData(CF_TEXT);
  p = h ? (const char *)GlobalLock(h) : NULL;
  if(!p) { CloseClipboard(); return -1; }
  n = strlen(p);
  if(n >= cap) n = cap - 1;
  memcpy(buf, p, n);
  buf[n] = 0;
  GlobalUnlock(h);
  CloseClipboard();
  return 0;
}

static void sf_preset_status(const char *fmt, const char *arg, int err)
{
  snprintf(g_preset_msg, sizeof g_preset_msg, fmt, arg ? arg : "");
  g_preset_msg_err = err;
}

/* The Presets group: a combo of saved looks, the name the actions target, and
 * two rows of buttons. Load and Paste replace the whole block (a preset is a
 * complete look), so both raise the preview dirty flag through `changed`. */
static void sf_imgui_presets_group(sf_imgui_ctx *c, int *changed)
{
  sf_params_block *b = &c->work;
  char name[SF_PRESET_NAME];
  float bw;

  if(!ImGui::CollapsingHeader("Presets")) return;

  if(g_preset_list_dirty)
  {
    g_preset_list_dirty = 0;
    sf_preset_refresh();
  }

  sf_lab("Preset");
  ImGui::SetNextItemWidth(g_sl_w + g_num_w + 4.0f);
  {
    const char *prev = (g_preset_n == 0) ? "(none saved)"
                      : (g_preset_sel >= 0) ? g_preset_names[g_preset_sel]
                                            : "(select)";
    if(ImGui::BeginCombo("##preset", prev))
    {
      for(int i = 0; i < g_preset_n; i++)
      {
        const bool sel = (i == g_preset_sel);
        if(ImGui::Selectable(g_preset_names[i], sel))
        {
          g_preset_sel = i;
          snprintf(g_preset_edit, sizeof g_preset_edit, "%s", g_preset_names[i]);
        }
        if(sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }
  sf_tip("Saved looks in %APPDATA%\\spektrafilm\\presets. Picking one fills "
         "the name below; Load, Save and Delete act on that name. Copy puts "
         "the current settings on the clipboard as text and Paste reads them "
         "back, so a look can be shared or kept in a document.");

  sf_lab("Name");
  ImGui::SetNextItemWidth(g_sl_w + g_num_w + 4.0f);
  if(ImGui::InputText("##presetname", g_preset_edit, sizeof g_preset_edit))
    g_preset_sel = -1;   /* typing detaches from the picked entry */

  snprintf(name, sizeof name, "%s", g_preset_edit);
  sf_preset_sanitize(name, sizeof name);

  bw = (ImGui::GetContentRegionAvail().x -
        ImGui::GetStyle().ItemSpacing.x * 2.0f) / 6.0f;

  sf_lab("");
  if(ImGui::Button("Load", ImVec2(bw, 0.0f)))
  {
    sf_params_block t;
    if(sf_preset_load(name, &t) == 0)
    {
      if(sf_params_validate(&t) != 0)
        sf_preset_status("\"%s\" holds values out of range.", name, 1);
      else
      {
        *b = t;
        *changed = 1;
        sf_preset_status("Loaded \"%s\".", name, 0);
      }
    }
    else
      sf_preset_status("Could not read \"%s\".", name, 1);
  }
  ImGui::SameLine();
  if(ImGui::Button("Save", ImVec2(bw, 0.0f)))
  {
    if(sf_preset_save(name, b) == 0)
    {
      g_preset_list_dirty = 0;
      sf_preset_refresh();
      for(int i = 0; i < g_preset_n; i++)
        if(strcmp(g_preset_names[i], name) == 0) { g_preset_sel = i; break; }
      sf_preset_status("Saved \"%s\".", name, 0);
    }
    else
      sf_preset_status("Could not write \"%s\".", name, 1);
  }
  ImGui::SameLine();
  if(ImGui::Button("Delete", ImVec2(bw, 0.0f)))
  {
    char path[MAX_PATH];
    if(sf_preset_dir() && (sf_preset_path(path, sizeof path, name),
                           DeleteFileA(path)))
    {
      g_preset_list_dirty = 0;
      sf_preset_refresh();
      g_preset_edit[0] = 0;
      sf_preset_status("Deleted \"%s\".", name, 0);
    }
    else
      sf_preset_status("No saved preset named \"%s\".", name, 1);
  }

  sf_lab("");
  if(ImGui::Button("Copy", ImVec2(bw, 0.0f)))
  {
    char *json = (char *)malloc(SF_PRESET_JSON_MAX);
    if(json)
    {
      const int n = sf_preset_write(b, name, json, SF_PRESET_JSON_MAX);
      if(n > 0 && sf_clip_set(json) == 0)
        sf_preset_status("Copied %d bytes of preset JSON to the clipboard.",
                         NULL, 0);
      else
        sf_preset_status("Copy failed.", NULL, 1);
      free(json);
    }
  }
  ImGui::SameLine();
  if(ImGui::Button("Paste", ImVec2(bw, 0.0f)))
  {
    char *json = (char *)malloc(SF_PRESET_JSON_MAX);
    if(json)
    {
      if(sf_clip_get(json, SF_PRESET_JSON_MAX) == 0 && json[0])
      {
        sf_params_block t;
        if(sf_preset_read(json, (int)strlen(json), &t) != 0)
          sf_preset_status("The clipboard does not hold a preset.", NULL, 1);
        else if(sf_params_validate(&t) != 0)
          sf_preset_status("That preset holds values out of range.", NULL, 1);
        else
        {
          *b = t;
          *changed = 1;
          sf_preset_status("Pasted preset settings from the clipboard.", NULL, 0);
        }
      }
      else
        sf_preset_status("The clipboard holds no text.", NULL, 1);
      free(json);
    }
  }
  ImGui::SameLine();
  if(ImGui::Button("Defaults", ImVec2(bw, 0.0f)))
  {
    sf_params_init(b);
    *changed = 1;
    sf_preset_status("Restored the factory defaults.", NULL, 0);
  }

  if(g_preset_msg[0])
    ImGui::TextColored(g_preset_msg_err ? ImVec4(1.00f, 0.45f, 0.45f, 1.0f)
                                        : ImVec4(0.55f, 0.80f, 0.55f, 1.0f),
                       "%s", g_preset_msg);
}

static void sf_imgui_settings_pane(sf_imgui_ctx *c)
{
  sf_params_block *b = &c->work;
  int changed = 0;
  int i;

  sf_grid_measure();

  ImGui::TextUnformatted("Settings");
  ImGui::SameLine();
  ImGui::TextDisabled("(middle/right-click a slider to reset)");
  ImGui::Separator();

  sf_imgui_presets_group(c, &changed);

  /* ------------------------------------------------------ Colour Management */
  if(ImGui::CollapsingHeader("Colour Management", ImGuiTreeNodeFlags_DefaultOpen))
  {
    sf_row_combo("Processing mode", &b->process_mode, SF_PROCESS_MODE_LABELS,
                 SF_PROCESS_MODE_COUNT,
                 "Processing mode: print simulation (neg + print + scan), scan "
                 "negative (develop + scan), or process negative only. Default "
                 "print simulation.", &changed);
    sf_row_check("Scan negative", &b->scan_film,
                 "Develop the film and scan it, without the print stage. Only "
                 "used when Processing mode is Print simulation. Default off.", &changed);
    sf_row_check("Invert scanned negative", &b->scan_negative_invert,
                 "Invert the scanned negative output. Default off.", &changed);
    sf_row_combo_dyn("Input colour space", &b->input_space, sp_color_space_count(),
                     sp_color_space_label,
                     "Input colour space expected for the source image (sRGB, "
                     "Display P3, P3-D65, Rec.709, Rec.2020, Adobe RGB, "
                     "ProPhoto, ACES2065-1, ACEScg, ARRI Wide, ...). Default "
                     "sRGB; enable Auto-detect to take it from the document "
                     "profile.", &changed);
    sf_row_combo_dyn("Input transfer", &b->input_transfer, sp_transfer_count(),
                     sp_transfer_label,
                     "How the host's samples map to scene-linear light (sRGB, "
                     "Linear, Gamma 2,2/2,4/2,6, LogC, S-Log3, V-Log, ACEScct, "
                     "...). Default sRGB.", &changed);
    sf_row_combo("RGB to raw", &b->rgb_to_raw_method, SF_RGB_TO_RAW_METHOD_LABELS,
                 SF_RGB_TO_RAW_METHOD_COUNT,
                 "How the scene-linear input is converted to film raw "
                 "sensitivities: Hanatos 2026 (reference, default), Hanatos "
                 "2025 (per-profile bandpass), or Mallett 2019 (sRGB matrix "
                 "approximation).", &changed);
    sf_row_check("Auto-detect input profile", &b->input_profile_auto,
                 "Auto-detect the document ICC profile and use it to fill the "
                 "input colour space, instead of the manual selection. Default "
                 "off.", &changed);

    /* Apply (or clear) the resolved document matrix; the render's start path
     * does the same, so the live preview matches the final render. */
    if(b->input_profile_auto && c->detect.ok)
    {
      b->input_custom_xyz_valid = 1;
      for(i = 0; i < 9; i++) b->input_custom_rgb_to_xyz[i] = c->detect.matrix[i];
      b->input_custom_white_xy[0] = c->detect.white_xy[0];
      b->input_custom_white_xy[1] = c->detect.white_xy[1];
    }
    else
    {
      b->input_custom_xyz_valid = 0;
    }

    {
      char det[160];
      if(!b->input_profile_auto) snprintf(det, sizeof det, "(auto-detect off)");
      else if(c->detect.ok) snprintf(det, sizeof det, "%s", c->detect.name);
      else snprintf(det, sizeof det, "(no document profile found)");
      sf_row_readonly("Detected profile", det);
    }
  }

  /* ------------------------------------------------------------- Engine */
  if(ImGui::CollapsingHeader("Engine"))
  {
    sf_row_i("LUT size", &b->lut_steps, 0, 33, 17,
             "Cube resolution of the engine's internal tables; 0 renders exact "
             "per-pixel spectra. Higher is more accurate and slower. 9 is the "
             "lowest safe value: below it the render takes a visible colour "
             "cast, worst at 2. Default 17 (1 snaps to 2).", &changed);
    if(b->lut_steps == 1) b->lut_steps = 2;
    sf_row_d("Pixel size um", &b->pixel_um, 0.5f, 40.0f, 4.0f, "%.2f",
             "Physical size of one image pixel on the film; all spatial stages "
             "scale by it. Default 4,00, range 0,50..40,00 um.", &changed);
  }

  /* --------------------------------------------------------------- Film */
  if(ImGui::CollapsingHeader("Film", ImGuiTreeNodeFlags_DefaultOpen))
  {
    {
      int v = b->film_index;
      sf_row_combo("Stock", &v, SF_FILMS_LABELS, SF_FILMS_COUNT,
                   "Film profile used for spectral sensitivity, density curves, "
                   "grain and halation defaults.", &changed);
      b->film_index = (int16_t)v;
    }
    sf_row_combo_dyn("Film format", &b->film_format, sp_film_format_count(),
                     sp_film_format_label,
                     "Film format (gauge): the physical long edge of the film "
                     "frame. The engine derives the per-pixel size from it so "
                     "the spatial stages (grain, halation, diffusion, "
                     "detail/MTF) scale with the format. 35mm (stills) is the "
                     "36 x 24 stills frame; Super 35 is the cinema 35 mm "
                     "negative, smaller on the film, so grain is coarser at "
                     "the same pixel count. Custom uses the Pixel Size field. "
                     "Default Custom.",
                     &changed);
    sf_row_d("Exposure EV", &b->exposure_ev, -8.0f, 8.0f, 0.0f, "%.2f",
             "Film Exposure EV: camera exposure before density formation; +1 "
             "doubles exposure. The print stage re-normalises mid-grey, so the "
             "visible effect is mainly in the highlights/shadows (and becomes "
             "stronger at large +/- values). Default 0,00, range -8,00..8,00 "
             "EV, 0,01 EV steps.", &changed);
    sf_row_check("Auto exposure", &b->auto_exposure,
                 "Meters the frame before the render and offsets the film "
                 "exposure so mid-tones land at the reference density. Default "
                 "off.", &changed);
    sf_row_combo("Auto exposure meter", &b->auto_exposure_method,
                 SF_AUTO_EXPOSURE_METHOD_LABELS, SF_AUTO_EXPOSURE_METHOD_COUNT,
                 "Auto-exposure metering method used to compute the correction. "
                 "Default Centre-weighted.", &changed);
    sf_row_combo("Push / Pull mode", &b->film_push_pull_mode,
                 SF_PUSH_PULL_MODE_LABELS, SF_PUSH_PULL_MODE_COUNT,
                 "Push/Pull mode: Standard maps the stop count onto exposure and "
                 "curve slope; Experimental is the engine's alternate model. "
                 "Default Standard.", &changed);
    sf_row_d("Film push / pull stops", &b->film_push_pull_stops, -4.0f, 4.0f, 0.0f,
             "%.2f",
             "Standard film push or pull in stops: positive push raises "
             "effective speed and contrast, negative pull lowers contrast. "
             "Default 0,00.", &changed);
    sf_row_d("Negative bleach bypass", &b->negative_bleach_bypass_amount, 0.0f, 1.0f,
             0.0f, "%.3f",
             "Negative bleach bypass, 0..1. Retains silver in the negative, "
             "raising contrast and lowering saturation. Linked to the "
             "leuco-cyan coupling slider below (coupling 0 = neutral bleach "
             "bypass; raise it to engage the cyan-layer loss / green shift). "
             "Default 0.", &changed);
    sf_row_d("Leuco-cyan coupling", &b->negative_leuco_cyan_coupling, 0.0f, 2.0f,
             0.0f, "%.3f",
             "Leuco-cyan coupling, 0..2. Couples the cyan layer to the "
             "bleach-bypass silver retention (see bleach bypass above). "
             "Default 0 (no coupling: bleach bypass alone leaves colour "
             "unchanged).", &changed);
    sf_row_d("Film gamma", &b->film_gamma, 0.25f, 4.0f, 1.0f, "%.3f",
             "Scales the film density-curve slope; 1,0 keeps the stock "
             "profile's own. Default 1,000, range 0,250..4,000.", &changed);
  }

  /* -------------------------------------------------------- DIR Couplers */
  if(ImGui::CollapsingHeader("DIR Couplers"))
  {
    sf_row_check("Enable DIR couplers", &b->couplers_active,
                 "DIR coupler redevelopment for contrast and interlayer colour "
                 "interaction. Default on.", &changed);
    sf_row_d("Diffusion um", &b->coupler_diffusion_um, -1.0f, 100.0f, -1.0f, "%.2f",
             "Lateral diffusion distance of the released inhibitor, in "
             "micrometres. -1 uses the stock's own (the pack's core size). "
             "Default -1.", &changed);
    /* validate() wants exactly -1 (stock) or a strictly positive radius; fold
     * the seam at 0 back into the sentinel / the smallest positive value. Only
     * fire when the value is actually in the invalid seam (not the -1 sentinel
     * itself), otherwise `changed` is set every frame and the preview debounce
     * never completes. */
    if(b->coupler_diffusion_um < -1.0) { b->coupler_diffusion_um = -1.0; changed = 1; }
    else if(b->coupler_diffusion_um > -1.0 && b->coupler_diffusion_um < 0.0)
    { b->coupler_diffusion_um = -1.0; changed = 1; }
    else if(b->coupler_diffusion_um >= 0.0 && b->coupler_diffusion_um < 0.01)
    { b->coupler_diffusion_um = 0.01; changed = 1; }
    sf_row_d("Amount", &b->coupler_amount, 0.0f, 2.0f, 1.0f, "%.3f",
             "Overall redevelopment strength. 0 bypasses the effect; 1,0 is the "
             "reference. Default 1,000.", &changed);
    sf_row_d("Same-layer inhibition", &b->coupler_inhibition_samelayer, 0.0f, 2.0f,
             1.0f, "%.3f",
             "Scales same-layer inhibition, which mainly shapes layer contrast. "
             "Default 1,000.", &changed);
    sf_row_d("Interlayer inhibition", &b->coupler_inhibition_interlayer, 0.0f, 2.0f,
             1.0f, "%.3f",
             "Scales interlayer inhibition, which mainly shapes colour "
             "separation. Default 1,000.", &changed);
    sf_row_d("Same-layer gamma R", &b->dir_gamma_same[0], 0.0f, 1.0f, 0.0f, "%.3f",
             "Same-layer DIR gamma for the red donor layer. 0 uses the stock's "
             "fitted value. Default 0.", &changed);
    sf_row_d("Same-layer gamma G", &b->dir_gamma_same[1], 0.0f, 1.0f, 0.0f, "%.3f",
             "Same-layer DIR gamma for the green donor layer. 0 uses the "
             "stock's fitted value. Default 0.", &changed);
    sf_row_d("Same-layer gamma B", &b->dir_gamma_same[2], 0.0f, 1.0f, 0.0f, "%.3f",
             "Same-layer DIR gamma for the blue donor layer. 0 uses the stock's "
             "fitted value. Default 0.", &changed);
    sf_row_d("R -> G/B gamma G", &b->dir_gamma_r_gb[0], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the red donor layer into the green "
             "receiver. Default 0 (stock).", &changed);
    sf_row_d("R -> G/B gamma B", &b->dir_gamma_r_gb[1], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the red donor layer into the blue "
             "receiver. Default 0 (stock).", &changed);
    sf_row_d("G -> R/B gamma R", &b->dir_gamma_g_rb[0], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the green donor layer into the red "
             "receiver. Default 0 (stock).", &changed);
    sf_row_d("G -> R/B gamma B", &b->dir_gamma_g_rb[1], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the green donor layer into the blue "
             "receiver. Default 0 (stock).", &changed);
    sf_row_d("B -> R/G gamma R", &b->dir_gamma_b_rg[0], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the blue donor layer into the red "
             "receiver. Default 0 (stock).", &changed);
    sf_row_d("B -> R/G gamma G", &b->dir_gamma_b_rg[1], 0.0f, 1.0f, 0.0f, "%.3f",
             "Interlayer DIR gamma from the blue donor layer into the green "
             "receiver. Default 0 (stock).", &changed);
  }

  /* -------------------------------------------------------------- Print */
  if(ImGui::CollapsingHeader("Print", ImGuiTreeNodeFlags_DefaultOpen))
  {
    {
      int v = b->paper_index;
      sf_row_combo("Paper", &v, SF_PAPERS_LABELS, SF_PAPERS_COUNT,
                   "Print paper or print film profile used by the enlarger "
                   "stage.", &changed);
      b->paper_index = (int16_t)v;
    }
    sf_row_combo("Print timing", &b->print_timing, SF_PRINT_TIMING_LABELS,
                 SF_PRINT_TIMING_COUNT,
                 "Print exposure model: spectral enlarger filtration (default) "
                 "or APD printer-light responsivity (spectral engine work in "
                 "progress). Default Filtered enlarger.", &changed);
    sf_row_d("Print exposure EV", &b->print_exposure, 0.0f, 10.0f, 1.0f, "%.2f",
             "Print Exposure EV: enlarger exposure applied to the print "
             "material; +1 doubles it. Default 1,00, range 0,00..10,00 EV, 0,01 "
             "EV steps.", &changed);
    sf_row_check("Print auto exposure", &b->print_auto_exposure,
                 "Re-normalise mid-grey so the film exposure has only a subtle "
                 "effect on brightness. Off (default): the full, uncompensated "
                 "exposure effect is kept.", &changed);
    sf_row_d("Print push / pull stops", &b->print_push_pull_stops, -2.0f, 2.0f,
             0.0f, "%.2f",
             "Print push/pull in stops (-2..2). Shifts the enlarger exposure and "
             "the print curve slope. Default 0,00.", &changed);
    sf_row_d("Shadow shape", &b->print_shadow_shape, -1.0f, 1.0f, 0.0f, "%.3f",
             "Print shadow shaping (-1..1). Rolls the print toe; negative "
             "deepens shadows, positive lifts them. Default 0.", &changed);
    sf_row_d("Highlight shape", &b->print_highlight_shape, -1.0f, 1.0f, 0.0f, "%.3f",
             "Print highlight shaping (-1..1). Rolls the print shoulder; "
             "positive compresses highlights. Default 0.", &changed);
    sf_row_d("Print gamma", &b->print_gamma, 0.25f, 4.0f, 1.0f, "%.3f",
             "Scales the print density-curve slope; 1,0 keeps the paper "
             "profile's own. Default 1,000, range 0,250..4,000.", &changed);
    sf_row_d("Print bleach bypass", &b->print_bleach_bypass_amount, 0.0f, 1.0f, 0.0f,
             "%.3f",
             "Print bleach bypass, 0..1. Retains silver in the print, raising "
             "print contrast and lowering saturation. Default 0.", &changed);
    sf_row_d("C filter", &b->filter_c_shift, 0.0f, 120.0f, 0.0f, "%.1f",
             "Cyan filtration trim relative to the neutral print balance, in CC "
             "units. Default 0.", &changed);
    sf_row_d("M filter shift", &b->filter_m_shift, -60.0f, 60.0f, 0.0f, "%.1f",
             "Magenta filtration trim relative to the neutral print balance, in "
             "CC units. Default 0.", &changed);
    sf_row_d("Y filter shift", &b->filter_y_shift, -60.0f, 60.0f, 0.0f, "%.1f",
             "Yellow filtration trim relative to the neutral print balance. "
             "Default 0 (neutral).", &changed);
    sf_row_d("Preflash exposure", &b->preflash_exposure, 0.0f, 1.0f, 0.0f, "%.3f",
             "Uniform pre-exposure added to the paper before the image exposure "
             "to lift toe detail. Default 0 (off).", &changed);
    sf_row_d("Preflash M shift", &b->preflash_m_shift, -60.0f, 60.0f, 0.0f, "%.1f",
             "Magenta filtration applied only to the preflash exposure. Default "
             "0.", &changed);
    sf_row_d("Preflash Y shift", &b->preflash_y_shift, -60.0f, 60.0f, 0.0f, "%.1f",
             "Yellow filtration applied only to the preflash exposure. Default "
             "0.", &changed);
    sf_row_check("Gang printer points", &b->printer_lights_gang,
                 "Link the three printer-light trims. Default off.", &changed);
    sf_row_check("Printer point calibration", &b->printer_light_calibration,
                 "Apply generated neutral APD calibration offsets. Default on.", &changed);
    sf_row_f("Printer point R", &b->printer_lights_r, -24.0f, 24.0f, 0.0f, "%.2f",
             "Red printer-light trim; needs spectral APD data. Default 0.", &changed);
    sf_row_f("Printer point G", &b->printer_lights_g, -24.0f, 24.0f, 0.0f, "%.2f",
             "Green printer-light trim; needs spectral APD data. Default 0.", &changed);
    sf_row_f("Printer point B", &b->printer_lights_b, -24.0f, 24.0f, 0.0f, "%.2f",
             "Blue printer-light trim; needs spectral APD data. Default 0.", &changed);
  }

  /* ----------------------------------------------------------- Halation */
  if(ImGui::CollapsingHeader("Halation"))
  {
    sf_row_check("Enable halation", &b->halation_on,
                 "Film halation and near-emulsion scatter around highlights. "
                 "Default off.", &changed);
    sf_row_f("Boost EV", &b->boost_ev, 0.0f, 20.0f, 0.0f, "%.2f",
             "Halation Boost EV. Lifts highlight exposure before halation. "
             "Default 0 (off).", &changed);
    sf_row_f("Boost range", &b->boost_range, 0.0f, 1.0f, 0.0f, "%.3f",
             "How far above the protect threshold the boost rolls off, in EV. "
             "Default 0.", &changed);
    sf_row_f("Protect EV", &b->protect_ev, 0.0f, 10.0f, 0.0f, "%.2f",
             "Highlights at or above this exposure are left out of the halation "
             "boost. Default 0.", &changed);
    sf_row_f("Scatter amount", &b->scatter_amount, 0.0f, 2.0f, 1.0f, "%.3f",
             "Near-emulsion scatter strength before the longer halation bounce. "
             "Default 1,000.", &changed);
    sf_row_f("Scatter scale", &b->scatter_scale, (float)SF_BLUR_SCALE_MIN,
             (float)SF_BLUR_SCALE_MAX, 1.0f, "%.3f",
             "Radius multiplier for the near-emulsion scatter. Default 1,000.", &changed);
    sf_row_f("Halation amount", &b->halation_amount, 0.0f, 4.0f, 1.0f, "%.3f",
             "Strength of the red-biased film-base back-reflection. Default "
             "1,000.", &changed);
    sf_row_f("Halation scale", &b->halation_scale, (float)SF_BLUR_SCALE_MIN,
             (float)SF_BLUR_SCALE_MAX, 1.0f, "%.3f",
             "Radius multiplier for the film-base halation. Default 1,000.", &changed);
    {
      int pchg = 0;
      int prof = c->hal_profile;
      sf_row_combo("Halation profile", &prof, SF_HALATION_PROFILE_LABELS,
                   SF_HALATION_PROFILE_COUNT,
                   "Load one of the feature-pack halation profiles into the "
                   "weight and first-sigma fields.", &pchg);
      if(pchg) { c->hal_profile = prof; sf_apply_halation_profile(b, prof); changed = 1; }
    }
    {
      double rr = b->halation_strength[0], gg = b->halation_strength[1],
             bb = b->halation_strength[2];
      float hcol[3];
      if(rr < 0.0 && gg < 0.0 && bb < 0.0) { hcol[0] = 13.0f/255.0f; hcol[1] = 4.0f/255.0f; hcol[2] = 0.0f; }
      else
      {
        double m = (rr > gg) ? rr : gg;
        if(bb > m) m = bb;
        if(!(m > 0.0)) m = 0.05;
        hcol[0] = (float)((rr > 0.0 ? rr : 0.0) / m);
        hcol[1] = (float)((gg > 0.0 ? gg : 0.0) / m);
        hcol[2] = (float)((bb > 0.0 ? bb : 0.0) / m);
      }
      sf_lab("Halation colour");
      if(ImGui::ColorButton("##hcol", ImVec4(hcol[0], hcol[1], hcol[2], 1.0f), ImGuiColorEditFlags_NoAlpha,
                            ImVec2(g_sl_w, 0.0f)))
        ImGui::OpenPopup("##hcol_pop");
      sf_tip("Pick a colour to set the per-channel halation return weights "
             "(R/G/B). The strongest channel keeps the current peak weight.");
      if(ImGui::BeginPopup("##hcol_pop"))
      {
        if(ImGui::ColorPicker3("##pick", hcol, 0))
        {
          sf_apply_halation_colour(b, hcol[0], hcol[1], hcol[2]);
          c->hal_profile = 6;
          changed = 1;
        }
        ImGui::EndPopup();
      }
    }
    { int ch0 = changed; sf_row_d("Strength R", &b->halation_strength[0], -1.0f, 1.0f, -1.0f, "%.4f",
             "Per-channel halation return weight, red. -1 uses the film's own "
             "value. Default -1.", &changed); if(changed != ch0) c->hal_profile = 6; }
    { int ch0 = changed; sf_row_d("Strength G", &b->halation_strength[1], -1.0f, 1.0f, -1.0f, "%.4f",
             "Per-channel halation return weight, green. -1 uses the film's own "
             "value. Default -1.", &changed); if(changed != ch0) c->hal_profile = 6; }
    { int ch0 = changed; sf_row_d("Strength B", &b->halation_strength[2], -1.0f, 1.0f, -1.0f, "%.4f",
             "Per-channel halation return weight, blue. -1 uses the film's own "
             "value. Default -1.", &changed); if(changed != ch0) c->hal_profile = 6; }
    sf_row_d("Sigma max um", &b->halation_sigma_max_um, -1.0f,
             (float)SF_HALATION_SIGMA_MAX_CAP_UM, -1.0f, "%.1f",
             "Ceiling on the per-channel first-bounce radii below, in "
             "micrometres. A radius above it is rendered at the ceiling. -1 "
             "keeps the stock ceiling (65 um). Default -1.", &changed);
    /* validate() accepts <0 (the stock ceiling) or a positive radius; 0 lies in
     * neither, so fold it back to the sentinel. */
    if(b->halation_sigma_max_um == 0.0) { b->halation_sigma_max_um = -1.0; changed = 1; }
    {
      const float sig_cap = (float)sf_halation_cap_um(b);
      sf_row_d("First sigma R um", &b->halation_first_sigma_um[0], -1.0f, sig_cap,
               -1.0f, "%.1f",
               "Per-channel halation first-bounce radius, red, in micrometres. "
               "-1 uses the film's own (about 65 um still / 50 um cine). Default "
               "-1.", &changed);
      sf_row_d("First sigma G um", &b->halation_first_sigma_um[1], -1.0f, sig_cap,
               -1.0f, "%.1f",
               "Per-channel halation first-bounce radius, green, in micrometres. "
               "Default -1 (the film's own).", &changed);
      sf_row_d("First sigma B um", &b->halation_first_sigma_um[2], -1.0f, sig_cap,
               -1.0f, "%.1f",
               "Per-channel halation first-bounce radius, blue, in micrometres. "
               "Default -1 (the film's own).", &changed);
      /* A radius above the ceiling stays in its field and is rendered at the
       * ceiling: the chain clamps it, and the preview and OK fold the block the
       * same way. Rewriting the three fields here would throw away radii the
       * user set channel by channel the moment they nudged this slider down,
       * and raising the ceiling back would not bring them back. */
      {
        int over = 0;
        for(i = 0; i < 3; i++)
          if(b->halation_first_sigma_um[i] > (double)sig_cap) over = 1;
        if(over)
        {
          char cap_txt[32];
          sf_num_txt(cap_txt, sizeof cap_txt, "%.1f", (double)sig_cap);
          ImGui::TextDisabled("A radius above the ceiling renders at %s um.", cap_txt);
        }
      }
    }
    /* The widest bounce the stage will run: the first bounce, times its own
     * scale, times sqrt(3) for the third. The scatter half of the stage cannot
     * reach the threshold -- its tail is pinned at 9.7 um, which is 215 px even
     * at scale 4 on the finest pitch the surface allows -- so the first-bounce
     * radius alone decides whether this line is drawn. */
    {
      double first = 0.0;
      for(i = 0; i < 3; i++)
        if(b->halation_first_sigma_um[i] > first) first = b->halation_first_sigma_um[i];
      if(first <= 0.0) first = SP_HALATION_FIRST_SIGMA_UM;
      if(first > sf_halation_cap_um(b)) first = sf_halation_cap_um(b);
      sf_slow_preview_note(first * (double)b->halation_scale * 1.7320508 /
                               sf_effective_pixel_um(b, c->sw, c->sh),
                           "halation");
    }
  }

  /* ---------------------------------------------------------- Diffusion */
  if(ImGui::CollapsingHeader("Diffusion"))
  {
    ImGui::TextDisabled("Camera");
    sf_row_check("Enable camera diffusion", &b->diffusion_on,
                 "A photographic diffusion filter in the camera path, applied in "
                 "linear light. Default off.", &changed);
    sf_row_combo("Camera family", &b->diffusion_family, SF_DIFF_LABELS,
                 SF_DIFF_COUNT,
                 "Diffusion filter type (Glimmerglass / Black Pro-Mist / "
                 "Pro-Mist / CineBloom). Default Black Pro-Mist.", &changed);
    sf_row_f("Camera strength", &b->diffusion_strength, 0.0f, 2.0f, 1.0f, "%.3f",
             "Overall camera diffusion intensity. Default 1,000.", &changed);
    sf_row_f("Camera scale", &b->diffusion_scale, (float)SF_BLUR_SCALE_MIN,
             (float)SF_BLUR_SCALE_MAX, 1.0f, "%.3f",
             "Radius multiplier for the camera diffusion. Default 1,000.", &changed);
    sf_row_f("Camera warmth", &b->diffusion_warmth, -1.5f, 1.5f, 0.0f, "%.3f",
             "Warm/cool bias of the diffusion halo; positive is warmer. Default "
             "0.", &changed);
    sf_row_f("Camera core intensity", &b->diffusion_core_intensity, 0.0f, 4.0f, 1.0f,
             "%.3f",
             "Core (bright-centre) intensity offset on top of the family "
             "profile. Default 1,000.", &changed);
    sf_row_f("Camera core size", &b->diffusion_core_size, 0.1f, 4.0f, 1.0f, "%.3f",
             "Core size scale. Default 1,000.", &changed);
    sf_row_f("Camera halo intensity", &b->diffusion_halo_intensity, 0.0f, 4.0f, 1.0f,
             "%.3f",
             "Halo intensity offset on top of the family profile. Default "
             "1,000.", &changed);
    sf_row_f("Camera halo size", &b->diffusion_halo_size, 0.1f, 4.0f, 1.0f, "%.3f",
             "Halo size. Default 1,000.", &changed);
    sf_row_f("Camera bloom intensity", &b->diffusion_bloom_intensity, 0.0f, 4.0f, 1.0f,
             "%.3f",
             "Bloom (wide, low-frequency) intensity offset. Default 1,000.", &changed);
    sf_row_f("Camera bloom size", &b->diffusion_bloom_size, 0.1f, 4.0f, 1.0f, "%.3f",
             "Bloom size. Default 1,000.", &changed);

    ImGui::Spacing();
    ImGui::TextDisabled("Print");
    sf_row_check("Enable print diffusion", &b->print_diffusion_on,
                 "A diffusion filter in the enlarger path, between print "
                 "exposure and development. Default off.", &changed);
    sf_row_combo("Print family", &b->print_diffusion_family, SF_DIFF_LABELS,
                 SF_DIFF_COUNT,
                 "Diffusion filter type used in the print stage. Default Black "
                 "Pro-Mist.", &changed);
    sf_row_f("Print strength", &b->print_diffusion_strength, 0.0f, 2.0f, 1.0f, "%.3f",
             "Overall print diffusion intensity. Default 1,000.", &changed);
    sf_row_f("Print scale", &b->print_diffusion_scale, (float)SF_BLUR_SCALE_MIN,
             (float)SF_BLUR_SCALE_MAX, 1.0f, "%.3f",
             "Radius multiplier for the print diffusion. Default 1,000.", &changed);
    sf_row_f("Print warmth", &b->print_diffusion_warmth, -1.5f, 1.5f, 0.0f, "%.3f",
             "Warm/cool bias of the print diffusion halo. Default 0.", &changed);
    sf_row_f("Print core intensity", &b->print_diffusion_core_intensity, 0.0f, 4.0f,
             1.0f, "%.3f",
             "Core (bright-centre) intensity offset on top of the family "
             "profile. Default 1,000.", &changed);
    sf_row_f("Print core size", &b->print_diffusion_core_size, 0.1f, 4.0f, 1.0f,
             "%.3f", "Core size. Default 1,000.", &changed);
    sf_row_f("Print halo intensity", &b->print_diffusion_halo_intensity, 0.0f, 4.0f,
             1.0f, "%.3f",
             "Halo intensity offset on top of the family profile. Default "
             "1,000.", &changed);
    sf_row_f("Print halo size", &b->print_diffusion_halo_size, 0.1f, 4.0f, 1.0f,
             "%.3f", "Halo size. Default 1,000.", &changed);
    sf_row_f("Print bloom intensity", &b->print_diffusion_bloom_intensity, 0.0f, 4.0f,
             1.0f, "%.3f",
             "Bloom intensity offset. Default 1,000.", &changed);
    sf_row_f("Print bloom size", &b->print_diffusion_bloom_size, 0.1f, 4.0f, 1.0f,
             "%.3f", "Bloom size. Default 1,000.", &changed);

    /* The widest Gaussian either diffusion path will build, at its own scale and
     * size multipliers. The bank's radii come from the engine, not from a copy
     * of the family tables here, so a new family cannot make this line lie. */
    {
      double cam = 0.0, prn = 0.0;
      if(b->diffusion_on)
        cam = sf_diffusion_widest_sigma_um(b->diffusion_family) *
              (double)b->diffusion_scale *
              std::fmax((double)b->diffusion_halo_size, (double)b->diffusion_bloom_size);
      if(b->print_diffusion_on)
        prn = sf_diffusion_widest_sigma_um(b->print_diffusion_family) *
              (double)b->print_diffusion_scale *
              std::fmax((double)b->print_diffusion_halo_size,
                   (double)b->print_diffusion_bloom_size);
      sf_slow_preview_note(std::fmax(cam, prn) / sf_effective_pixel_um(b, c->sw, c->sh),
                           "diffusion");
    }
  }

  /* -------------------------------------------------------------- Grain */
  if(ImGui::CollapsingHeader("Grain"))
  {
    sf_row_check("Enable grain", &b->grain_on,
                 "Enable production grain (layered dye-cloud grain on the "
                 "developed density). Default off.", &changed);
    sf_row_combo("Model", &b->grain_model, SF_GRAIN_MODEL_LABELS,
                 SF_GRAIN_MODEL_COUNT,
                 "Preview collapses to the coarsest sub-layer; Production keeps "
                 "the stock's layered dye-cloud grain. Default Production.", &changed);
    sf_row_check("Sublayers", &b->grain_sublayers_enabled,
                 "When off, grain renders as a single coarsest layer instead of "
                 "the layered production model. Default on.", &changed);
    sf_row_i("Sub layer count", &b->grain_sublayer_count, 0, 8, 0,
             "Clamps the stock's sub-layer count (the engine has no curves for "
             "layers the stock did not fit). 0 = the stock's own. Default 0.", &changed);
    sf_row_f("Amount", &b->grain_amount, 0.0f, 2.0f, 1.0f, "%.3f",
             "Scales the final density-space grain contribution. Default "
             "1,000.", &changed);
    sf_row_f("Saturation", &b->grain_saturation, 0.0f, 1.0f, 1.0f, "%.3f",
             "1 = full colour grain, 0 = neutral luminance grain. Default "
             "1,000.", &changed);
    sf_row_i("Seed", &b->grain_seed, 0, 1000000, 0,
             "0 uses the stock pattern; any other value reseeds the grain "
             "field. Default 0.", &changed);
    sf_row_f("Micro structure", &b->grain_micro_structure_scale, 0.05f, 4.0f, 1.0f,
             "%.3f",
             "Multiplier on the dye-cloud blur; 1,0 keeps the stock's own "
             "dye-cloud structure. Default 1,000.", &changed);
    sf_row_f("Clump blur", &b->grain_blur, 0.0f, 4.0f, 1.0f, "%.3f",
             "Clumping of the developed grain; 1,0 is the stock's own figure. "
             "Default 1,000.", &changed);
    sf_row_f("Particle area", &b->grain_granularity, 0.0f, 4.0f, 1.0f, "%.3f",
             "Scales the stock's rms granularity, changing the particle "
             "population. Default 1,000.", &changed);
    sf_row_f("Uniformity", &b->grain_uniformity, 0.0f, 1.0f, 1.0f, "%.3f",
             "Higher values make grain more even; lower values increase "
             "clumping. Default 1,000.", &changed);
    sf_row_f("Sublayer scale", &b->grain_sublayer_scale, 0.0f, 4.0f, 1.0f, "%.3f",
             "Size multiplier for the finer emulsion sub-layers. Default "
             "1,000.", &changed);
    sf_row_f("Density min", &b->grain_density_min, 0.0f, 1.0f, 1.0f, "%.3f",
             "Minimum per-channel grain density contribution. Default 1,000.", &changed);
    sf_row_f("Dye cloud", &b->grain_dye_cloud, 0.0f, 4.0f, 1.0f, "%.3f",
             "Physical dye-cloud blur of the production grain. Default 1,000.", &changed);
    sf_row_f("Recovery radius", &b->grain_usm_sigma, 0.0f, 10.0f, 0.7f, "%.3f",
             "Radius of the acutance recovery after the grain clump blur. "
             "Default 0,700.", &changed);
    sf_row_f("Recovery amount", &b->grain_usm_amount, 0.0f, 4.0f, 1.5f, "%.3f",
             "Strength of the grain acutance recovery. Default 1,500.", &changed);
    sf_row_f("Final blur um", &b->grain_final_blur_um, -1.0f, 25.0f, -1.0f, "%.2f",
             "Absolute clump-blur sigma; -1 keeps the stock-scaled clump blur. "
             "Default -1.", &changed);
    sf_row_f("Dye cloud um", &b->grain_dye_cloud_um, -1.0f, 10.0f, -1.0f, "%.2f",
             "Absolute dye-cloud blur in um; -1 keeps the stock-scaled "
             "dye-cloud blur. Default -1.", &changed);
    ImGui::TextDisabled("Per-channel / per-layer (1 = the stock's own)");
    sf_row_f("Uniformity R", &b->grain_uniformity_rgb[0], 0.0f, 1.0f, 1.0f, "%.3f",
             "Red uniformity multiplier on the stock's own figure. Default 1.", &changed);
    sf_row_f("Uniformity G", &b->grain_uniformity_rgb[1], 0.0f, 1.0f, 1.0f, "%.3f",
             "Green uniformity multiplier on the stock's own figure. Default 1.", &changed);
    sf_row_f("Uniformity B", &b->grain_uniformity_rgb[2], 0.0f, 1.0f, 1.0f, "%.3f",
             "Blue uniformity multiplier on the stock's own figure. Default 1.", &changed);
    sf_row_f("Particle R", &b->grain_particle_scale[0], 0.1f, 5.0f, 1.0f, "%.3f",
             "Red particle-area scale; > 1 makes red grain coarser. Default 1.", &changed);
    sf_row_f("Particle G", &b->grain_particle_scale[1], 0.1f, 5.0f, 1.0f, "%.3f",
             "Green particle-area scale; > 1 makes green grain coarser. Default 1.", &changed);
    sf_row_f("Particle B", &b->grain_particle_scale[2], 0.1f, 5.0f, 1.0f, "%.3f",
             "Blue particle-area scale; > 1 makes blue grain coarser. Default 1.", &changed);
    sf_row_f("Layer 0 scale", &b->grain_particle_scale_layer[0], 0.1f, 8.0f, 1.0f,
             "%.3f", "Coarsest sub-layer particle-area scale. Default 1.", &changed);
    sf_row_f("Layer 1 scale", &b->grain_particle_scale_layer[1], 0.1f, 8.0f, 1.0f,
             "%.3f", "Middle sub-layer particle-area scale. Default 1.", &changed);
    sf_row_f("Layer 2 scale", &b->grain_particle_scale_layer[2], 0.1f, 8.0f, 1.0f,
             "%.3f", "Finest sub-layer particle-area scale. Default 1.", &changed);
    sf_row_f("Density min R", &b->grain_density_min_rgb[0], 0.0f, 1.0f, 1.0f, "%.3f",
             "Red multiplier on the stock's grain density floor. Default 1.", &changed);
    sf_row_f("Density min G", &b->grain_density_min_rgb[1], 0.0f, 1.0f, 1.0f, "%.3f",
             "Green multiplier on the stock's grain density floor. Default 1.", &changed);
    sf_row_f("Density min B", &b->grain_density_min_rgb[2], 0.0f, 1.0f, 1.0f, "%.3f",
             "Blue multiplier on the stock's grain density floor. Default 1.", &changed);
  }

  /* ------------------------------------------------------------ Scanner */
  if(ImGui::CollapsingHeader("Scanner"))
  {
    sf_row_f("Optical blur px", &b->scan_blur, 0.0f, 20.0f, 0.0f, "%.3f",
             "Gaussian blur of the scanner lens; 0 bypasses optical blur. "
             "Default 0.", &changed);
    sf_row_f("Unsharp radius px", &b->scan_usm_sigma, 0.0f, 20.0f, 0.0f, "%.3f",
             "Scanner unsharp-mask radius, in pixels. Default 0.", &changed);
    sf_row_f("Unsharp amount", &b->scan_usm_amount, 0.0f, 4.0f, 0.0f, "%.3f",
             "Strength of the scanner unsharp masking. Default 0.", &changed);
    sf_row_d("MTF50 lp/mm", &b->scan_mtf50_lpmm, 0.0f, 300.0f, 0.0f, "%.1f",
             "Scanner MTF50 in line-pairs per millimetre. Applies the scanner "
             "lens response to the scan; 0 bypasses. Default 0.", &changed);
    sf_row_d("Unsharp radius um", &b->scan_usm_radius_um, 0.0f, 100.0f, 0.0f, "%.2f",
             "Scanner unsharp-mask radius in micrometres. Absolute physical "
             "radius; 0 uses the pixel-based radius. Default 0.", &changed);
    sf_row_f("Glare percent", &b->glare_percent, 0.0f, 0.2f, 0.0f, "%.4f",
             "Mean print-scan glare added from the illuminant, as a fraction "
             "(0..0,2). Default 0.", &changed);
    sf_row_f("Glare roughness", &b->scan_glare_roughness, 0.0f, 4.0f, 0.7f, "%.3f",
             "Roughness of the viewing glare. Default 0,700.", &changed);
    sf_row_f("Glare blur", &b->scan_glare_blur, 0.0f, 32.0f, 0.5f, "%.2f",
             "Blur radius of the viewing glare. Default 0,50.", &changed);
    sf_row_check("Black correction", &b->scan_black_correction,
                 "Normalise scanner output so the chosen black level maps to "
                 "display black. Default on.", &changed);
    sf_row_check("White correction", &b->scan_white_correction,
                 "Normalise scanner output so the chosen white level maps to "
                 "display white. Default on.", &changed);
    sf_row_d("Black level", &b->scan_black_level, 0.0f, 1.0f, 0.01f, "%.4f",
             "Input level treated as scanner black when black correction is "
             "enabled. Default 0,0100.", &changed);
    sf_row_d("White level", &b->scan_white_level, 0.0f, 1.0f, 0.98f, "%.4f",
             "Input level treated as scanner white when white correction is "
             "enabled. Default 0,9800.", &changed);
  }

  /* ------------------------------------------------------ Camera Filters */
  if(ImGui::CollapsingHeader("Camera Filters"))
  {
    sf_row_check("Filter UV", &b->camera_uv_filter_enabled,
                 "Apply camera-side UV cut before film exposure (spectral "
                 "feasibility work pending). Default off.", &changed);
    sf_row_f("UV cut nm", &b->camera_uv_cut_nm, 280.0f, 460.0f, 410.0f, "%.0f",
             "UV cut centre wavelength, nm. Default 410.", &changed);
    sf_row_check("Filter IR", &b->camera_ir_filter_enabled,
                 "Apply camera-side IR cut before film exposure (spectral "
                 "feasibility work pending). Default off.", &changed);
    sf_row_f("IR cut nm", &b->camera_ir_cut_nm, 600.0f, 760.0f, 675.0f, "%.0f",
             "IR cut centre wavelength, nm. Default 675, range 600..760.", &changed);
  }

  /* How long the preview waits depends on how the change arrived. A slider
   * under the button is held rather than debounced: the preview pane keeps the
   * deadline out until the button is released, so a drag that stops on the way
   * renders nothing. A wheel notch waits out the rolling, so starting and
   * stopping again inside that window is still one edit. Everything else - a
   * committed number field, a dropdown, a reset - renders on the short
   * debounce. */
  c->preview_hold = g_slider_held;
  if(changed)
  {
    unsigned int delay = SF_PREVIEW_DEBOUNCE_MS;
    if(g_wheel_notch) delay = SF_WHEEL_SETTLE_MS;
    if(g_slider_held) delay = SF_SLIDER_HOLD_MS;
    sf_preview_mark_dirty(delay);
  }
}

/* The Windows 11 accent. Dark theme uses the lighter #4CC2FF (good as a fill on
 * dark backgrounds); light theme the deeper #0067C0. */
static ImVec4 sf_accent(int light)
{
  return light ? ImVec4(0.000f, 0.404f, 0.753f, 1.0f)
               : ImVec4(0.298f, 0.761f, 1.000f, 1.0f);
}

/* A filled accent button (used for OK). White text reads on both accents. */
static int sf_accent_button(const char *label, float width)
{
  const int light = (g_theme == 1);
  const ImVec4 bg  = light ? ImVec4(0.000f, 0.404f, 0.753f, 1.0f)
                           : ImVec4(0.098f, 0.518f, 1.000f, 1.0f);
  const ImVec4 hi  = light ? ImVec4(0.098f, 0.463f, 0.788f, 1.0f)
                           : ImVec4(0.200f, 0.580f, 1.000f, 1.0f);
  const ImVec4 act = light ? ImVec4(0.000f, 0.322f, 0.604f, 1.0f)
                           : ImVec4(0.050f, 0.420f, 0.850f, 1.0f);
  int r;
  ImGui::PushStyleColor(ImGuiCol_Button, bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hi);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, act);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  r = ImGui::Button(label, ImVec2(width, 0.0f)) ? 1 : 0;
  ImGui::PopStyleColor(4);
  return r;
}

/* Commit the edited copy (OK only). The sliders clamp themselves, so this
 * should always pass; guard anyway (the native dialog rejects out-of-range
 * blocks the same way). The halation radii are folded to the ceiling first, on
 * a copy: what the host is handed must be what was previewed and rendered, and
 * the edited block is only replaced once the accept actually goes through. */
static void sf_imgui_accept(sf_imgui_ctx *c)
{
  sf_params_block b = c->work;
  sf_halation_reconcile(&b);
  if(sf_params_validate(&b) == 0 && b.film_index >= 0 && b.film_index < SF_FILMS_COUNT &&
     (b.scan_film || (b.paper_index >= 0 && b.paper_index < SF_PAPERS_COUNT)))
  {
    c->work = b;
    g_done = 1;
    g_result = 1;
    g_dirty = 1;
  }
  else
  {
    c->has_ok_err = 1;
    snprintf(c->ok_err, sizeof c->ok_err,
             "One or more values are out of range; fix them before OK.");
  }
}

/* The About dialog's text column, as a minimum in em: the dialog is as wide as
 * the widest line that must not break, but never narrower than this, so the
 * paragraphs keep a readable measure at any font size.
 *
 * 28 em gives the paragraphs a comfortable measure and keeps the dialog well
 * inside the plug-in window without scrolling: the window's client area is
 * 33 em tall (792 px at 96 dpi) and the dialog adds 56 px of title bar and
 * padding to its text. Measured with the project's own ImGui at 24 px: 644
 * px of text at 28 em in Liberation Sans, a 700 px dialog; 692 px in the
 * much wider DejaVu Sans -- a metric proxy, never shipped -- a 748 px
 * dialog. Neither scrolls, and Segoe UI, the font that ships, is a little
 * narrower than Liberation. A 22 em floor would fit too now that the
 * description is a single line, but it wraps the remaining paragraphs into
 * a taller, narrower 748 px dialog. */
#define SF_ABOUT_MIN_EM 28.0f
#define SF_ABOUT_REPO   "github.com/andreavolpato/spektrafilm"

/* About: a real modal dialog — dims and blocks the settings dialog behind it,
 * so Esc here closes only the modal (the global Esc handler at the end of
 * sf_imgui_build() stands down while any popup is open).
 *
 * The dialog is a fixed size that fits its content, and it must not change
 * shape when it is dragged. Two things are needed for that:
 *
 *  - PushTextWrapPos() takes a *window-local* x (CalcWrapWidthForPos() adds
 *    window->Pos.x to it), so the wrap position is computed from
 *    GetCursorPos(), not GetCursorScreenPos(): with a screen-space value the
 *    wrap width became 480 + window->Pos.x, so dragging the dialog reflowed
 *    every paragraph and resized the window along with it.
 *  - The width is pinned from the text. SetNextWindowSize() with a zero height
 *    pins only that axis — Begin(): "Using SetNextWindowSize() overrides
 *    ImGuiWindowFlags_AlwaysAutoResize", per axis — so the auto-fitted width
 *    cannot feed back on itself: SeparatorText() extends the content to the
 *    inner right edge, which for an auto-fit width is a moving target.
 *
 * The height stays auto-fitted, so the dialog is exactly as tall as its text
 * needs at that width; ImGui clamps it to the viewport and scrolls the rest. */
static void sf_imgui_about_modal(bool *p_open)
{
  const ImGuiStyle &st = ImGui::GetStyle();
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  char build[160], presets[160];
  float text_w, win_w, max_w;

  snprintf(build, sizeof build, "Build %s  ·  Dear ImGui %s on Direct3D 11",
           sf_build_date_iso(), IMGUI_VERSION);
  snprintf(presets, sizeof presets, "Presets: %%APPDATA%%\\spektrafilm\\presets\\");

  /* The widest line that has to stay on one line, floored at SF_ABOUT_MIN_EM so
   * the paragraphs get a sensible measure, and capped so the dialog still fits
   * the screen. All of it is measured from the font, so the shape it gives the
   * dialog is the same at any DPI. */
  {
    const char *keep[] = { SF_PRODUCT_NAME " " SF_VERSION, build, presets,
                           SF_ABOUT_REPO };
    text_w = ImGui::GetFontSize() * SF_ABOUT_MIN_EM;
    for(size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++)
    {
      float w = ImGui::CalcTextSize(keep[i]).x;
      if(w > text_w)
        text_w = w;
    }
  }
  max_w = vp->WorkSize.x - st.WindowPadding.x * 2.0f - st.WindowBorderSize;
  if(text_w > max_w)
    text_w = max_w;
  win_w = (float)(int)(text_w + st.WindowPadding.x * 2.0f + st.WindowBorderSize);

  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(win_w, 0.0f), ImGuiCond_Always);
  if(ImGui::BeginPopupModal("About " SF_PRODUCT_NAME, p_open,
                            ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoResize))
  {
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + text_w);
    ImGui::TextUnformatted(SF_PRODUCT_NAME " " SF_VERSION);
    ImGui::TextWrapped("Spectral film simulations of analogue photography.");
    ImGui::TextDisabled("%s", build);
    ImGui::TextDisabled("%s", presets);

    ImGui::Spacing();
    // The upstream project asks to be credited by name in plug-in descriptions
    // and credits (see CITATION.cff in andreavolpato/spektrafilm), so it leads
    // this section instead of appearing as one source among several.
    ImGui::SeparatorText("Acknowledgements");
    ImGui::PushStyleColor(ImGuiCol_Text, sf_accent(g_theme == 1));
    ImGui::TextWrapped("Spectral film modelling powered by spektrafilm.");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("A derivative of Andrea Volpato's spektrafilm project.");
    ImGui::TextDisabled(SF_ABOUT_REPO);
    /* Bullet() + TextWrapped(), not BulletText(): the latter never wraps, so a
     * long credit would be clipped at the fixed width instead of breaking. */
    ImGui::Bullet();
    ImGui::TextWrapped("darktable spektrafilm module "
                       "(darktable-org/darktable-spektrafilm)");
    ImGui::Bullet();
    ImGui::TextWrapped("spektrafilm OFX plug-in (chaert-s/spektrafilm-ofx)");
    ImGui::Bullet();
    ImGui::TextWrapped("Mallett 2019 basis (turbasvin/spektrafilm-rs)");
    ImGui::Bullet();
    ImGui::TextWrapped("Hanatos sensitivity model (hanatos/vkdt)");

    ImGui::Spacing();
    ImGui::SeparatorText("Licence");
    ImGui::TextUnformatted("GPL-3.0-or-later");
    ImGui::TextWrapped("LICENSE and THIRD-PARTY-NOTICES.md carry the full text "
                       "and notices.");
    /* The pack's licence names what must travel with every copy and derivative,
     * "an app's About screen" included: the author, the canonical source, the
     * licence notice, and for modified files a note that they were modified and
     * by whom. Ours is modified — sync_pack.py folds the archived bandpass into
     * 20 film profiles — so the note belongs here, not only in the sources. The
     * canonical source is the URL under Acknowledgements. */
    ImGui::TextWrapped("Data pack: spektrafilm by Andrea Volpato, CC BY-SA 4.0. "
                       "Modified by this project: the archived OFX bandpass "
                       "merged into 20 film profiles.");
    ImGui::TextWrapped("Our own declaration of the documented .8bf binary "
                       "layout: no Adobe SDK, no Adobe code.");
    ImGui::PopTextWrapPos();

    /* Esc closes the modal; Enter is accepted here too, as the modal has one
     * action and no text field. Either way the frame is recorded: the global
     * Esc handler below runs later in the same frame, would now see "no popup
     * open" and cancel the whole dialog on the same keypress. */
    if(ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
       ImGui::IsKeyPressed(ImGuiKey_Enter, false))
    {
      ImGui::CloseCurrentPopup();
      *p_open = false;
      g_about_key_frame = ImGui::GetFrameCount();
    }
    ImGui::Spacing();
    if(sf_accent_button("Close", 100.0f))
    {
      ImGui::CloseCurrentPopup();
      *p_open = false;
    }
    ImGui::EndPopup();
  }
}

static void sf_imgui_build(sf_imgui_ctx *c)
{
  ImGuiIO &io = ImGui::GetIO();
  int field_held_focus;
  float footer;
  ImVec2 avail, pane;
  float left_w;

  /* Whether a control held keyboard focus going into this frame, taken before
   * anything is submitted. A text field that finishes its edit does so inside
   * the frame - Enter, Esc and a click elsewhere all release its focus there -
   * so by the time the footer is built IsAnyItemActive() is already false and
   * can no longer say that the keypress below belonged to the field. NewFrame()
   * keeps ActiveId for a control that submitted last frame, so asking here is
   * the one place the answer is still true. */
  field_held_focus = ImGui::IsAnyItemActive() ? 1 : 0;

  /* Slider state lasts one frame: the panes below set it as they submit their
   * widgets, and the tail of the settings pane reads it to choose how long the
   * preview should wait. */
  g_slider_held = 0;
  g_wheel_notch = 0;

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##sf_root", NULL,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
               ImGuiWindowFlags_NoBringToFrontOnFocus |
               ImGuiWindowFlags_NoSavedSettings |
               ImGuiWindowFlags_NoScrollbar |
               ImGuiWindowFlags_NoScrollWithMouse);

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(SF_PRODUCT_NAME " " SF_VERSION);
  ImGui::SameLine();
  if(ImGui::Button(g_theme == 0 ? "Theme: Dark" : "Theme: Light"))
  {
    g_theme = (g_theme == 0) ? 1 : 0;
    g_style_dirty = 1;
    g_dirty = 1;
  }
  if(ImGui::IsItemHovered())
    ImGui::SetTooltip("Switch between the dark and light interface themes.");
  ImGui::SameLine();
  if(ImGui::Button("About"))
  {
    /* OpenPopup owns the visible state; g_about_open mirrors it so the title
     * X (which only clears the flag) and our own Close/Esc/Enter all agree.
     * Passing a *false* p_open to BeginPopupModal would close the popup in
     * the same frame it opens, so the flag is set here, never cleared
     * before the modal call. */
    g_about_open = true;
    ImGui::OpenPopup("About " SF_PRODUCT_NAME);
  }
  sf_imgui_about_modal(&g_about_open);
  ImGui::Separator();

  footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  avail = ImGui::GetContentRegionAvail();
  pane.y = avail.y - footer;
  if(pane.y < 80.0f) pane.y = 80.0f;
  left_w = avail.x * 0.56f;
  {
    /* A settings row needs SF_ROW_MIN_EM of font, plus the child window's own
     * padding and border, to hold its label, slider and value at the current
     * font size. If the split leaves less than that to the settings pane, the
     * preview gives up width first: a narrower preview is still a preview,
     * while a row without room draws its label over its slider. */
    const ImGuiStyle &st = ImGui::GetStyle();
    float want = ImGui::GetFontSize() * SF_ROW_MIN_EM + st.WindowPadding.x * 2.0f +
                 st.ChildBorderSize * 2.0f;
    if(avail.x - left_w - st.ItemSpacing.x < want)
      left_w = avail.x - st.ItemSpacing.x - want;
  }
  if(left_w < 200.0f) left_w = 200.0f;
  pane.x = left_w;

  ImGui::BeginChild("##sf_preview", ImVec2(pane.x, pane.y), ImGuiChildFlags_Borders);
  sf_imgui_preview_pane(c);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##sf_settings", ImVec2(0.0f, pane.y), ImGuiChildFlags_Borders);
  sf_imgui_settings_pane(c);
  ImGui::EndChild();

  ImGui::Separator();

  /* OK and Cancel are deliberately out of reach of the keyboard: NoNav keeps
   * them from ever becoming the navigation focus, so neither Enter nor Space
   * can start the render or close the window. Applying is a decision that
   * takes a click, on OK, on Cancel, or on the title-bar X. The cost is that
   * a keyboard-only user cannot press OK either; Tab and the arrows still walk
   * every setting, and Esc below still cancels. */
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
  if(sf_accent_button("OK", 100.0f))
    sf_imgui_accept(c);
  ImGui::SameLine();
  if(ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
  {
    g_done = 1;
    g_result = 0;
    g_dirty = 1;
  }
  ImGui::PopItemFlag();
  if(c->has_ok_err)
  {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", c->ok_err);
  }

  /* Keyboard: Esc cancels, and no key applies. Never while a popup (combo /
   * colour picker / About modal) owns the screen, never on the frame the About
   * modal consumed the key to close itself, and never when a control held
   * focus coming into this frame: Enter and Esc both finish a text field's
   * edit, and the field releases its focus inside this same frame, so the
   * capture above is what keeps either key from reaching the dialog here. */
  if(!field_held_focus &&
     !ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
     ImGui::GetFrameCount() != g_about_key_frame)
  {
    if(ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
      g_done = 1;
      g_result = 0;
      g_dirty = 1;
    }
  }

  ImGui::End();
}

static LRESULT CALLBACK sf_imgui_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if(ImGui_ImplWin32_WndProcHandler(h, m, w, l))
    return 1;

  switch(m)
  {
    case WM_SIZE:
      if(w != SIZE_MINIMIZED && l != 0)
      {
        g_resize_w = LOWORD(l);
        g_resize_h = HIWORD(l);
        g_need_resize = 1;
        g_dirty = 1;
      }
      return 0;
    case WM_GETMINMAXINFO:
    {
      /* The minimum tracks the UI font, so a settings row still fits at 200%
       * DPI. ptMinTrackSize is the *outer* size, so add the frame thickness
       * measured from this window's own rects -- DPI-correct for its monitor,
       * where AdjustWindowRectEx would answer for the system DPI. */
      MINMAXINFO *mmi = (MINMAXINFO *)l;
      const float em = sf_em_px(g_dpi);
      LONG fw = 16, fh = 39; /* a 96-dpi thick frame, if the rects fail */
      RECT orc, crc;
      if(GetWindowRect(h, &orc) && GetClientRect(h, &crc))
      {
        fw = (orc.right - orc.left) - (crc.right - crc.left);
        fh = (orc.bottom - orc.top) - (crc.bottom - crc.top);
      }
      mmi->ptMinTrackSize.x = (LONG)(SF_WIN_MIN_EM_X * em) + fw;
      mmi->ptMinTrackSize.y = (LONG)(SF_WIN_MIN_EM_Y * em) + fh;
      return 0;
    }
    case WM_DPICHANGED:
    {
      /* Per-monitor DPI changed (the process is DPI-aware when the host is).
       * Adopt Windows' suggested rect, then rebuild the font/style between
       * frames in the modal loop. */
      RECT *pr = (RECT *)l;
      if(pr)
        SetWindowPos(h, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      g_pending_dpi = (int)HIWORD(w); /* Y (and X) DPI */
      g_dirty = 1;
      return 0;
    }
    case WM_ERASEBKGND:
      return 1; /* ImGui/D3D paints the client area; skip GDI erase. */
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);
      EndPaint(h, &ps);
      g_dirty = 1;
      return 0;
    }
    case WM_SYSCOMMAND:
      if((w & 0xfff0) == SC_KEYMENU)
        return 0; /* Disable the ALT menu. */
      break;
    case WM_TIMER:
      if(w == SF_TMR_PREVIEW)
      {
        if(!g_ctx.preview_dirty)
        {
          KillTimer(h, SF_TMR_PREVIEW);
          g_timer_on = 0;
        }
        else if(GetTickCount64() >= g_ctx.preview_at)
        {
          g_dirty = 1; /* debounce elapsed: render now */
          KillTimer(h, SF_TMR_PREVIEW);
          g_timer_on = 0;
        }
        return 0;
      }
      break;
    case WM_CLOSE:
      g_done = 1;
      g_result = 0;
      g_dirty = 1;
      return 0;
    case WM_DESTROY:
      /* No PostQuitMessage: we drive the loop ourselves and must not leave a
         stray WM_QUIT in the host thread's queue. */
      g_done = 1;
      g_dirty = 1;
      return 0;
    default:
      break;
  }
  return DefWindowProcA(h, m, w, l);
}

/* ------------------------------------------------------------------------- */
/* theme + presentation (phase B4)                                            */
/* ------------------------------------------------------------------------- */

/* A soft, flat, Win11-ish theme. Geometry is expressed at 96 dpi and scaled by
 * sf_imgui_apply_style(), so re-applying never accumulates. Every ImGuiCol is
 * set so no stale "dark" default survives a switch to light. */
static void sf_imgui_style_apply(ImGuiStyle &s, int light)
{
  const ImVec4 accent    = sf_accent(light);
  const ImVec4 accent_hi = light ? ImVec4(0.106f, 0.494f, 0.831f, 1.0f)
                                 : ImVec4(0.420f, 0.812f, 1.000f, 1.0f);
  const ImVec4 accent_lo = light ? ImVec4(0.000f, 0.320f, 0.600f, 1.0f)
                                 : ImVec4(0.216f, 0.678f, 0.925f, 1.0f);
  ImVec4 *c = s.Colors;

  /* Geometry (96 dpi bases). */
  s.WindowRounding = 0.0f; /* the real frame is drawn/rounded by DWM */
  s.ChildRounding = 6.0f;
  s.FrameRounding = 4.0f;
  s.PopupRounding = 6.0f;
  s.ScrollbarRounding = 9.0f;
  s.GrabRounding = 4.0f;
  s.TabRounding = 4.0f;
  s.WindowBorderSize = 0.0f;
  s.ChildBorderSize = 1.0f;
  s.FrameBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.WindowPadding = ImVec2(12.0f, 12.0f);
  s.FramePadding = ImVec2(8.0f, 4.0f);
  s.ItemSpacing = ImVec2(8.0f, 6.0f);
  s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  s.ScrollbarSize = 12.0f;
  s.GrabMinSize = 12.0f;
  s.IndentSpacing = 18.0f;

  /* Colours. */
  c[ImGuiCol_Text]                  = light ? ImVec4(0.10f, 0.10f, 0.10f, 1.00f) : ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
  c[ImGuiCol_TextDisabled]          = light ? ImVec4(0.44f, 0.44f, 0.44f, 1.00f) : ImVec4(0.58f, 0.58f, 0.58f, 1.00f);
  c[ImGuiCol_WindowBg]              = light ? ImVec4(0.949f, 0.949f, 0.949f, 1.00f) : ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
  c[ImGuiCol_ChildBg]               = light ? ImVec4(0.988f, 0.988f, 0.988f, 1.00f) : ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
  c[ImGuiCol_PopupBg]               = light ? ImVec4(0.996f, 0.996f, 0.996f, 1.00f) : ImVec4(0.165f, 0.165f, 0.165f, 0.98f);
  c[ImGuiCol_Border]                = light ? ImVec4(0.835f, 0.835f, 0.835f, 1.00f) : ImVec4(0.263f, 0.263f, 0.263f, 1.00f);
  c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg]               = light ? ImVec4(0.988f, 0.988f, 0.988f, 1.00f) : ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
  c[ImGuiCol_FrameBgHovered]        = light ? ImVec4(0.949f, 0.969f, 0.996f, 1.00f) : ImVec4(0.243f, 0.243f, 0.243f, 1.00f);
  c[ImGuiCol_FrameBgActive]         = light ? ImVec4(0.898f, 0.929f, 0.973f, 1.00f) : ImVec4(0.278f, 0.278f, 0.278f, 1.00f);
  c[ImGuiCol_TitleBg]               = light ? ImVec4(0.925f, 0.925f, 0.925f, 1.00f) : ImVec4(0.118f, 0.118f, 0.118f, 1.00f);
  c[ImGuiCol_TitleBgActive]         = c[ImGuiCol_TitleBg];
  c[ImGuiCol_TitleBgCollapsed]      = c[ImGuiCol_TitleBg];
  c[ImGuiCol_MenuBarBg]             = light ? ImVec4(0.925f, 0.925f, 0.925f, 1.00f) : ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
  c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab]         = light ? ImVec4(0.72f, 0.72f, 0.72f, 1.00f) : ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered]  = light ? ImVec4(0.62f, 0.62f, 0.62f, 1.00f) : ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]   = light ? ImVec4(0.52f, 0.52f, 0.52f, 1.00f) : ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  c[ImGuiCol_CheckMark]             = accent;
  /* The checkmark is the checked-state indicator at all times, so the
   * selected background is only a faint accent tint: filling it with the
   * accent itself hid the same-colour checkmark whenever the mouse was
   * away (hover swaps in FrameBgHovered, which made the tick appear). */
  c[ImGuiCol_CheckboxSelectedBg]    = ImVec4(accent.x, accent.y, accent.z,
                                             light ? 0.22f : 0.30f);
  c[ImGuiCol_SliderGrab]            = accent;
  c[ImGuiCol_SliderGrabActive]      = accent_lo;
  c[ImGuiCol_Button]                = light ? ImVec4(0.898f, 0.898f, 0.898f, 1.00f) : ImVec4(0.227f, 0.227f, 0.227f, 1.00f);
  c[ImGuiCol_ButtonHovered]         = light ? ImVec4(0.851f, 0.851f, 0.851f, 1.00f) : ImVec4(0.282f, 0.282f, 0.282f, 1.00f);
  c[ImGuiCol_ButtonActive]          = light ? ImVec4(0.796f, 0.796f, 0.796f, 1.00f) : ImVec4(0.192f, 0.192f, 0.192f, 1.00f);
  c[ImGuiCol_Header]                = light ? ImVec4(0.898f, 0.898f, 0.898f, 1.00f) : ImVec4(0.196f, 0.196f, 0.196f, 1.00f);
  c[ImGuiCol_HeaderHovered]         = light ? ImVec4(0.855f, 0.855f, 0.855f, 1.00f) : ImVec4(0.251f, 0.251f, 0.251f, 1.00f);
  c[ImGuiCol_HeaderActive]          = light ? ImVec4(0.812f, 0.812f, 0.812f, 1.00f) : ImVec4(0.298f, 0.298f, 0.298f, 1.00f);
  c[ImGuiCol_Separator]             = c[ImGuiCol_Border];
  c[ImGuiCol_SeparatorHovered]      = accent_hi;
  c[ImGuiCol_SeparatorActive]       = accent;
  c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered]     = accent_hi;
  c[ImGuiCol_ResizeGripActive]      = accent;
  c[ImGuiCol_InputTextCursor]       = c[ImGuiCol_Text];
  c[ImGuiCol_Tab]                   = c[ImGuiCol_Header];
  c[ImGuiCol_TabHovered]            = c[ImGuiCol_HeaderHovered];
  c[ImGuiCol_TabSelected]           = c[ImGuiCol_HeaderActive];
  c[ImGuiCol_TabSelectedOverline]   = accent;
  c[ImGuiCol_TabDimmed]             = c[ImGuiCol_Tab];
  c[ImGuiCol_TabDimmedSelected]     = c[ImGuiCol_TabSelected];
  c[ImGuiCol_TabDimmedSelectedOverline] = accent;
  c[ImGuiCol_PlotLines]             = accent;
  c[ImGuiCol_PlotLinesHovered]      = accent_hi;
  c[ImGuiCol_PlotHistogram]         = accent;
  c[ImGuiCol_PlotHistogramHovered]  = accent_hi;
  c[ImGuiCol_TableHeaderBg]         = c[ImGuiCol_Header];
  c[ImGuiCol_TableBorderStrong]     = c[ImGuiCol_Border];
  c[ImGuiCol_TableBorderLight]      = light ? ImVec4(0.90f, 0.90f, 0.90f, 1.00f) : ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
  c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt]         = light ? ImVec4(0, 0, 0, 0.03f) : ImVec4(1, 1, 1, 0.03f);
  c[ImGuiCol_TextLink]              = accent;
  c[ImGuiCol_TextSelectedBg]        = light ? ImVec4(0.00f, 0.40f, 0.75f, 0.30f) : ImVec4(0.30f, 0.76f, 1.00f, 0.35f);
  c[ImGuiCol_TreeLines]             = c[ImGuiCol_Border];
  c[ImGuiCol_DragDropTarget]        = accent_hi;
  c[ImGuiCol_DragDropTargetBg]      = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_UnsavedMarker]         = ImVec4(1.00f, 0.70f, 0.20f, 1.00f);
  c[ImGuiCol_NavCursor]             = accent;
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
  c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.05f, 0.05f, 0.05f, 0.40f);
}

static void sf_imgui_apply_style(UINT dpi)
{
  ImGuiStyle &s = ImGui::GetStyle();
  sf_imgui_style_apply(s, g_theme);
  s.FontSizeBase = SF_UI_FONT_BASE_PX;
  s.FontScaleMain = 1.0f;
  s.FontScaleDpi = (float)dpi / 96.0f;
  s.ScaleAllSizes((float)dpi / 96.0f);
}

/* Load Segoe UI once, at the DPI-independent base size (falling back to the
 * built-in font). DPI is handled per frame via style.FontScaleDpi above, so this
 * is only ever called at dialog setup. */
static void sf_imgui_load_font(void)
{
  ImGuiIO &io = ImGui::GetIO();
  char path[MAX_PATH];
  int loaded = 0;

  io.Fonts->ClearFonts();
  io.FontDefault = NULL; /* use Fonts[0] */

  if(GetWindowsDirectoryA(path, MAX_PATH) > 0 &&
     strlen(path) < sizeof(path) - 20)
  {
    strncat(path, "\\Fonts\\segoeui.ttf", sizeof(path) - strlen(path) - 1);
    if(io.Fonts->AddFontFromFileTTF(path, SF_UI_FONT_BASE_PX) != NULL)
      loaded = 1;
  }
  if(!loaded)
    io.Fonts->AddFontDefault();
}

/* The window's DPI. GetDpiForWindow is Win10 1607+; load it dynamically so the
 * import stays optional. Fall back to the DC (system DPI). */
static UINT sf_imgui_query_dpi(HWND hwnd)
{
  typedef UINT(WINAPI *PFN_GetDpiForWindow)(HWND);
  HMODULE u = GetModuleHandleA("user32.dll");
  if(u && hwnd)
  {
    PFN_GetDpiForWindow fn =
        (PFN_GetDpiForWindow)((void *)GetProcAddress(u, "GetDpiForWindow"));
    if(fn)
    {
      UINT d = fn(hwnd);
      if(d >= 48) return d;
    }
  }
  if(hwnd)
  {
    HDC dc = GetDC(hwnd);
    if(dc)
    {
      UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
      ReleaseDC(hwnd, dc);
      if(d >= 48) return d;
    }
  }
  return 96;
}

/* Windows 11 chrome: rounded corners + an immersive (dark/light) title bar,
 * via DwmSetWindowAttribute loaded from dwmapi.dll (keeps the import table
 * clean). No-ops on older Windows, where the attributes are unknown. */
static void sf_imgui_dwm_chrome(HWND hwnd, int light)
{
  typedef HRESULT(WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
  HMODULE dwm;
  PFN_DwmSetWindowAttribute set;
  BOOL dark;
  int corner = 2; /* DWMWCP_ROUND */
  COLORREF cap, txt;

  if(!hwnd) return;
  dwm = LoadLibraryA("dwmapi.dll");
  if(!dwm) return;
  set = (PFN_DwmSetWindowAttribute)((void *)GetProcAddress(dwm, "DwmSetWindowAttribute"));
  if(set)
  {
    dark = light ? FALSE : TRUE;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE == 20 on Win10 20H1+/Win11, 19 before. */
    if(FAILED(set(hwnd, 20u, &dark, sizeof dark)))
      set(hwnd, 19u, &dark, sizeof dark);
    /* DWMWA_WINDOW_CORNER_PREFERENCE (Win11). */
    set(hwnd, 33u, &corner, sizeof corner);
    /* DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR (Win11 22000). */
    cap = light ? RGB(0xF3, 0xF3, 0xF3) : RGB(0x1E, 0x1E, 0x1E);
    txt = light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xF0, 0xF0, 0xF0);
    set(hwnd, 35u, &cap, sizeof cap);
    set(hwnd, 36u, &txt, sizeof txt);
  }
  FreeLibrary(dwm);
}

extern "C" short sf_run_dialog_imgui(void *owner, FilterRecordPtr fr,
                                    sf_params_block *block,
                                    const float *src, int sw, int sh)
{
  HWND howner = (HWND)owner;
  HWND hwnd;
  sf_d3d11 *d3d;
  RECT rc;
  WNDCLASSEXA wc;
  UINT dpi0;
  int cw, ch;
  const char *cls = "SpektrafilmImGuiWnd";

  g_done = 0;
  g_result = 0;
  g_dirty = 1;
  g_need_resize = 0;
  g_resize_w = 0;
  g_resize_h = 0;
  g_hwnd = NULL;
  g_pending_dpi = 0;
  g_style_dirty = 0;

  ZeroMemory(&g_ctx, sizeof(g_ctx));
  g_ctx.out = block;
  g_ctx.work = *block; /* edit a copy; commit on OK only */
  g_ctx.hal_profile = 0;
  g_ctx.src = src;
  g_ctx.sw = sw;
  g_ctx.sh = sh;
  g_ctx.fr = fr;
  sf_detect_document_profile(fr, &g_ctx.detect); /* matches the render's start path */
  /* When auto-detect is on, start the transfer selection on the profile's own
   * TRC (parity with the native sf_setup); the user can still override it. */
  if(g_ctx.work.input_profile_auto && g_ctx.detect.ok)
    g_ctx.work.input_transfer = g_ctx.detect.transfer;
  g_ctx.view_fit = 1;
  g_ctx.preview_dirty = 1;
  g_ctx.preview_at = 0;
  sf_preview_set_pan(0.0, 0.0); /* each invocation starts centred */

  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = sf_imgui_wndproc;
  wc.hInstance = g_sf_hinst;
  wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
  wc.lpszClassName = cls;
  RegisterClassExA(&wc);

  dpi0 = sf_imgui_query_dpi(howner);
  {
    /* Default size: 50 x 33 em of the UI font, so the dialog keeps its
     * proportions at any DPI. Before the window exists, the owner window's
     * monitor DPI is the best estimate of where it will land. */
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                        WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const float em = sf_em_px(dpi0);
    MONITORINFO mi;
    HMONITOR hm;
    POINT ptop = { 0, 0 };
    RECT ro;
    int w = (int)(SF_WIN_DEF_EM_X * em);
    int h = (int)(SF_WIN_DEF_EM_Y * em);
    int x, y, have_mi = 0;

    /* client size -> outer size */
    ZeroMemory(&ro, sizeof ro);
    ro.right = w;
    ro.bottom = h;
    AdjustWindowRectEx(&ro, style, FALSE, 0);
    w = ro.right - ro.left;
    h = ro.bottom - ro.top;

    /* never bigger than the work area of the monitor that will host it, so a
     * large default on a small or heavily scaled display still opens whole */
    ZeroMemory(&mi, sizeof mi);
    mi.cbSize = sizeof mi;
    hm = howner ? MonitorFromWindow(howner, MONITOR_DEFAULTTONEAREST)
                : MonitorFromPoint(ptop, MONITOR_DEFAULTTOPRIMARY);
    if(hm && GetMonitorInfoA(hm, &mi))
    {
      have_mi = 1;
      if(w > mi.rcWork.right - mi.rcWork.left) w = mi.rcWork.right - mi.rcWork.left;
      if(h > mi.rcWork.bottom - mi.rcWork.top) h = mi.rcWork.bottom - mi.rcWork.top;
    }

    if(howner && GetWindowRect(howner, &ro))
    {
      x = ro.left + ((ro.right - ro.left) - w) / 2;
      y = ro.top + ((ro.bottom - ro.top) - h) / 2;
    }
    else if(have_mi)
    {
      x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
      y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    }
    else
    {
      x = 100;
      y = 80;
    }
    if(have_mi) /* keep it on screen */
    {
      if(x + w > mi.rcWork.right) x = mi.rcWork.right - w;
      if(y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
      if(x < mi.rcWork.left) x = mi.rcWork.left;
      if(y < mi.rcWork.top) y = mi.rcWork.top;
    }

    g_dpi = dpi0; /* WM_GETMINMAXINFO already fires inside CreateWindow */
    hwnd = CreateWindowExA(0, cls, SF_PRODUCT_NAME, style, x, y, w, h, howner,
                           NULL, g_sf_hinst, NULL);
  }
  if(!hwnd)
  {
    UnregisterClassA(cls, g_sf_hinst);
    return -1;
  }
  g_hwnd = hwnd;

  /* The estimate can miss (owner on another monitor, or a host that is not
   * per-monitor aware): re-query the window's own DPI and resize to the em
   * default at that DPI, before the swap chain and ImGui take their initial
   * size from the client rect. */
  {
    UINT dpi1 = sf_imgui_query_dpi(hwnd);
    g_dpi = dpi1;
    if(dpi1 != dpi0 && GetWindowRect(hwnd, &rc))
      SetWindowPos(hwnd, NULL, 0, 0,
                   MulDiv(rc.right - rc.left, (int)dpi1, (int)dpi0),
                   MulDiv(rc.bottom - rc.top, (int)dpi1, (int)dpi0),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  GetClientRect(hwnd, &rc);
  cw = rc.right - rc.left;
  ch = rc.bottom - rc.top;
  if(cw < 1) cw = (int)(SF_WIN_DEF_EM_X * sf_em_px(g_dpi));
  if(ch < 1) ch = (int)(SF_WIN_DEF_EM_Y * sf_em_px(g_dpi));

  d3d = sf_d3d11_create(hwnd, cw, ch);
  if(!d3d)
  {
    g_hwnd = NULL;
    DestroyWindow(hwnd);
    UnregisterClassA(cls, g_sf_hinst);
    return -1; /* D3D11/WARP unavailable -> caller falls back to native. */
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  {
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL; /* Don't drop an imgui.ini next to the host. */
    /* European number format: this is what makes a typed '.' or ',' in a
     * number field come out as ','. Display and parsing are handled by
     * sf_num_txt()/sf_numf() next to the settings rows. */
    ImGui::GetPlatformIO().Platform_LocaleDecimalPoint = ',';
  }
  {
    const char *theme_env = getenv("SPEKTRAFILM_GUI_THEME");
    if(theme_env && lstrcmpiA(theme_env, "light") == 0)
      g_theme = 1;
    else if(theme_env && lstrcmpiA(theme_env, "dark") == 0)
      g_theme = 0;
  }
  g_dpi = sf_imgui_query_dpi(hwnd);
  sf_imgui_apply_style(g_dpi);
  sf_imgui_load_font();
  sf_imgui_dwm_chrome(hwnd, g_theme == 1);

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init((ID3D11Device *)sf_d3d11_device(d3d),
                      (ID3D11DeviceContext *)sf_d3d11_context(d3d));
  g_ctx.d3d = d3d;
  g_ctx.tex = sf_d3d11_tex_create(d3d);

  if(howner)
    EnableWindow(howner, FALSE);

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  while(!g_done)
  {
    MSG msg;
    BOOL any = FALSE;

    while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
      any = TRUE;
      if(msg.message == WM_QUIT)
      {
        g_done = 1;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if(g_done)
      break;

    if(any)
      g_dirty = 1;

    if(g_need_resize)
    {
      if(!IsIconic(hwnd))
        sf_d3d11_resize(d3d, g_resize_w, g_resize_h);
      g_need_resize = 0;
      g_dirty = 1;
    }

    /* Apply a DPI change between frames (rescales the style and the per-monitor
     * font scale factor). */
    if(g_pending_dpi)
    {
      UINT nd = (UINT)g_pending_dpi;
      g_pending_dpi = 0;
      if(nd != g_dpi)
      {
        g_dpi = nd;
        sf_imgui_apply_style(g_dpi);
        g_dirty = 1;
      }
    }

    if(g_dirty)
    {
      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();
      sf_imgui_build(&g_ctx);
      ImGui::Render();
      if(g_theme == 1)
        sf_d3d11_begin(d3d, 0.949f, 0.949f, 0.949f);
      else
        sf_d3d11_begin(d3d, 0.118f, 0.118f, 0.118f);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
      sf_d3d11_present(d3d);
      g_dirty = 0;
    }

    /* The theme button flips g_style_dirty while the frame above is being built;
     * apply the new style between frames and force one more render so the whole
     * UI (and the DWM chrome) reflects it. */
    if(g_style_dirty)
    {
      sf_imgui_apply_style(g_dpi);
      sf_imgui_dwm_chrome(hwnd, g_theme == 1);
      g_style_dirty = 0;
      g_dirty = 1;
    }

    if(!g_dirty)
      WaitMessage(); /* Idle until the next input message. */
  }

  if(g_result == 1)
    *block = g_ctx.work; /* commit */

  if(g_timer_on)
  {
    KillTimer(hwnd, SF_TMR_PREVIEW);
    g_timer_on = 0;
  }
  g_hwnd = NULL;

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  if(g_ctx.tex) sf_d3d11_tex_destroy(g_ctx.tex);
  sf_d3d11_destroy(d3d);
  DestroyWindow(hwnd);
  UnregisterClassA(cls, g_sf_hinst);

  if(howner)
  {
    EnableWindow(howner, TRUE);
    SetActiveWindow(howner);
    SetForegroundWindow(howner);
  }

  return (short)g_result;
}
