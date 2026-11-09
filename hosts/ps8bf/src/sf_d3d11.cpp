/* sf_d3d11.cpp — D3D11 device + DXGI swap chain + RTV (see sf_d3d11.h).
 *
 * Decision D8: the GPU stack is reached through LoadLibrary/GetProcAddress, so
 * the shipping .8bf's static import table stays at KERNEL32/USER32/GDI32 (no
 * d3d11/dxgi/d3dcompiler import). "d3dcompiler_47.dll" is opened
 * lazily by sf_d3d_compile_shim(), which imgui_impl_dx11.cpp calls instead of
 * D3DCompile() (via the D3DCompile=sf_d3d_compile_shim compile definition).
 *
 * Built as C++ so the mingw-w64/Windows-SDK COM interface class methods can be
 * called directly and __uuidof() is available. WARP is tried as a fallback for
 * hardware device creation, so the dialog still comes up on GPU-less machines,
 * RDP and VMs.
 */
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <stdlib.h>
#include <string.h>

#include "sf_d3d11.h"

/* ------------------------------------------------------------------------- */
/* dynamic loader shims                                                       */
/* ------------------------------------------------------------------------- */

typedef HRESULT(WINAPI *PFN_D3D11CreateDeviceAndSwapChain)(
    IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);

typedef HRESULT(WINAPI *PFN_D3DCompile)(const void *data, SIZE_T data_size,
    const char *filename, const D3D_SHADER_MACRO *defines, ID3DInclude *include,
    const char *entrypoint, const char *target, UINT sflags, UINT eflags,
    ID3DBlob **shader, ID3DBlob **error_messages);

static HMODULE s_hD3D11 = NULL;
static HMODULE s_hD3DCompiler = NULL;
static PFN_D3D11CreateDeviceAndSwapChain s_pCreateDeviceAndSwapChain = NULL;
static PFN_D3DCompile s_pD3DCompile = NULL;

static int sf_d3d11_load(void)
{
  if(s_pCreateDeviceAndSwapChain) return 1;
  s_hD3D11 = LoadLibraryA("d3d11.dll");
  if(!s_hD3D11) return 0;
  s_pCreateDeviceAndSwapChain = (PFN_D3D11CreateDeviceAndSwapChain)(
      (void *)GetProcAddress(s_hD3D11, "D3D11CreateDeviceAndSwapChain"));
  if(!s_pCreateDeviceAndSwapChain) return 0;
  return 1;
}

/* Called by imgui_impl_dx11.cpp (renamed from D3DCompile). Loads
 * d3dcompiler_47.dll on first use; returns E_FAIL if unavailable so the caller
 * treats it like a device failure. */
extern "C" HRESULT WINAPI sf_d3d_compile_shim(const void *data, SIZE_T data_size,
    const char *filename, const D3D_SHADER_MACRO *defines, ID3DInclude *include,
    const char *entrypoint, const char *target, UINT sflags, UINT eflags,
    ID3DBlob **shader, ID3DBlob **error_messages)
{
  if(!s_pD3DCompile)
  {
    if(!s_hD3DCompiler) s_hD3DCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if(!s_hD3DCompiler) return E_FAIL;
    s_pD3DCompile = (PFN_D3DCompile)((void *)GetProcAddress(s_hD3DCompiler, "D3DCompile"));
    if(!s_pD3DCompile) return E_FAIL;
  }
  return s_pD3DCompile(data, data_size, filename, defines, include,
                       entrypoint, target, sflags, eflags, shader, error_messages);
}

/* ------------------------------------------------------------------------- */
/* device + swap chain + RTV                                                  */
/* ------------------------------------------------------------------------- */

struct sf_d3d11
{
  HWND hwnd;
  ID3D11Device *device;
  ID3D11DeviceContext *context;
  IDXGISwapChain *swap;
  ID3D11RenderTargetView *rtv;
  int w, h;
};

static void sf_d3d11_release_rtv(sf_d3d11 *d)
{
  if(d->rtv)
  {
    d->rtv->Release();
    d->rtv = NULL;
  }
}

static int sf_d3d11_create_rtv(sf_d3d11 *d)
{
  ID3D11Texture2D *back = NULL;
  HRESULT hr;
  sf_d3d11_release_rtv(d);
  hr = d->swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back);
  if(FAILED(hr) || !back) return 0;
  hr = d->device->CreateRenderTargetView(back, NULL, &d->rtv);
  back->Release();
  return SUCCEEDED(hr) && d->rtv != NULL;
}

static int sf_d3d11_try_device(sf_d3d11 *d, const DXGI_SWAP_CHAIN_DESC *sd,
                              D3D_DRIVER_TYPE type)
{
  static const D3D_FEATURE_LEVEL levels[] = {
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };
  D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr;
  d->swap = NULL;
  d->device = NULL;
  d->context = NULL;
  hr = s_pCreateDeviceAndSwapChain(NULL, type, NULL, 0, levels, 3,
      D3D11_SDK_VERSION, sd, &d->swap, &d->device, &got, &d->context);
  if(FAILED(hr)) return 0;
  if(!d->swap || !d->device || !d->context) return 0;
  return 1;
}

