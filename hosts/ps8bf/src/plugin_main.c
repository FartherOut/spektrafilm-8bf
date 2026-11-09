/* SPDX-License-Identifier: GPL-3.0-or-later */
/* plugin_main.c — the .8bf entry point (Windows).
 *
 * Translates the six filter selectors into the portable selector bodies in
 * filter_loop.c, installs the ImGui parameter dialog (which filter_loop runs
 * during Continue, once the frame is available), and stores/restores the
 * parameter handle. DllMain stashes the module instance so the embedded data
 * pack (Win32 RCDATA resources, see assets_win.c) can be looked up.
 *
 * Built MSVC-only (see CMakeLists.txt / build.ps1): static CRT, imports only
 * kernel32/user32/gdi32, exports exactly PluginMain.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "pifilter.h"
#include "params.h"
#include "filter_loop.h"

#include "assets_win.h"
#include "sf_gui.h"
#include "sf_parallel.h"
#include "version.h"
#include "build_date.h"

/* Engine scratch sp_chain_render allocates per image pixel: raw, corr, cmy,
 * lraw, pcmy and the blur working copy (7 interleaved buffers x 3 floats) plus
 * the coupler scratch plane w1 (1 float) = 22 floats = 88 B/px; each blur call
 * transiently allocates one more w*h plane (+4 B/px). filter_loop holds its
 * float tile (3 floats = 12 B/px) on top. Rounded up to 128. Declared to the
 * host in Prepare (see sf_prepare) so it can free the space before Start. */
#define SF_WORK_BYTES_PER_PIXEL 128

/* selectors are called on the host's UI thread; a single filter invocation at
 * a time is the 8BF contract, so a module-global is the normal pattern. */
HINSTANCE g_sf_hinst = NULL; /* module instance; read by sf_gui_imgui.cpp */

static HWND sf_active_owner(void)
{
  HWND w = GetActiveWindow();
  if(!w) w = GetForegroundWindow();
  return w;
}

/* Point the filter loop's diagnostic transcript at %TEMP%\spektrafilm.log.
 * Temporary while the Affinity Photo buffer handshake is being pinned down:
 * a failed render can then be diagnosed from the file the user sends back. */
static void sf_init_log_path(void)
{
  char dir[MAX_PATH];
  char path[MAX_PATH];
  DWORD n = GetTempPathA(MAX_PATH, dir);
  if(n == 0 || n >= MAX_PATH) return;
  /* path is the size sf_filter_set_log_path() stores, so a temp path near
   * MAX_PATH leaves no room for the file name: skip the transcript instead of
   * writing to a silently truncated path. */
  if(snprintf(path, sizeof path, "%sspektrafilm.log", dir) >= (int)sizeof path)
    return;
  sf_filter_set_log_path(path);
}

/* Human-readable selector name for the transcript. */
static const char *sf_selector_name(int16 selector)
{
  switch(selector)
  {
    case filterSelectorAbout:      return "About";
    case filterSelectorParameters: return "Parameters";
    case filterSelectorPrepare:    return "Prepare";
    case filterSelectorStart:      return "Start";
    case filterSelectorContinue:   return "Continue";
    case filterSelectorFinish:     return "Finish";
    default:                       return "unknown";
  }
}

/* One banner per loaded module: product/version/build, process id and the full
 * path of the DLL the host actually loaded. This is what identifies a stale
 * install when a fix appears not to take. */
static void sf_log_banner(void)
{
  static int done;
  char mod[MAX_PATH];
  if(done) return;
  done = 1;
  mod[0] = 0;
  if(g_sf_hinst) GetModuleFileNameA(g_sf_hinst, mod, (DWORD)sizeof mod);
  sf_filter_log_write("=== %s %s build %s pid=%lu ===", SF_PRODUCT_NAME,
                      SF_VERSION, SF_BUILD_STAMP,
                      (unsigned long)GetCurrentProcessId());
  sf_filter_log_write("module=%s", mod);
}

