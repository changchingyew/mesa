/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef D3D12_FORMAT_UTILS_H
#define D3D12_FORMAT_UTILS_H

#ifndef _WIN32
#include <wsl/winadapter.h>
#endif

#define D3D12_IGNORE_SDK_LAYERS
#include <directx/d3d12.h>
#include <d3dx12.h>
#include "pipe/p_video_enums.h"

#define D3DFORMATDESC 1

#define MAP_ALIGN_REQUIREMENT 16 // Map is required to return 16-byte aligned addresses

namespace D3D12VideoFormatHelper
{
    // ----------------------------------------------------------------------------
    // Some enumerations used in the D3D11_FORMAT_DETAIL structure
    // ----------------------------------------------------------------------------
    typedef enum D3D11_FORMAT_LAYOUT
    {
        D3D11FL_STANDARD = 0, // standard layout
        D3D11FL_CUSTOM   = -1  // custom layout
        // Note, 1 bit allocated for this in FORMAT_DETAIL below. If you add fields here, add bits...
        // NOTE SIGNED VALUES ARE USED SINCE COMPILER MAKES ENUMS SIGNED, AND BITFIELDS ARE SIGN EXTENDED ON READ
    } D3D11_FORMAT_LAYOUT;

    typedef enum D3D11_FORMAT_TYPE_LEVEL
    {
        D3D11FTL_NO_TYPE      = 0,
        D3D11FTL_PARTIAL_TYPE = -2,
        D3D11FTL_FULL_TYPE    = -1,
        // Note, 2 bits allocated for this in FORMAT_DETAIL below. If you add fields here, add bits...
        // NOTE SIGNED VALUES ARE USED SINCE COMPILER MAKES ENUMS SIGNED, AND BITFIELDS ARE SIGN EXTENDED ON READ
    } D3D11_FORMAT_TYPE_LEVEL;

    typedef enum D3D11_FORMAT_COMPONENT_NAME
    {
        D3D11FCN_R     = -4,
        D3D11FCN_G     = -3,
        D3D11FCN_B     = -2,
        D3D11FCN_A     = -1,
        D3D11FCN_D     = 0,
        D3D11FCN_S     = 1,
        D3D11FCN_X     = 2,
        // Note, 3 bits allocated for this in FORMAT_DETAIL below. If you add fields here, add bits...
        // NOTE SIGNED VALUES ARE USED SINCE COMPILER MAKES ENUMS SIGNED, AND BITFIELDS ARE SIGN EXTENDED ON READ
    } D3D11_FORMAT_COMPONENT_NAME;

    typedef enum D3D11_FORMAT_COMPONENT_INTERPRETATION
    {
        D3D11FCI_TYPELESS    = 0,
        D3D11FCI_FLOAT       = -4,
        D3D11FCI_SNORM       = -3,
        D3D11FCI_UNORM       = -2,
        D3D11FCI_SINT        = -1,
        D3D11FCI_UINT        = 1,
        D3D11FCI_UNORM_SRGB  = 2,
        D3D11FCI_BIASED_FIXED_2_8   = 3,
        // Note, 3 bits allocated for this in FORMAT_DETAIL below. If you add fields here, add bits...
        // NOTE SIGNED VALUES ARE USED SINCE COMPILER MAKES ENUMS SIGNED, AND BITFIELDS ARE SIGN EXTENDED ON READ
    } D3D11_FORMAT_COMPONENT_INTERPRETATION;