sf_d3d11 *sf_d3d11_create(HWND hwnd, int w, int h)
{
  sf_d3d11 *d;
  DXGI_SWAP_CHAIN_DESC sd;

  if(!sf_d3d11_load()) return NULL;

  d = (sf_d3d11 *)calloc(1, sizeof(sf_d3d11));
  if(!d) return NULL;
  d->hwnd = hwnd;
  d->w = (w > 0) ? w : 1;
  d->h = (h > 0) ? h : 1;

  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = (UINT)d->w;
  sd.BufferDesc.Height = (UINT)d->h;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  /* Hardware first; WARP (software) fallback for VMs/RDP/GPU-less hosts. */
  if(!sf_d3d11_try_device(d, &sd, D3D_DRIVER_TYPE_HARDWARE) &&
     !sf_d3d11_try_device(d, &sd, D3D_DRIVER_TYPE_WARP))
  {
    free(d);
    return NULL;
  }

  if(!sf_d3d11_create_rtv(d))
  {
    d->swap->Release();
    d->device->Release();
    d->context->Release();
    free(d);
    return NULL;
  }
  return d;
}

int sf_d3d11_resize(sf_d3d11 *d, int w, int h)
{
  if(!d || !d->swap) return 0;
  if(w <= 0 || h <= 0) return 0;
  d->w = w;
  d->h = h;
  sf_d3d11_release_rtv(d);
  if(FAILED(d->swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0)))
    return 0;
  return sf_d3d11_create_rtv(d);
}

void sf_d3d11_begin(sf_d3d11 *d, float r, float g, float b)
{
  D3D11_VIEWPORT vp;
  ID3D11RenderTargetView *rtv;
  if(!d || !d->context || !d->rtv) return;
  rtv = d->rtv;
  d->context->OMSetRenderTargets(1, &rtv, NULL);
  ZeroMemory(&vp, sizeof(vp));
  vp.Width = (FLOAT)d->w;
  vp.Height = (FLOAT)d->h;
  vp.MaxDepth = 1.0f;
  d->context->RSSetViewports(1, &vp);
  {
    const FLOAT clear[4] = { r, g, b, 1.0f };
    d->context->ClearRenderTargetView(rtv, clear);
  }
}

void sf_d3d11_present(sf_d3d11 *d)
{
  if(!d || !d->swap) return;
  d->swap->Present(1, 0);
}

void *sf_d3d11_device(sf_d3d11 *d) { return d ? (void *)d->device : NULL; }
void *sf_d3d11_context(sf_d3d11 *d) { return d ? (void *)d->context : NULL; }

void sf_d3d11_destroy(sf_d3d11 *d)
{
  if(!d) return;
  sf_d3d11_release_rtv(d);
  if(d->swap) d->swap->Release();
  if(d->context) d->context->Release();
  if(d->device) d->device->Release();
  free(d);
}

/* ------------------------------------------------------------------------- */
/* preview texture (RGBA8 shader resource)                                    */
/* ------------------------------------------------------------------------- */

struct sf_tex
{
  ID3D11Device *device;
  ID3D11DeviceContext *context;
  ID3D11Texture2D *tex;
  ID3D11ShaderResourceView *srv;
  int w, h;
};

static int sf_tex_realloc(sf_tex *t, int w, int h)
{
  D3D11_TEXTURE2D_DESC td;
  if(t->srv) { t->srv->Release(); t->srv = NULL; }
  if(t->tex) { t->tex->Release(); t->tex = NULL; }
  ZeroMemory(&td, sizeof(td));
  td.Width = (UINT)w;
  td.Height = (UINT)h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(t->device->CreateTexture2D(&td, NULL, &t->tex)))
    return 0;
  if(FAILED(t->device->CreateShaderResourceView(t->tex, NULL, &t->srv)))
  {
    t->tex->Release();
    t->tex = NULL;
    return 0;
  }
  t->w = w;
  t->h = h;
  return 1;
}

sf_tex *sf_d3d11_tex_create(sf_d3d11 *d)
{
  sf_tex *t;
  if(!d || !d->device || !d->context) return NULL;
  t = (sf_tex *)calloc(1, sizeof(sf_tex));
  if(!t) return NULL;
  d->device->AddRef();
  d->context->AddRef();
  t->device = d->device;
  t->context = d->context;
  return t;
}

int sf_d3d11_tex_update(sf_tex *t, int w, int h, const unsigned char *rgba)
{
  if(!t || !t->device || !t->context) return 0;
  if(w <= 0 || h <= 0 || !rgba) return 0;
  if(!t->tex || t->w != w || t->h != h)
  {
    if(!sf_tex_realloc(t, w, h)) return 0;
  }
  t->context->UpdateSubresource((ID3D11Resource *)t->tex, 0, NULL, rgba,
                                (UINT)w * 4u, 0);
  return 1;
}

void sf_d3d11_tex_destroy(sf_tex *t)
{
  if(!t) return;
  if(t->srv) t->srv->Release();
  if(t->tex) t->tex->Release();
  if(t->context) t->context->Release();
  if(t->device) t->device->Release();
  free(t);
}

void *sf_d3d11_tex_srv(sf_tex *t) { return t ? (void *)t->srv : NULL; }