/* ------------------------------------------------------------- about box */
/* Reachable from the host's About selector (Photoshop shows it in the filter
 * menu). USER32's MessageBoxA only, so the import table is unaffected. This is
 * deliberately a pointer, not the About screen: the full credits and licence
 * text lives in the modal behind the About button of the plug-in's own window
 * (sf_gui_imgui.cpp), where it is wrapped and styled. The host can invoke this
 * selector at any time the plug-in is loaded - no window, no D3D device - so
 * the answer stays bare and always works. */
static OSErr sf_about(void)
{
  char msg[256];
  snprintf(msg, sizeof msg,
           "Build %s\n\n"
           "Spectral film simulations of analogue photography.\n\n"
           "Full credits and licence: the About button in the\n"
           "spektrafilm window.",
           sf_build_date_iso());
  MessageBoxA(sf_active_owner(), msg, SF_PRODUCT_NAME " " SF_VERSION,
              MB_OK | MB_ICONINFORMATION);
  return noErr;
}

/* ----------------------------------------------------- selector bodies -- */
/* filterSelectorParameters no longer shows the modal dialog: a preview of the
 * user's image needs pixels, which the host only hands over during the
 * Start/Continue loop. Here we just make sure a valid settings block exists in
 * the host's handle; the dialog runs later (see sf_dialog_bridge below). */
static OSErr sf_parameters(FilterRecordPtr fr)
{
  HandleProcs *hp;
  Handle h;
  sf_params_block *b;

  if(!fr) return filterBadParameters;
  hp = fr->handleProcs;
  h = fr->parameters;
  sf_filter_mark_interactive(); /* the dialog runs later, from Continue */
  if(!hp || !hp->lockProc) return noErr;

  if(!h)
  {
    if(!hp->newProc) return noErr;
    h = hp->newProc((int32)sizeof(sf_params_block));
    if(!h) return memFullErr;
    fr->parameters = h;
    b = (sf_params_block *)hp->lockProc(h, FALSE);
    if(b) sf_params_init(b);
    if(hp->unlockProc) hp->unlockProc(h);
    return noErr;
  }

  /* A parameters handle persisted by an older build can be smaller than the
   * current block (it grew with the Phase 2 fields). Grow it before we
   * validate/init so sf_params_init never writes past the host's allocation. */
  if(hp->getSizeProc && hp->setSizeProc &&
     hp->getSizeProc(h) < (int32)sizeof(sf_params_block))
    hp->setSizeProc(h, (int32)sizeof(sf_params_block));

  b = (sf_params_block *)hp->lockProc(h, FALSE);
  if(b && sf_params_validate(b) != 0) sf_params_init(b);
  if(hp->unlockProc) hp->unlockProc(h);
  return noErr;
}

/* The dialog hook filter_loop.c calls from filterSelectorContinue once it has
 * decoded the frame. Runs the modal dialog on the host's UI thread and, on OK,
 * writes the (possibly edited) settings back into the host's parameters handle
 * so they persist for the next invocation. Returns 1 to render, 0 on cancel. */
static int sf_dialog_bridge(void *ctx, FilterRecordPtr fr, sf_params_block *b,
                            const float *src, int w, int h)
{
  (void)ctx;

  if(!sf_run_params_dialog(sf_active_owner(), fr, b, src, w, h)) return 0;

  if(fr && fr->handleProcs && fr->parameters && fr->handleProcs->lockProc)
  {
    HandleProcs *hp = fr->handleProcs;
    sf_params_block *store;
    if(hp->getSizeProc && hp->setSizeProc &&
       hp->getSizeProc(fr->parameters) < (int32)sizeof(sf_params_block))
      hp->setSizeProc(fr->parameters, (int32)sizeof(sf_params_block));
    store = (sf_params_block *)hp->lockProc(fr->parameters, FALSE);
    if(store) memcpy(store, b, sizeof *store);
    if(hp->unlockProc) hp->unlockProc(fr->parameters);
  }
  return 1;
}

