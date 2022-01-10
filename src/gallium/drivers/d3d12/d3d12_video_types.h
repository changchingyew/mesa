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

#ifndef D3D12_VIDEO_TYPES_H
#define D3D12_VIDEO_TYPES_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <stdarg.h>
#include <memory>
#include <vector>
#include <functional>

#include "pipe/p_context.h"
#include "pipe/p_video_codec.h"
#include "d3d12_fence.h"
 
#include "DXGIColorSpaceHelper.h"

#include <dxguids/dxguids.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// The following defines can be tweaked for better code performance or debug verbosity

#define D3D12_VIDEO_ENHANCED_DEBUGGING true

#if D3D12_VIDEO_ENHANCED_DEBUGGING
    #define D3D12_LOG_DBG_ON true
    #define D3D12_VALIDATE_DEVICE_REMOVED true
#else
    #define D3D12_LOG_DBG_ON false
    #define D3D12_VALIDATE_DEVICE_REMOVED false
#endif

#define VERIFY_DEVICE_NOT_REMOVED(videoObj) { if(D3D12_VALIDATE_DEVICE_REMOVED){ HRESULT hr = videoObj->m_pD3D12Screen->dev->GetDeviceRemovedReason(); if(FAILED(hr)) { D3D12_LOG_ERROR("D3D12 Device was removed with HR %x\n", hr); } } }
#define D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL(fnName, errMsg, unsupportedVal) { D3D12_LOG_ERROR("[D3D12 Video Driver] Function %s - %s with value: %d", fnName, errMsg, unsupportedVal); }

#define D3D12_LOG_DBG(args...) if(D3D12_LOG_DBG_ON) debug_printf(args);
#define D3D12_LOG_INFO(args...) debug_printf(args);
#define D3D12_LOG_ERROR(args...) { D3D12_LOG_INFO("\n\t"); D3D12_LOG_INFO(args); D3D12_LOG_INFO("\t[D3D12 Video Driver Error] - Exiting program execution with code %d after error in %s:%d]\n", EXIT_FAILURE, __FILE__ , __LINE__); exit(EXIT_FAILURE); } 
#define VERIFY_SUCCEEDED(x) { HRESULT hr = (x); if(FAILED(hr)) { D3D12_LOG_ERROR("[D3D12 Video Driver Error] VERIFY_SUCCEEDED(%s) failed with HR %x\n", #x, hr); } }
#define VERIFY_IS_TRUE(x) { bool evaluationRes = (x); if(!evaluationRes) { D3D12_LOG_ERROR("[D3D12 Video Driver Error] VERIFY_IS_TRUE(%s) failed\n", #x); } }
#define VERIFY_ARE_EQUAL(a, b) { if((a) != (b)) { D3D12_LOG_ERROR("[D3D12 Video Driver Error] VERIFY_ARE_EQUAL(%s, %s) failed\n", #a, #b); } }
#define VERIFY_IS_LESS_THAN_OR_EQUAL(a, b) { if((a) > (b)) { D3D12_LOG_ERROR("[D3D12 Video Driver Error] VERIFY_IS_LESS_THAN_OR_EQUAL(%s, %s) failed\n", #a, #b); } }

typedef enum {
    D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_NONE = 0,
    D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ALIGNMENT_HEIGHT = 1 << 12,       // set by accelerator
    D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ARRAY_OF_TEXTURES = 1 << 14,      // set by accelerator
    D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REUSE_DECODER = 1 << 15,          // set by accelerator - This bit means that the decoder can be re-used with resolution change and bit depth change (including profile GUID change from 8bit to 10bit and vice versa).
    D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED = 1 << 30, // custom created for WSL
} D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_FLAGS;

typedef enum 
{
    D3D12_VIDEO_DECODE_PROFILE_TYPE_NONE,
    D3D12_VIDEO_DECODE_PROFILE_TYPE_H264,
    D3D12_VIDEO_DECODE_PROFILE_TYPE_MAX_VALID
} D3D12_VIDEO_DECODE_PROFILE_TYPE;

typedef enum {
    VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_8_BIT = 0,
    VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_10_BIT = 1,
    VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_16_BIT = 2,

    VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_MAX, // Keep at end to inform array size.
} VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX;

typedef enum {
    VIDEO_DECODE_PROFILE_BIT_DEPTH_NONE = 0, 
    VIDEO_DECODE_PROFILE_BIT_DEPTH_8_BIT = (1 << VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_8_BIT),
    VIDEO_DECODE_PROFILE_BIT_DEPTH_10_BIT = (1 << VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_10_BIT),
    VIDEO_DECODE_PROFILE_BIT_DEPTH_16_BIT = (1 << VIDEO_DECODE_PROFILE_BIT_DEPTH_INDEX_16_BIT),
} VIDEO_DECODE_PROFILE_BIT_DEPTH;

typedef struct D3D12DPBDescriptor
{
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    UINT64 Width = 0;
    UINT Height = 0;
    bool fArrayOfTexture = false;
    bool fReferenceOnly = false;
    UINT16 dpbSize = 0;
    UINT m_NodeMask = 0;
} D3D12DPBDescriptor;

typedef struct D3D12VideoDecodeOutputConversionArguments
{
    BOOL Enable;
    DXGI_COLOR_SPACE_TYPE OutputColorSpace;
    D3D12_VIDEO_SAMPLE ReferenceInfo;
    UINT ReferenceFrameCount;
} D3D12VideoDecodeOutputConversionArguments;

void d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12, UINT &specLevel, UINT &constraint_set3_flag);
D3D12_VIDEO_ENCODER_PROFILE_H264 d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile);
D3D12_VIDEO_ENCODER_CODEC d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(enum pipe_video_profile profile);
GUID d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(enum pipe_video_profile profile);

#pragma GCC diagnostic pop

#endif
