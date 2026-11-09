/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pifilter.h — the 8BF filter module interface: this project's own declaration
 * of Adobe's public PIFilter.h (self-contained, minimal), plus the service procs
 * a filter actually calls. Layout must match the host's FilterRecord
 * byte-for-byte on x64 (pack(4)); the 121 offset asserts below vindicate that on
 * the first MSVC build.
 *
 * Only the subset a stills filter needs is here: the six selector calls, the
 * parameter handle, plane/rect data access, HandleProcs/advanceState/
 * progress/abort, and the FilterCaseInfo ('fici') constants used by the PiPL.
 * PICA suites (sSPBasic) are deliberately NOT used (the spetric host and
 * Affinity's plugin runner don't need us to).
 */
#pragma once

#include "pi_types.h"

/* ----------------------------------------------------------- selectors -- */
#define filterSelectorAbout       0
#define filterSelectorParameters  1
#define filterSelectorPrepare     2
#define filterSelectorStart       3
#define filterSelectorContinue    4
#define filterSelectorFinish      5

/* ----------------------------------------------------------- result codes */
#define filterBadParameters (-30100)
#define filterBadMode       (-30101)
/* Return this and set FilterRecord::errorString to have the host report it. */
#define errReportString     (-30904)

/* ------------------------------------------------------------ image modes */
#define plugInModeGrayScale       1
#define plugInModeIndexedColor    2
#define plugInModeRGBColor        3
#define plugInModeCMYKColor       4
#define plugInModeHSBColor        6
#define plugInModeMultichannel    7
#define plugInModeDuotone         8
#define plugInModeLabColor        9
#define plugInModeGray16         10
#define plugInModeRGB48          11
#define plugInModeLab48          12
#define plugInModeCMYK64         13
#define plugInModeGray32         17
#define plugInModeRGB96          16

/* ------------------------------------------------------ filter case info */
/* Values for FilterCaseInfo::inputHandling / outputHandling. */
#define filterDataHandlingCantFilter 0
#define filterDataHandlingNone      1
#define filterDataHandlingBlackMat  2
#define filterDataHandlingGrayMat   3
#define filterDataHandlingWhiteMat  4
#define filterDataHandlingDefringe  5
#define filterDataHandlingBlackZap  6
#define filterDataHandlingGrayZap   7
#define filterDataHandlingWhiteZap  8
#define filterDataHandlingFillMask  9
#define filterDataHandlingBackgroundZap 10
#define filterDataHandlingForegroundZap 11

/* FilterCaseInfo::flags1 bits (LSB is bit 0). */
#define PIFilterDontCopyToDestinationBit 0
#define PIFilterWorksWithBlankDataBit     1
#define PIFilterFiltersLayerMaskBit       2
#define PIFilterWritesOutsideSelectionBit 3

/* The seven filter cases, ordered as the 'fici' PiPL array is indexed. */
#define filterCaseFlatImageNoSelection             1
#define filterCaseFlatImageWithSelection           2
#define filterCaseFloatingSelection                3
#define filterCaseEditableTransparencyNoSelection  4
#define filterCaseEditableTransparencyWithSelection 5
#define filterCaseProtectedTransparencyNoSelection 6
#define filterCaseProtectedTransparencyWithSelection 7

/* ----------------------------------------------------- proc declarations */
/* NB: the calling convention goes *inside* the pointer declarator —
 * `Boolean (PASCAL *fp)(void)` — not before the return type. The leading form
 * is tolerated by GCC but rejected by MSVC in C mode. On x64 PASCAL is a
 * no-op anyway (single calling convention), so this is purely syntax. */
typedef Boolean (PASCAL *TestAbortProc)(void);
typedef void (PASCAL *ProgressProc)(int32 done, int32 total);
typedef OSErr (PASCAL *AdvanceStateProc)(void);
typedef void (PASCAL *HostProc)(int16 selector, intptr_t *data);
typedef void (PASCAL *ProcessEventProc)(void *event);
typedef OSErr (PASCAL *DisplayPixelsProc)(void *source, void *destination,
                                          int16 *destinationRect,
                                          int16 transparent);
