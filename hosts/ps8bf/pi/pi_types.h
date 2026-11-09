/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pi_types.h — minimal, self-contained subset of the Adobe "Photoshop API"
 * type surface that a filter (.8bf) plugin needs.
 *
 * The layout of FilterRecord (pifilter.h) is what the host (Photoshop or
 * Affinity Photo) writes into our parameter block, so the struct must match
 * the SDK's byte-for-byte (x64, #pragma pack(4)). These definitions are this
 * project's own declaration of the PUBLIC, documented layout, written from
 * Adobe's published headers as the reference (Adobe publishes
 * PIFilter.h/PITypes.h/PIGeneral.h as PUBLIC with a build/distribute
 * permission; see the header comments). We do NOT redistribute the Adobe
 * headers: this file only re-declares the fields a filter needs.
 *
 * Compiles unchanged on non-MSVC toolchains (used by the Linux test harness
 * with a mock host) — only the optional static offset asserts are MSVC/x64.
 */
#pragma once

#include <stdint.h>

/* ------------------------------------------------------------------ ints */
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef unsigned char Boolean;     /* 0 = false, 1 = true (BYTE) */
typedef int16 OSErr;               /* errors are signed shorts (noErr=0) */
typedef uint32_t OSType;           /* four-character code */
typedef uint32_t Fixed;            /* 16.16 fixed point */
typedef uint32_t ResType;
typedef uint32_t DescType;

typedef char *Ptr;
typedef Ptr *Handle;               /* pointer to a master pointer */

typedef unsigned char Str255[256]; /* length byte + up to 255 payload chars */

typedef intptr_t (*ProcPtr)();

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define noErr 0
#define memFullErr (-108)
#define userCanceledErr (-128)

/* ------------------------------------------------------------------ geom */
typedef struct Point
{
  int16 v;
  int16 h;
} Point;

typedef struct Rect
{
  int16 top;
  int16 left;
  int16 bottom;
  int16 right;
} Rect;

typedef struct VPoint
{
  int32 v;
  int32 h;
} VPoint;

typedef struct VRect
{
  int32 top;
  int32 left;
  int32 bottom;
  int32 right;
} VRect;

/* 48-bit RGB (16 bits per component), as the host reports foreground /
 * background colors. */
typedef struct PSRGBColor
{
  uint16 red;
  uint16 green;
  uint16 blue;
} PSRGBColor;

/* 8BF FilterRecord background/foreground color (4 bytes, native image
 * color space). */
typedef unsigned char FilterColor[4];

/* ------------------------------------------------------------------ conv */
/* windows.h already defines PASCAL (as __stdcall); on x64 that is the same
 * ABI as the empty 64-bit default, so keep whatever the platform provided. */
#ifndef PASCAL
#if defined(_WIN32) && !defined(_WIN64)
#define PASCAL __stdcall
#else
#define PASCAL
#endif
#endif

#if defined(_WIN32)
#define DLLExport __declspec(dllexport)
#define DLLImport __declspec(dllimport)
#else
#define DLLExport
#define DLLImport
#endif