static OSErr sf_prepare(FilterRecordPtr fr)
{
  int64 rw, rh, sample, inout, work;
  int planes;
  VRect sel;

  if(!fr) return filterBadParameters;

  /* The vertical slice renders the whole selection as ONE tile (see
   * filter_loop.c), so the host must be able to hand us both the input and
   * the output buffer at full selection size. FilterRecord::bufferSpace64 is
   * the plug-in's own working-set declaration (the SDK: "specify how much
   * buffer space it needs"); maxSpace64 is the host's per-invocation budget
   * (input + output + mask + bufferSpace).
   *
   * Use the same rect resolution as Start: a 32-bit host may carry the
   * selection only in filterRect32, and under-reporting the buffer here would
   * leave the host with no pixel buffer to hand over. */
  sf_filter_selection_rect(fr, &sel);
  rw = (int64)sel.right - (int64)sel.left;
  rh = (int64)sel.bottom - (int64)sel.top;
  if(rw <= 0 || rh <= 0) return noErr; /* empty selection: nothing to render */

  planes = fr->planes;
  if(planes < 3) planes = 3;
  sample = (fr->depth == 16) ? 2 : 1;
  inout = rw * rh * (int64)planes * sample;              /* per buffer */
  work = rw * rh * (int64)SF_WORK_BYTES_PER_PIXEL;       /* our scratch */

  /* Ask the host to free up our working set before filterSelectorStart. */
  fr->bufferSpace64 = work;
  fr->bufferSpace = (work > (int64)0x7fffffff) ? (int32)0x7fffffff : (int32)work;

  if(fr->maxSpace64 > 0 && fr->maxSpace64 < inout * 2 + work)
  {
    static Str255 msg;
    static const char text[] =
        "spektrafilm: the selection is too large for the host's filter "
        "buffer. Try a smaller selection.";
    const size_t n = sizeof text - 1;
    memcpy(msg + 1, text, n);
    msg[0] = (unsigned char)n;
    fr->errorString = &msg;
    return errReportString;
  }
  return noErr;
}

/* ---------------------------------------------------------------- entry -- */
DLLExport void PASCAL PluginMain(const int16 selector, FilterRecordPtr fr,
                                 intptr_t *data, int16 *result)
{
  OSErr err = noErr;
  (void)data;

  if(!result) return;

  sf_parallel_install(); /* honour SPEKTRAFILM_THREADS; safe to call repeatedly */
  sf_filter_set_dialog_hook(sf_dialog_bridge, NULL); /* dialog runs in Continue */

  sf_filter_log_begin();
  sf_log_banner();
  sf_filter_log_write("selector=%s(%d) fr=%p", sf_selector_name(selector),
                      (int)selector, (void *)fr);

  switch(selector)
  {
    case filterSelectorAbout:
      err = sf_about();
      break;

    case filterSelectorParameters:
      err = sf_parameters(fr);
      break;

    case filterSelectorPrepare:
      err = sf_prepare(fr);
      break;

    case filterSelectorStart:
      err = sf_filter_start(fr);
      break;

    case filterSelectorContinue:
      err = sf_filter_continue(fr);
      break;

    case filterSelectorFinish:
      err = sf_filter_finish(fr);
      break;

    default:
      err = filterBadParameters;
      break;
  }

  /* Surface real errors (not a user cancel) to the user. errReportString is
   * excluded: the host reports FilterRecord::errorString itself. */
  if(err != noErr && err != userCanceledErr && err != filterBadMode &&
     err != filterBadParameters && err != memFullErr && err != errReportString)
  {
    MessageBoxA(sf_active_owner(), sf_filter_last_error(), SF_PRODUCT_NAME,
                MB_OK | MB_ICONEXCLAMATION);
  }
  else if((err == filterBadMode || err == filterBadParameters) &&
          sf_filter_last_error()[0])
  {
    MessageBoxA(sf_active_owner(), sf_filter_last_error(), SF_PRODUCT_NAME,
                MB_OK | MB_ICONEXCLAMATION);
  }

  *result = (int16)err;
}

/* --------------------------------------------------------------- DllMain */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
  (void)reserved;
  if(reason == DLL_PROCESS_ATTACH)
  {
    g_sf_hinst = hinst;
    sf_asset_set_hinstance(hinst);
    sf_init_log_path();
  }
  else if(reason == DLL_PROCESS_DETACH)
  {
    sf_filter_reset();
    sf_filter_log_end();
  }
  return TRUE;
}