typedef OSErr (*GetPropertyProc)(int16 selector, int16 index, void *simple,
                                 intptr_t *complex);
typedef void (*ColorServicesProc)(void);

/* Suite pointers we only need as sized slots. */
typedef struct BufferProcs BufferProcs;
typedef struct ResourceProcs ResourceProcs;
typedef struct ImageServicesProcs ImageServicesProcs;
typedef struct PropertyProcs PropertyProcs;
typedef struct ChannelPortProcs ChannelPortProcs;
typedef struct ReadImageDocumentDesc ReadImageDocumentDesc;
typedef struct SPBasicSuite SPBasicSuite;
typedef struct PIDescriptorParameters PIDescriptorParameters;

/* ---------------------------------------------------------- Handle suite */
typedef Handle (PASCAL *NewPIHandleProc)(int32 size);
typedef void (PASCAL *DisposePIHandleProc)(Handle h);
typedef int32 (PASCAL *GetPIHandleSizeProc)(Handle h);
typedef void (PASCAL *SetPIHandleSizeProc)(Handle h, int32 newSize);
typedef void *(PASCAL *LockPIHandleProc)(Handle h, Boolean moveHigh);
typedef void (PASCAL *UnlockPIHandleProc)(Handle h);
typedef int32 (PASCAL *RecoverSpaceProc)(int32 size);
typedef void (PASCAL *DisposeRegularHandlePIHandleProc)(
    Handle h, void *regularHandle);

typedef struct HandleProcs
{
  int16 handleProcsVersion;
  int16 numHandleProcs;
  NewPIHandleProc newProc;
  DisposePIHandleProc disposeProc;
  GetPIHandleSizeProc getSizeProc;
  SetPIHandleSizeProc setSizeProc;
  LockPIHandleProc lockProc;
  UnlockPIHandleProc unlockProc;
  RecoverSpaceProc recoverSpaceProc;
  DisposeRegularHandlePIHandleProc disposeRegularHandleProc;
} HandleProcs;

/* ---------------------------------------------------------- monitor info */
typedef struct PlugInMonitor
{
  Fixed gamma;      /* 0 if the whole record is invalid */
  Fixed redX;
  Fixed redY;
  Fixed greenX;
  Fixed greenY;
  Fixed blueX;
  Fixed blueY;
  Fixed whiteX;
  Fixed whiteY;
  Fixed ambient;
} PlugInMonitor;

/* -------------------------------------------------------- big documents */
typedef struct BigDocumentStruct
{
  int32 PluginUsing32BitCoordinates;
  VPoint imageSize32;   /* replaces FilterRecord::imageSize */
  VRect filterRect32;   /* replaces FilterRecord::filterRect */
  VRect inRect32;       /* replaces FilterRecord::inRect */
  VRect outRect32;      /* replaces FilterRecord::outRect */
  VRect maskRect32;     /* replaces FilterRecord::maskRect */
  VPoint floatCoord32;  /* replaces FilterRecord::floatCoord */
  VPoint wholeSize32;   /* replaces FilterRecord::wholeSize */
} BigDocumentStruct;

/* ---------------------------------------------------------- FilterRecord */
/* pack(4) applies on EVERY toolchain: the layout must match the Windows host
 * byte-for-byte, and the Linux mock harness relies on that too. */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4121) /* alignment of a member was sensitive to packing */
#endif
#pragma pack(push, 4)

