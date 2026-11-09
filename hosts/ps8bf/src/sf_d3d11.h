/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_d3d11.h — D3D11 device/swapchain wrapper (dynamic load, C API). */
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sf_d3d11 sf_d3d11;

/* An RGBA8 shader-resource texture (the dialog's preview). The opaque handle is
 * created once per dialog and re-uploaded as the preview changes. */
typedef struct sf_tex sf_tex;

sf_d3d11 *sf_d3d11_create(HWND hwnd, int width, int height);
void      sf_d3d11_destroy(sf_d3d11 *d);
int       sf_d3d11_resize(sf_d3d11 *d, int width, int height);
void      sf_d3d11_begin(sf_d3d11 *d, float r, float g, float b);
void      sf_d3d11_present(sf_d3d11 *d);
void     *sf_d3d11_device(sf_d3d11 *d);  /* ID3D11Device* */
void     *sf_d3d11_context(sf_d3d11 *d); /* ID3D11DeviceContext* */

/* Create an (initially empty) RGBA8 texture associated with *d*. Returns NULL
 * if the GPU stack is unavailable. */
sf_tex   *sf_d3d11_tex_create(sf_d3d11 *d);
/* Upload w*h RGBA8 pixels (top-down). (Re)allocates if the size changed.
 * Returns 1 on success. */
int       sf_d3d11_tex_update(sf_tex *t, int w, int h, const unsigned char *rgba);
void      sf_d3d11_tex_destroy(sf_tex *t);
void     *sf_d3d11_tex_srv(sf_tex *t); /* ID3D11ShaderResourceView* (ImTextureID) */

#ifdef __cplusplus
}
#endif