    // This struct holds information about formats that is feature level and driver version agnostic
    typedef struct FORMAT_DETAIL
    {
        DXGI_FORMAT                 DXGIFormat;
        DXGI_FORMAT                 ParentFormat;
        const DXGI_FORMAT*          pDefaultFormatCastSet;  // This is dependent on FL/driver version, but is here to save a lot of space
        UINT8                       BitsPerComponent[4]; // only used for D3D11FTL_PARTIAL_TYPE or FULL_TYPE
        UINT8                       BitsPerUnit;             // BitsPerUnit is bits per pixel for non-compressed formats and bits per block for compressed formats
        BOOL                        SRGBFormat : 1;
        UINT                        WidthAlignment : 4;      // number of texels to align to in a mip level.
        UINT                        HeightAlignment : 4;     // Top level dimensions must be a multiple of these
        UINT                        DepthAlignment : 1;      // values.
        D3D11_FORMAT_LAYOUT         Layout : 1;
        D3D11_FORMAT_TYPE_LEVEL     TypeLevel : 2;
        D3D11_FORMAT_COMPONENT_NAME ComponentName0 : 3; // RED    ... only used for D3D11FTL_PARTIAL_TYPE or FULL_TYPE
        D3D11_FORMAT_COMPONENT_NAME ComponentName1 : 3; // GREEN  ... only used for D3D11FTL_PARTIAL_TYPE or FULL_TYPE
        D3D11_FORMAT_COMPONENT_NAME ComponentName2 : 3; // BLUE   ... only used for D3D11FTL_PARTIAL_TYPE or FULL_TYPE
        D3D11_FORMAT_COMPONENT_NAME ComponentName3 : 3; // ALPHA  ... only used for D3D11FTL_PARTIAL_TYPE or FULL_TYPE
        D3D11_FORMAT_COMPONENT_INTERPRETATION ComponentInterpretation0 : 3; // only used for D3D11FTL_FULL_TYPE
        D3D11_FORMAT_COMPONENT_INTERPRETATION ComponentInterpretation1 : 3; // only used for D3D11FTL_FULL_TYPE
        D3D11_FORMAT_COMPONENT_INTERPRETATION ComponentInterpretation2 : 3; // only used for D3D11FTL_FULL_TYPE
        D3D11_FORMAT_COMPONENT_INTERPRETATION ComponentInterpretation3 : 3; // only used for D3D11FTL_FULL_TYPE
        bool                        bPlanar : 1;
        bool                        bYUV : 1;
    } FORMAT_DETAIL;

    #define R D3D11FCN_R
    #define G D3D11FCN_G
    #define B D3D11FCN_B
    #define A D3D11FCN_A
    #define D D3D11FCN_D
    #define S D3D11FCN_S
    #define X D3D11FCN_X

    #define _TYPELESS   D3D11FCI_TYPELESS
    #define _FLOAT      D3D11FCI_FLOAT
    #define _SNORM      D3D11FCI_SNORM
    #define _UNORM      D3D11FCI_UNORM
    #define _SINT       D3D11FCI_SINT
    #define _UINT       D3D11FCI_UINT
    #define _UNORM_SRGB D3D11FCI_UNORM_SRGB
    #define _FIXED_2_8  D3D11FCI_BIASED_FIXED_2_8