typedef struct FilterRecord
{
  /* --- legacy/new fields, in SDK order so offsets match on x64 pack(4) --- */
  int32        serialNumber;      /* deprecated: always 0 */
  TestAbortProc abortProc;        /* check user abort */
  ProgressProc  progressProc;     /* report progress */
  Handle        parameters;       /* our params handle; NULL until we set it */
  Point         imageSize;        /* deprecated: use bigDocumentData */

  int16         planes;           /* total active planes (color + alpha) */
  Rect          filterRect;       /* selection bounds */
  PSRGBColor    background;
  PSRGBColor    foreground;
  int32         maxSpace;         /* deprecated: use maxSpace64 */
  int32         bufferSpace;      /* deprecated: use bufferSpace64 */

  Rect          inRect;           /* our requested input tile */
  int16         inLoPlane;
  int16         inHiPlane;
  Rect          outRect;          /* our requested output tile */
  int16         outLoPlane;
  int16         outHiPlane;

  void         *inData;           /* tile pixels, interleaved (inLo..inHi) */
  int32         inRowBytes;
  void         *outData;
  int32         outRowBytes;

  Boolean       isFloating;
  Boolean       haveMask;
  Boolean       autoMask;
  Rect          maskRect;
  void         *maskData;
  int32         maskRowBytes;

  FilterColor   backColor;
  FilterColor   foreColor;
  OSType        hostSig;
  HostProc      hostProc;

  int16         imageMode;        /* plugInModeRGBColor etc. */
  Fixed         imageHRes;
  Fixed         imageVRes;
  Point         floatCoord;       /* deprecated: use bigDocumentData */
  Point         wholeSize;        /* deprecated: use bigDocumentData */
  PlugInMonitor monitor;

  void                    *platformData;
  BufferProcs             *bufferProcs;
  ResourceProcs           *resourceProcs;
  ProcessEventProc         processEvent;
  DisplayPixelsProc        displayPixels;
  HandleProcs             *handleProcs;

  Boolean       supportsDummyChannels;
  Boolean       supportsAlternateLayouts;
  int16         wantLayout;
  int16         filterCase;       /* the FilterCaseIdentifiers value */
  int16         dummyPlaneValue;

  void                *premiereHook;  /* deprecated */
  AdvanceStateProc     advanceState;  /* request the next tile */
  Boolean              supportsAbsolute;
  Boolean              wantsAbsolute;
  GetPropertyProc      getPropertyObsolete;
  Boolean              cannotUndo;
  Boolean              supportsPadding;
  int16                inputPadding;
  int16                outputPadding;
  int16                maskPadding;
  char                 samplingSupport;
  char                 reservedByte;
  Fixed                inputRate;
  Fixed                maskRate;
  ColorServicesProc    colorServices;

  int16         inLayerPlanes;
  int16         inTransparencyMask;
  int16         inLayerMasks;
  int16         inInvertedLayerMasks;
  int16         inNonLayerPlanes;
  int16         outLayerPlanes;
  int16         outTransparencyMask;
  int16         outLayerMasks;
  int16         outInvertedLayerMasks;
  int16         outNonLayerPlanes;
  int16         absLayerPlanes;
  int16         absTransparencyMask;
  int16         absLayerMasks;
  int16         absInvertedLayerMasks;
  int16         absNonLayerPlanes;

  int16         inPreDummyPlanes;
  int16         inPostDummyPlanes;
  int16         outPreDummyPlanes;
  int16         outPostDummyPlanes;

  int32         inColumnBytes;
  int32         inPlaneBytes;
  int32         outColumnBytes;
  int32         outPlaneBytes;

  ImageServicesProcs *imageServicesProcs;
  PropertyProcs      *propertyProcs;

  int16         inTileHeight;
  int16         inTileWidth;
  Point         inTileOrigin;
  int16         absTileHeight;
  int16         absTileWidth;
  Point         absTileOrigin;
  int16         outTileHeight;
  int16         outTileWidth;
  Point         outTileOrigin;
  int16         maskTileHeight;
  int16         maskTileWidth;
  Point         maskTileOrigin;

  PIDescriptorParameters *descriptorParameters;
  Str255                 *errorString;
  ChannelPortProcs       *channelPortProcs;
  ReadImageDocumentDesc  *documentInfo;
  SPBasicSuite           *sSPBasic;
  void                   *plugInRef;

  int32         depth;            /* 8, 16, or 32 bits per channel */
  Handle        iCCprofileData;
  int32         iCCprofileSize;
  int32         canUseICCProfiles;
  int32         hasImageScrap;
  BigDocumentStruct *bigDocumentData;
  PIDescriptorParameters *input3DScene;
  PIDescriptorParameters *output3DScene;
  Boolean       createNewLayer;
  Handle        iCCWorkingProfileData;
  int32         iCCWorkingProfileSize;

  int64         bufferSpace64;    /* max bytes of in+out data we can use */
  int64         maxSpace64;       /* deprecated: same figure Photoshop allows */

  Boolean       allowOverrangeColors; /* 32-bit results may use over-range */
  char          reserved[45];
} FilterRecord;