    // --------------------------------------------------------------------------------------------------------------------------------
    // Format Cast Sets
    const DXGI_FORMAT D3D11FCS_UNKNOWN[] =
    {
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R32G32B32A32[] =
    {
        DXGI_FORMAT_R32G32B32A32_TYPELESS,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_UINT,
        DXGI_FORMAT_R32G32B32A32_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R32G32B32[] =
    {
        DXGI_FORMAT_R32G32B32_TYPELESS,
        DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT_R32G32B32_UINT,
        DXGI_FORMAT_R32G32B32_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R16G16B16A16[] =
    {
        DXGI_FORMAT_R16G16B16A16_TYPELESS,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_UNORM,
        DXGI_FORMAT_R16G16B16A16_UINT,
        DXGI_FORMAT_R16G16B16A16_SNORM,
        DXGI_FORMAT_R16G16B16A16_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R32G32[] =
    {
        DXGI_FORMAT_R32G32_TYPELESS,
        DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT_R32G32_UINT,
        DXGI_FORMAT_R32G32_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R32G8X24[] =
    {
        DXGI_FORMAT_R32G8X24_TYPELESS,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
        DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R10G10B10A2[] =
    {
        DXGI_FORMAT_R10G10B10A2_TYPELESS,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R10G10B10A2_UINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R11G11B10[] =
    {
        DXGI_FORMAT_R11G11B10_FLOAT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R8G8B8A8[] =
    {
        DXGI_FORMAT_R8G8B8A8_TYPELESS,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT_R8G8B8A8_SNORM,
        DXGI_FORMAT_R8G8B8A8_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R16G16[] =
    {
        DXGI_FORMAT_R16G16_TYPELESS,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16G16_UNORM,
        DXGI_FORMAT_R16G16_UINT,
        DXGI_FORMAT_R16G16_SNORM,
        DXGI_FORMAT_R16G16_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R32[] =
    {
        DXGI_FORMAT_R32_TYPELESS,
        DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32_UINT,
        DXGI_FORMAT_R32_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R24G8[] =
    {
        DXGI_FORMAT_R24G8_TYPELESS,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
        DXGI_FORMAT_X24_TYPELESS_G8_UINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R8G8[] =
    {
        DXGI_FORMAT_R8G8_TYPELESS,
        DXGI_FORMAT_R8G8_UNORM,
        DXGI_FORMAT_R8G8_UINT,
        DXGI_FORMAT_R8G8_SNORM,
        DXGI_FORMAT_R8G8_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R16[] =
    {
        DXGI_FORMAT_R16_TYPELESS,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_D16_UNORM,
        DXGI_FORMAT_R16_UNORM,
        DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT_R16_SNORM,
        DXGI_FORMAT_R16_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R8[] =
    {
        DXGI_FORMAT_R8_TYPELESS,
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8_UINT,
        DXGI_FORMAT_R8_SNORM,
        DXGI_FORMAT_R8_SINT,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_A8[] =
    {
        DXGI_FORMAT_A8_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R1[] =
    {
        DXGI_FORMAT_R1_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R9G9B9E5[] =
    {
        DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R8G8_B8G8[] =
    {
        DXGI_FORMAT_R8G8_B8G8_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_G8R8_G8B8[] =
    {
        DXGI_FORMAT_G8R8_G8B8_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC1[] =
    {
        DXGI_FORMAT_BC1_TYPELESS,
        DXGI_FORMAT_BC1_UNORM,
        DXGI_FORMAT_BC1_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC2[] =
    {
        DXGI_FORMAT_BC2_TYPELESS,
        DXGI_FORMAT_BC2_UNORM,
        DXGI_FORMAT_BC2_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC3[] =
    {
        DXGI_FORMAT_BC3_TYPELESS,
        DXGI_FORMAT_BC3_UNORM,
        DXGI_FORMAT_BC3_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC4[] =
    {
        DXGI_FORMAT_BC4_TYPELESS,
        DXGI_FORMAT_BC4_UNORM,
        DXGI_FORMAT_BC4_SNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC5[] =
    {
        DXGI_FORMAT_BC5_TYPELESS,
        DXGI_FORMAT_BC5_UNORM,
        DXGI_FORMAT_BC5_SNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B5G6R5[] =
    {
        DXGI_FORMAT_B5G6R5_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B5G5R5A1[] =
    {
        DXGI_FORMAT_B5G5R5A1_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B8G8R8A8[] =
    {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B8G8R8X8[] =
    {
        DXGI_FORMAT_B8G8R8X8_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B8G8R8A8_Win7[] =
    {
        DXGI_FORMAT_B8G8R8A8_TYPELESS,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B8G8R8X8_Win7[] =
    {
        DXGI_FORMAT_B8G8R8X8_TYPELESS,
        DXGI_FORMAT_B8G8R8X8_UNORM,
        DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_R10G10B10A2_XR[] =
    {
        DXGI_FORMAT_R10G10B10A2_TYPELESS,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R10G10B10A2_UINT,
        DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC6H[] =
    {
        DXGI_FORMAT_BC6H_TYPELESS,
        DXGI_FORMAT_BC6H_UF16,
        DXGI_FORMAT_BC6H_SF16,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_BC7[] =
    {
        DXGI_FORMAT_BC7_TYPELESS,
        DXGI_FORMAT_BC7_UNORM,
        DXGI_FORMAT_BC7_UNORM_SRGB,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_AYUV[] =
    {
        DXGI_FORMAT_AYUV,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_NV12[] =
    {
        DXGI_FORMAT_NV12,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_YUY2[] =
    {
        DXGI_FORMAT_YUY2,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_P010[] =
    {
        DXGI_FORMAT_P010,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_P016[] =
    {
        DXGI_FORMAT_P016,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_NV11[] =
    {
        DXGI_FORMAT_NV11,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_420_OPAQUE[] =
    {
        DXGI_FORMAT_420_OPAQUE,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_Y410[] =
    {
        DXGI_FORMAT_Y410,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_Y416[] =
    {
        DXGI_FORMAT_Y416,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_Y210[] =
    {
        DXGI_FORMAT_Y210,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_Y216[] =
    {
        DXGI_FORMAT_Y216,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_AI44[] =
    {
        DXGI_FORMAT_AI44,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_IA44[] =
    {
        DXGI_FORMAT_IA44,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_P8[] =
    {
        DXGI_FORMAT_P8,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_A8P8[] =
    {
        DXGI_FORMAT_A8P8,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_B4G4R4A4[] =
    {
        DXGI_FORMAT_B4G4R4A4_UNORM,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_P208[] =
    {
        DXGI_FORMAT_P208,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_V208[] =
    {
        DXGI_FORMAT_V208,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    const DXGI_FORMAT D3D11FCS_V408[] =
    {
        DXGI_FORMAT_V408,
        DXGI_FORMAT_UNKNOWN // not part of cast set, just the "null terminator"
    };

    // ----------------------------------------------------------------------------
    // As much information about D3D10's interpretation of DXGI Resource Formats should be encoded in this
    // table, and everyone should query the information from here, be it for
    // specs or for code. 
    // The new BitsPerUnit value represents two possible values. If the format is a block compressed format
    // then the value stored is bit per block. If the format is not a block compressed format then the value
    // represents bits per pixel.
    // ----------------------------------------------------------------------------

    const FORMAT_DETAIL s_FormatDetail[] =
    {
    //      DXGI_FORMAT                           ParentFormat                              pDefaultFormatCastSet   BitsPerComponent[4], BitsPerUnit,    SRGB,  WidthAlignment, HeightAlignment, DepthAlignment,   Layout,             TypeLevel,              ComponentName[4],ComponentInterpretation[4],                          bPlanar, bYUV    
        {DXGI_FORMAT_UNKNOWN                     ,DXGI_FORMAT_UNKNOWN,                      D3D11FCS_UNKNOWN,       {0,0,0,0},           0,              FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_NO_TYPE,       X,X,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {DXGI_FORMAT_R32G32B32A32_TYPELESS       ,DXGI_FORMAT_R32G32B32A32_TYPELESS,        D3D11FCS_R32G32B32A32,  {32,32,32,32},       128,            FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32A32_FLOAT      ,DXGI_FORMAT_R32G32B32A32_TYPELESS,        D3D11FCS_R32G32B32A32,  {32,32,32,32},       128,            FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _FLOAT, _FLOAT, _FLOAT, _FLOAT,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32A32_UINT       ,DXGI_FORMAT_R32G32B32A32_TYPELESS,        D3D11FCS_R32G32B32A32,  {32,32,32,32},       128,            FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UINT, _UINT, _UINT, _UINT,                          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32A32_SINT       ,DXGI_FORMAT_R32G32B32A32_TYPELESS,        D3D11FCS_R32G32B32A32,  {32,32,32,32},       128,            FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _SINT, _SINT, _SINT, _SINT,                          FALSE,   FALSE,  },
        {DXGI_FORMAT_R32G32B32_TYPELESS          ,DXGI_FORMAT_R32G32B32_TYPELESS,           D3D11FCS_R32G32B32,     {32,32,32,0},        96,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,B,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32_FLOAT         ,DXGI_FORMAT_R32G32B32_TYPELESS,           D3D11FCS_R32G32B32,     {32,32,32,0},        96,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,X,         _FLOAT, _FLOAT, _FLOAT, _TYPELESS,                   FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32_UINT          ,DXGI_FORMAT_R32G32B32_TYPELESS,           D3D11FCS_R32G32B32,     {32,32,32,0},        96,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,X,         _UINT, _UINT, _UINT, _TYPELESS,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32B32_SINT          ,DXGI_FORMAT_R32G32B32_TYPELESS,           D3D11FCS_R32G32B32,     {32,32,32,0},        96,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,X,         _SINT, _SINT, _SINT, _TYPELESS,                      FALSE,   FALSE,  },
        {DXGI_FORMAT_R16G16B16A16_TYPELESS       ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {   DXGI_FORMAT_R16G16B16A16_FLOAT       ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _FLOAT, _FLOAT, _FLOAT, _FLOAT,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16B16A16_UNORM      ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16B16A16_UINT       ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UINT, _UINT, _UINT, _UINT,                          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16B16A16_SNORM      ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _SNORM, _SNORM, _SNORM, _SNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16B16A16_SINT       ,DXGI_FORMAT_R16G16B16A16_TYPELESS,        D3D11FCS_R16G16B16A16,  {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _SINT, _SINT, _SINT, _SINT,                          FALSE,   FALSE,  },
        {DXGI_FORMAT_R32G32_TYPELESS             ,DXGI_FORMAT_R32G32_TYPELESS,              D3D11FCS_R32G32,        {32,32,0,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32_FLOAT            ,DXGI_FORMAT_R32G32_TYPELESS,              D3D11FCS_R32G32,        {32,32,0,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _FLOAT, _FLOAT, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32_UINT             ,DXGI_FORMAT_R32G32_TYPELESS,              D3D11FCS_R32G32,        {32,32,0,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _UINT, _UINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32G32_SINT             ,DXGI_FORMAT_R32G32_TYPELESS,              D3D11FCS_R32G32,        {32,32,0,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _SINT, _SINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {DXGI_FORMAT_R32G8X24_TYPELESS           ,DXGI_FORMAT_R32G8X24_TYPELESS,            D3D11FCS_R32G8X24,      {32,8,24,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_D32_FLOAT_S8X24_UINT    ,DXGI_FORMAT_R32G8X24_TYPELESS,            D3D11FCS_R32G8X24,      {32,8,24,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     D,S,X,X,         _FLOAT,_UINT,_TYPELESS,_TYPELESS,                    FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_R32G8X24_TYPELESS,            D3D11FCS_R32G8X24,      {32,8,24,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _FLOAT,_TYPELESS,_TYPELESS,_TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_X32_TYPELESS_G8X24_UINT ,DXGI_FORMAT_R32G8X24_TYPELESS,            D3D11FCS_R32G8X24,      {32,8,24,0},         64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     X,G,X,X,         _TYPELESS,_UINT,_TYPELESS,_TYPELESS,                 FALSE,   FALSE,  },
        {DXGI_FORMAT_R10G10B10A2_TYPELESS        ,DXGI_FORMAT_R10G10B10A2_TYPELESS,         D3D11FCS_R10G10B10A2_XR,{10,10,10,2},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R10G10B10A2_UNORM       ,DXGI_FORMAT_R10G10B10A2_TYPELESS,         D3D11FCS_R10G10B10A2_XR,{10,10,10,2},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R10G10B10A2_UINT        ,DXGI_FORMAT_R10G10B10A2_TYPELESS,         D3D11FCS_R10G10B10A2_XR,{10,10,10,2},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UINT, _UINT, _UINT, _UINT,                          FALSE,   FALSE,  },
        {DXGI_FORMAT_R11G11B10_FLOAT             ,DXGI_FORMAT_R11G11B10_FLOAT,              D3D11FCS_R11G11B10,     {11,11,10,0},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,X,         _FLOAT, _FLOAT, _FLOAT, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_R8G8B8A8_TYPELESS           ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8B8A8_UNORM          ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB     ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             TRUE,  1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB,  FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8B8A8_UINT           ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _UINT, _UINT, _UINT, _UINT,                          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8B8A8_SNORM          ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _SNORM, _SNORM, _SNORM, _SNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8B8A8_SINT           ,DXGI_FORMAT_R8G8B8A8_TYPELESS,            D3D11FCS_R8G8B8A8,      {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _SINT, _SINT, _SINT, _SINT,                          FALSE,   FALSE,  },
        {DXGI_FORMAT_R16G16_TYPELESS             ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16_FLOAT            ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _FLOAT, _FLOAT, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16_UNORM            ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _UNORM, _UNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16_UINT             ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _UINT, _UINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16_SNORM            ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _SNORM, _SNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16G16_SINT             ,DXGI_FORMAT_R16G16_TYPELESS,              D3D11FCS_R16G16,        {16,16,0,0},         32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _SINT, _SINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {DXGI_FORMAT_R32_TYPELESS                ,DXGI_FORMAT_R32_TYPELESS,                 D3D11FCS_R32,           {32,0,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,X,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_D32_FLOAT               ,DXGI_FORMAT_R32_TYPELESS,                 D3D11FCS_R32,           {32,0,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     D,X,X,X,         _FLOAT, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32_FLOAT               ,DXGI_FORMAT_R32_TYPELESS,                 D3D11FCS_R32,           {32,0,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _FLOAT, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32_UINT                ,DXGI_FORMAT_R32_TYPELESS,                 D3D11FCS_R32,           {32,0,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {    DXGI_FORMAT_R32_SINT                ,DXGI_FORMAT_R32_TYPELESS,                 D3D11FCS_R32,           {32,0,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _SINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {DXGI_FORMAT_R24G8_TYPELESS              ,DXGI_FORMAT_R24G8_TYPELESS,               D3D11FCS_R24G8,         {24,8,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_D24_UNORM_S8_UINT       ,DXGI_FORMAT_R24G8_TYPELESS,               D3D11FCS_R24G8,         {24,8,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     D,S,X,X,         _UNORM,_UINT,_TYPELESS,_TYPELESS,                    FALSE,   FALSE,  },
        {    DXGI_FORMAT_R24_UNORM_X8_TYPELESS   ,DXGI_FORMAT_R24G8_TYPELESS,               D3D11FCS_R24G8,         {24,8,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM,_TYPELESS,_TYPELESS,_TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_X24_TYPELESS_G8_UINT    ,DXGI_FORMAT_R24G8_TYPELESS,               D3D11FCS_R24G8,         {24,8,0,0},          32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     X,G,X,X,         _TYPELESS,_UINT,_TYPELESS,_TYPELESS,                 FALSE,   FALSE,  },
        {DXGI_FORMAT_R8G8_TYPELESS               ,DXGI_FORMAT_R8G8_TYPELESS,                D3D11FCS_R8G8,          {8,8,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8_UNORM              ,DXGI_FORMAT_R8G8_TYPELESS,                D3D11FCS_R8G8,          {8,8,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _UNORM, _UNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8_UINT               ,DXGI_FORMAT_R8G8_TYPELESS,                D3D11FCS_R8G8,          {8,8,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _UINT, _UINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8_SNORM              ,DXGI_FORMAT_R8G8_TYPELESS,                D3D11FCS_R8G8,          {8,8,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _SNORM, _SNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8G8_SINT               ,DXGI_FORMAT_R8G8_TYPELESS,                D3D11FCS_R8G8,          {8,8,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,X,X,         _SINT, _SINT, _TYPELESS, _TYPELESS,                  FALSE,   FALSE,  },
        {DXGI_FORMAT_R16_TYPELESS                ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,X,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16_FLOAT               ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _FLOAT, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_D16_UNORM               ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     D,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16_UNORM               ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16_UINT                ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16_SNORM               ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _SNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R16_SINT                ,DXGI_FORMAT_R16_TYPELESS,                 D3D11FCS_R16,           {16,0,0,0},          16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _SINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {DXGI_FORMAT_R8_TYPELESS                 ,DXGI_FORMAT_R8_TYPELESS,                  D3D11FCS_R8,            {8,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  R,X,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8_UNORM                ,DXGI_FORMAT_R8_TYPELESS,                  D3D11FCS_R8,            {8,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8_UINT                 ,DXGI_FORMAT_R8_TYPELESS,                  D3D11FCS_R8,            {8,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8_SNORM                ,DXGI_FORMAT_R8_TYPELESS,                  D3D11FCS_R8,            {8,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _SNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_R8_SINT                 ,DXGI_FORMAT_R8_TYPELESS,                  D3D11FCS_R8,            {8,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _SINT, _TYPELESS, _TYPELESS, _TYPELESS,              FALSE,   FALSE,  },
        {DXGI_FORMAT_A8_UNORM                    ,DXGI_FORMAT_A8_UNORM,                     D3D11FCS_A8,            {0,0,0,8},           8,              FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     X,X,X,A,         _TYPELESS, _TYPELESS, _TYPELESS, _UNORM,             FALSE,   FALSE,  },
        {DXGI_FORMAT_R1_UNORM                    ,DXGI_FORMAT_R1_UNORM,                     D3D11FCS_R1,            {1,0,0,0},           1,              FALSE, 8,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {DXGI_FORMAT_R9G9B9E5_SHAREDEXP          ,DXGI_FORMAT_R9G9B9E5_SHAREDEXP,           D3D11FCS_R9G9B9E5,      {0,0,0,0},           32,             FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _FLOAT, _FLOAT, _FLOAT, _FLOAT,                      FALSE,   FALSE,  },
        {DXGI_FORMAT_R8G8_B8G8_UNORM             ,DXGI_FORMAT_R8G8_B8G8_UNORM,              D3D11FCS_R8G8_B8G8,     {0,0,0,0},           16,             FALSE, 2,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_G8R8_G8B8_UNORM             ,DXGI_FORMAT_G8R8_G8B8_UNORM,              D3D11FCS_G8R8_G8B8,     {0,0,0,0},           16,             FALSE, 2,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_BC1_TYPELESS                ,DXGI_FORMAT_BC1_TYPELESS,                 D3D11FCS_BC1,           {0,0,0,0},           64,             FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC1_UNORM               ,DXGI_FORMAT_BC1_TYPELESS,                 D3D11FCS_BC1,           {0,0,0,0},           64,             FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC1_UNORM_SRGB          ,DXGI_FORMAT_BC1_TYPELESS,                 D3D11FCS_BC1,           {0,0,0,0},           64,             TRUE,  4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM,       FALSE,   FALSE,  },
        {DXGI_FORMAT_BC2_TYPELESS                ,DXGI_FORMAT_BC2_TYPELESS,                 D3D11FCS_BC2,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC2_UNORM               ,DXGI_FORMAT_BC2_TYPELESS,                 D3D11FCS_BC2,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC2_UNORM_SRGB          ,DXGI_FORMAT_BC2_TYPELESS,                 D3D11FCS_BC2,           {0,0,0,0},           128,            TRUE,  4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM,       FALSE,   FALSE,  },
        {DXGI_FORMAT_BC3_TYPELESS                ,DXGI_FORMAT_BC3_TYPELESS,                 D3D11FCS_BC3,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC3_UNORM               ,DXGI_FORMAT_BC3_TYPELESS,                 D3D11FCS_BC3,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC3_UNORM_SRGB          ,DXGI_FORMAT_BC3_TYPELESS,                 D3D11FCS_BC3,           {0,0,0,0},           128,            TRUE,  4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM,       FALSE,   FALSE,  },
        {DXGI_FORMAT_BC4_TYPELESS                ,DXGI_FORMAT_BC4_TYPELESS,                 D3D11FCS_BC4,           {0,0,0,0},           64,             FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,X,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC4_UNORM               ,DXGI_FORMAT_BC4_TYPELESS,                 D3D11FCS_BC4,           {0,0,0,0},           64,             FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC4_SNORM               ,DXGI_FORMAT_BC4_TYPELESS,                 D3D11FCS_BC4,           {0,0,0,0},           64,             FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _SNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   FALSE,  },
        {DXGI_FORMAT_BC5_TYPELESS                ,DXGI_FORMAT_BC5_TYPELESS,                 D3D11FCS_BC5,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,X,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC5_UNORM               ,DXGI_FORMAT_BC5_TYPELESS,                 D3D11FCS_BC5,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,X,X,         _UNORM, _UNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC5_SNORM               ,DXGI_FORMAT_BC5_TYPELESS,                 D3D11FCS_BC5,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,X,X,         _SNORM, _SNORM, _TYPELESS, _TYPELESS,                FALSE,   FALSE,  },
        {DXGI_FORMAT_B5G6R5_UNORM                ,DXGI_FORMAT_B5G6R5_UNORM,                 D3D11FCS_B5G6R5,        {5,6,5,0},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_B5G5R5A1_UNORM              ,DXGI_FORMAT_B5G5R5A1_UNORM,               D3D11FCS_B5G5R5A1,      {5,5,5,1},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {DXGI_FORMAT_B8G8R8A8_UNORM              ,DXGI_FORMAT_B8G8R8A8_TYPELESS,            D3D11FCS_B8G8R8A8_Win7, {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {DXGI_FORMAT_B8G8R8X8_UNORM              ,DXGI_FORMAT_B8G8R8X8_TYPELESS,            D3D11FCS_B8G8R8X8_Win7, {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM  ,DXGI_FORMAT_R10G10B10A2_TYPELESS,         D3D11FCS_R10G10B10A2_XR,{10,10,10,2},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     R,G,B,A,         _FIXED_2_8, _FIXED_2_8, _FIXED_2_8, _UNORM,          FALSE,   FALSE,  },
        {DXGI_FORMAT_B8G8R8A8_TYPELESS           ,DXGI_FORMAT_B8G8R8A8_TYPELESS,            D3D11FCS_B8G8R8A8_Win7, {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  B,G,R,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB     ,DXGI_FORMAT_B8G8R8A8_TYPELESS,            D3D11FCS_B8G8R8A8_Win7, {8,8,8,8},           32,             TRUE,  1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB,  FALSE,   FALSE,  },
        {DXGI_FORMAT_B8G8R8X8_TYPELESS           ,DXGI_FORMAT_B8G8R8X8_TYPELESS,            D3D11FCS_B8G8R8X8_Win7, {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_PARTIAL_TYPE,  B,G,R,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB     ,DXGI_FORMAT_B8G8R8X8_TYPELESS,            D3D11FCS_B8G8R8X8_Win7, {8,8,8,8},           32,             TRUE,  1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,X,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _TYPELESS,    FALSE,   FALSE,  },
        {DXGI_FORMAT_BC6H_TYPELESS               ,DXGI_FORMAT_BC6H_TYPELESS,                D3D11FCS_BC6H,          {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,B,X,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC6H_UF16               ,DXGI_FORMAT_BC6H_TYPELESS,                D3D11FCS_BC6H,          {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _FLOAT, _FLOAT, _FLOAT, _TYPELESS,                   FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC6H_SF16               ,DXGI_FORMAT_BC6H_TYPELESS,                D3D11FCS_BC6H,          {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _FLOAT, _FLOAT, _FLOAT, _TYPELESS,                   FALSE,   FALSE,  },
        {DXGI_FORMAT_BC7_TYPELESS                ,DXGI_FORMAT_BC7_TYPELESS,                 D3D11FCS_BC7,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_PARTIAL_TYPE,  R,G,B,A,         _TYPELESS, _TYPELESS, _TYPELESS, _TYPELESS,          FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC7_UNORM               ,DXGI_FORMAT_BC7_TYPELESS,                 D3D11FCS_BC7,           {0,0,0,0},           128,            FALSE, 4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
        {    DXGI_FORMAT_BC7_UNORM_SRGB          ,DXGI_FORMAT_BC7_TYPELESS,                 D3D11FCS_BC7,           {0,0,0,0},           128,            TRUE,  4,              4,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,A,         _UNORM_SRGB, _UNORM_SRGB, _UNORM_SRGB, _UNORM,       FALSE,   FALSE,  },
        // YUV 4:4:4 formats
        { DXGI_FORMAT_AYUV                       ,DXGI_FORMAT_AYUV,                         D3D11FCS_AYUV,          {8,8,8,8},           32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   TRUE,   },
        { DXGI_FORMAT_Y410                       ,DXGI_FORMAT_Y410,                         D3D11FCS_Y410,          {10,10,10,2},        32,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   TRUE,   },
        { DXGI_FORMAT_Y416                       ,DXGI_FORMAT_Y416,                         D3D11FCS_Y416,          {16,16,16,16},       64,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   TRUE,   },
        // YUV 4:2:0 formats
        { DXGI_FORMAT_NV12                       ,DXGI_FORMAT_NV12,                         D3D11FCS_NV12,          {0,0,0,0},           8,              FALSE, 2,              2,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             TRUE,    TRUE,   },
        { DXGI_FORMAT_P010                       ,DXGI_FORMAT_P010,                         D3D11FCS_P010,          {0,0,0,0},           16,             FALSE, 2,              2,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             TRUE,    TRUE,   },
        { DXGI_FORMAT_P016                       ,DXGI_FORMAT_P016,                         D3D11FCS_P016,          {0,0,0,0},           16,             FALSE, 2,              2,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             TRUE,    TRUE,   },
        { DXGI_FORMAT_420_OPAQUE                 ,DXGI_FORMAT_420_OPAQUE,                   D3D11FCS_420_OPAQUE,    {0,0,0,0},           8,              FALSE, 2,              2,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             TRUE,    TRUE,   },
        // YUV 4:2:2 formats
        { DXGI_FORMAT_YUY2                       ,DXGI_FORMAT_YUY2,                         D3D11FCS_YUY2,          {0,0,0,0},           16,             FALSE, 2,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   TRUE,   },
        { DXGI_FORMAT_Y210                       ,DXGI_FORMAT_Y210,                         D3D11FCS_Y210,          {0,0,0,0},           32,             FALSE, 2,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   TRUE,   },
        { DXGI_FORMAT_Y216                       ,DXGI_FORMAT_Y216,                         D3D11FCS_Y216,          {0,0,0,0},           32,             FALSE, 2,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,G,B,X,         _UNORM, _UNORM, _UNORM, _TYPELESS,                   FALSE,   TRUE,   },
        // YUV 4:1:1 formats
        { DXGI_FORMAT_NV11                       ,DXGI_FORMAT_NV11,                         D3D11FCS_NV11,          {0,0,0,0},           8,              FALSE, 4,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             TRUE,    TRUE,   },
        // Legacy substream formats
        { DXGI_FORMAT_AI44                       ,DXGI_FORMAT_AI44,                         D3D11FCS_AI44,          {0,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   TRUE,   },
        { DXGI_FORMAT_IA44                       ,DXGI_FORMAT_IA44,                         D3D11FCS_IA44,          {0,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   TRUE,   },
        { DXGI_FORMAT_P8                         ,DXGI_FORMAT_P8,                           D3D11FCS_P8,            {0,0,0,0},           8,              FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   TRUE,   },
        { DXGI_FORMAT_A8P8                       ,DXGI_FORMAT_A8P8,                         D3D11FCS_A8P8,          {0,0,0,0},           16,             FALSE, 1,              1,               1,                D3D11FL_CUSTOM,     D3D11FTL_FULL_TYPE,     R,X,X,X,         _UNORM, _TYPELESS, _TYPELESS, _TYPELESS,             FALSE,   TRUE,   },
        // 
        { DXGI_FORMAT_B4G4R4A4_UNORM             ,DXGI_FORMAT_B4G4R4A4_UNORM,               D3D11FCS_B4G4R4A4,      {4,4,4,4},           16,             FALSE, 1,              1,               1,                D3D11FL_STANDARD,   D3D11FTL_FULL_TYPE,     B,G,R,A,         _UNORM, _UNORM, _UNORM, _UNORM,                      FALSE,   FALSE,  },
    };

    const UINT s_NumFormats = (sizeof(s_FormatDetail)/sizeof(FORMAT_DETAIL));

    UINT GetDetailTableIndex(DXGI_FORMAT  Format );
    UINT GetBitsPerUnit(DXGI_FORMAT Format);
    BOOL YUV(DXGI_FORMAT Format);

    DXGI_FORMAT d3d12_convert_pipe_video_profile_to_dxgi_format(enum pipe_video_profile profile);
}
#endif
// End of file