#pragma pack(pop)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef FilterRecord *FilterRecordPtr;

/* ------------------------------------------------------------ size guard */
/* Vindicate every offset against the canonical x64 layout on the first MSVC
 * build. Generated from the SDK field list + simulated pack(4) layout (see
 * the ps8bf README); a mismatch here means a wrong type/order above. */
#if defined(_WIN64) && !defined(RC_INVOKED)
#include <stddef.h>
/* C++ translation units (the ImGui GUI backend) can't use C11 _Static_assert,
 * so route through a portable macro. */
#if defined(__cplusplus)
#define SF_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define SF_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif
#define SF_OFF(f, n) SF_STATIC_ASSERT(offsetof(FilterRecord, f) == (n), #f)
SF_OFF(serialNumber, 0);         SF_OFF(abortProc, 4);
SF_OFF(progressProc, 12);        SF_OFF(parameters, 20);
SF_OFF(imageSize, 28);           SF_OFF(planes, 32);
SF_OFF(filterRect, 34);          SF_OFF(background, 42);
SF_OFF(foreground, 48);          SF_OFF(maxSpace, 56);
SF_OFF(bufferSpace, 60);         SF_OFF(inRect, 64);
SF_OFF(inLoPlane, 72);           SF_OFF(inHiPlane, 74);
SF_OFF(outRect, 76);             SF_OFF(outLoPlane, 84);
SF_OFF(outHiPlane, 86);          SF_OFF(inData, 88);
SF_OFF(inRowBytes, 96);          SF_OFF(outData, 100);
SF_OFF(outRowBytes, 108);        SF_OFF(isFloating, 112);
SF_OFF(haveMask, 113);           SF_OFF(autoMask, 114);
SF_OFF(maskRect, 116);           SF_OFF(maskData, 124);
SF_OFF(maskRowBytes, 132);       SF_OFF(backColor, 136);
SF_OFF(foreColor, 140);          SF_OFF(hostSig, 144);
SF_OFF(hostProc, 148);           SF_OFF(imageMode, 156);
SF_OFF(imageHRes, 160);          SF_OFF(imageVRes, 164);
SF_OFF(floatCoord, 168);         SF_OFF(wholeSize, 172);
SF_OFF(monitor, 176);            SF_OFF(platformData, 216);
SF_OFF(bufferProcs, 224);        SF_OFF(resourceProcs, 232);
SF_OFF(processEvent, 240);       SF_OFF(displayPixels, 248);
SF_OFF(handleProcs, 256);        SF_OFF(supportsDummyChannels, 264);
SF_OFF(supportsAlternateLayouts, 265); SF_OFF(wantLayout, 266);
SF_OFF(filterCase, 268);         SF_OFF(dummyPlaneValue, 270);
SF_OFF(premiereHook, 272);       SF_OFF(advanceState, 280);
SF_OFF(supportsAbsolute, 288);   SF_OFF(wantsAbsolute, 289);
SF_OFF(getPropertyObsolete, 292); SF_OFF(cannotUndo, 300);
SF_OFF(supportsPadding, 301);    SF_OFF(inputPadding, 302);
SF_OFF(outputPadding, 304);      SF_OFF(maskPadding, 306);
SF_OFF(samplingSupport, 308);    SF_OFF(reservedByte, 309);
SF_OFF(inputRate, 312);          SF_OFF(maskRate, 316);
SF_OFF(colorServices, 320);      SF_OFF(inLayerPlanes, 328);
SF_OFF(inTransparencyMask, 330); SF_OFF(inLayerMasks, 332);
SF_OFF(inInvertedLayerMasks, 334); SF_OFF(inNonLayerPlanes, 336);
SF_OFF(outLayerPlanes, 338);     SF_OFF(outTransparencyMask, 340);
SF_OFF(outLayerMasks, 342);      SF_OFF(outInvertedLayerMasks, 344);
SF_OFF(outNonLayerPlanes, 346);  SF_OFF(absLayerPlanes, 348);
SF_OFF(absTransparencyMask, 350); SF_OFF(absLayerMasks, 352);
SF_OFF(absInvertedLayerMasks, 354); SF_OFF(absNonLayerPlanes, 356);
SF_OFF(inPreDummyPlanes, 358);   SF_OFF(inPostDummyPlanes, 360);
SF_OFF(outPreDummyPlanes, 362);  SF_OFF(outPostDummyPlanes, 364);
SF_OFF(inColumnBytes, 368);      SF_OFF(inPlaneBytes, 372);
SF_OFF(outColumnBytes, 376);     SF_OFF(outPlaneBytes, 380);
SF_OFF(imageServicesProcs, 384); SF_OFF(propertyProcs, 392);
SF_OFF(inTileHeight, 400);       SF_OFF(inTileWidth, 402);
SF_OFF(inTileOrigin, 404);       SF_OFF(absTileHeight, 408);
SF_OFF(absTileWidth, 410);       SF_OFF(absTileOrigin, 412);
SF_OFF(outTileHeight, 416);      SF_OFF(outTileWidth, 418);
SF_OFF(outTileOrigin, 420);      SF_OFF(maskTileHeight, 424);
SF_OFF(maskTileWidth, 426);      SF_OFF(maskTileOrigin, 428);
SF_OFF(descriptorParameters, 432); SF_OFF(errorString, 440);
SF_OFF(channelPortProcs, 448);   SF_OFF(documentInfo, 456);
SF_OFF(sSPBasic, 464);           SF_OFF(plugInRef, 472);
SF_OFF(depth, 480);              SF_OFF(iCCprofileData, 484);
SF_OFF(iCCprofileSize, 492);     SF_OFF(canUseICCProfiles, 496);
SF_OFF(hasImageScrap, 500);      SF_OFF(bigDocumentData, 504);
SF_OFF(input3DScene, 512);       SF_OFF(output3DScene, 520);
SF_OFF(createNewLayer, 528);     SF_OFF(iCCWorkingProfileData, 532);
SF_OFF(iCCWorkingProfileSize, 540); SF_OFF(bufferSpace64, 544);
SF_OFF(maxSpace64, 552);         SF_OFF(allowOverrangeColors, 560);
SF_OFF(reserved, 561);
SF_STATIC_ASSERT(sizeof(FilterRecord) == 608, "sizeof FilterRecord");
#endif /* _WIN64 */

/* ----------------------------------------------------- FilterCaseInfo --- */
#pragma pack(push, 1)
typedef struct FilterCaseInfo
{
  char inputHandling;   /* FilterDataHandling* constant */
  char outputHandling;
  char flags1;          /* PIFilter*Bit flags */
  char flags2;          /* reserved, 0 */
} FilterCaseInfo;
#pragma pack(pop)