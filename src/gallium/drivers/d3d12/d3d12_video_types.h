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

#include <stdarg.h>
#include <memory>
#include <vector>
#include <functional>

#include "pipe/p_context.h"
#include "pipe/p_video_codec.h"
#include "d3d12_fence.h"
#include "d3d12_debug.h"

#include <dxguids/dxguids.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// Allow encoder to continue the encoding session when an optional 
// rate control mode such as the following is used but not supported
//
// D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES
// D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE
//
// If setting this OS Env variable to true, the encoding process will continue, disregarding the settings
// requested for the optional RC mode
//

const bool D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG = debug_get_bool_option("D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG", false);

/* For CBR mode, to guarantee bitrate of generated stream complies with
* target bitrate (e.g. no over +/-10%), vbv_buffer_size should be same
* as target bitrate. Controlled by OS env var D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE
*/
const bool D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE = debug_get_bool_option("D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE", true);

// Allow encoder to continue the encoding session when aa slice mode 
// is requested but not supported.
//
// If setting this OS Env variable to true, the encoder will try to adjust to the closest slice
// setting available and encode using that configuration anyway
//
const bool D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG = debug_get_bool_option("D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG", false);

constexpr unsigned int D3D12_VIDEO_H264_MB_IN_PIXELS = 16;

// The following defines can be tweaked for better code performance or debug verbosity

#define D3D12_VIDEO_ENHANCED_DEBUGGING 1

#if D3D12_VIDEO_ENHANCED_DEBUGGING
#define D3D12_VALIDATE_DEVICE_REMOVED true
#else
#define D3D12_VALIDATE_DEVICE_REMOVED false
#endif

#define D3D12_DEVICE_REMOVED_EXITCODE 10 // Special process exit code to inform device removed
#define VERIFY_DEVICE_NOT_REMOVED(videoObj)                                                                            \
   {                                                                                                                   \
      if (D3D12_VALIDATE_DEVICE_REMOVED) {                                                                             \
         HRESULT hr = videoObj->m_pD3D12Screen->dev->GetDeviceRemovedReason();                                         \
         if (FAILED(hr)) {                                                                                             \
            D3D12_LOG_ERROR_WITH_EXIT_CODE(D3D12_DEVICE_REMOVED_EXITCODE, "D3D12 Device was removed with HR %x\n", hr);\
         }                                                                                                             \
      }                                                                                                                \
   }
#define D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL(fnName, errMsg, unsupportedVal)                                       \
   {                                                                                                                   \
      D3D12_LOG_ERROR("[D3D12 Video Driver] Function %s - %s with value: %d", fnName, errMsg, unsupportedVal);         \
   }

#define D3D12_LOG_DBG(args...)                                                                                         \
   if (D3D12_DEBUG_VERBOSE & d3d12_debug)                                                                              \
      debug_printf(args);
#define D3D12_LOG_INFO(args...) debug_printf(args);

#define D3D12_LOG_ERROR(args...) D3D12_LOG_ERROR_WITH_EXIT_CODE(EXIT_FAILURE, args)

#define D3D12_LOG_ERROR_WITH_EXIT_CODE(exitcode, args...)                                                              \
   {                                                                                                                   \
      D3D12_LOG_INFO("\n\t");                                                                                          \
      D3D12_LOG_INFO(args);                                                                                            \
      D3D12_LOG_INFO("\t[D3D12 Video Driver Error] - Exiting program execution with code %d after error in %s:%d]\n",  \
                     exitcode,                                                                                         \
                     __FILE__,                                                                                         \
                     __LINE__);                                                                                        \
      exit(exitcode);                                                                                                  \
   }
#define VERIFY_SUCCEEDED(x)                                                                                            \
   {                                                                                                                   \
      HRESULT hr = (x);                                                                                                \
      if (FAILED(hr)) {                                                                                                \
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] VERIFY_SUCCEEDED(%s) failed with HR %x\n", #x, hr);               \
      }                                                                                                                \
   }

typedef enum
{
   d3d12_video_decode_config_specific_flag_none              = 0,
   d3d12_video_decode_config_specific_flag_alignment_height  = 1 << 12,   // set by accelerator
   d3d12_video_decode_config_specific_flag_array_of_textures = 1 << 14,   // set by accelerator
   d3d12_video_decode_config_specific_flag_reuse_decoder =
      1 << 15,   // set by accelerator - This bit means that the decoder can be re-used with resolution change and bit
                 // depth change (including profile GUID change from 8bit to 10bit and vice versa).
   d3d12_video_decode_config_specific_flag_reference_only_textures_required = 1 << 30,   // custom created for WSL
} d3d12_video_decode_config_specific_flags;

typedef enum
{
   d3d12_video_decode_profile_type_none,
   d3d12_video_decode_profile_type_h264,
   d3d12_video_decode_profile_type_max_valid
} d3d12_video_decode_profile_type;

typedef struct d3d12_video_decode_dpb_descriptor
{
   DXGI_FORMAT Format          = DXGI_FORMAT_UNKNOWN;
   uint64_t    Width           = 0;
   uint32_t    Height          = 0;
   bool        fArrayOfTexture = false;
   bool        fReferenceOnly  = false;
   uint16_t    dpbSize         = 0;
   uint32_t    m_NodeMask      = 0;
} d3d12_video_decode_dpb_descriptor;

typedef struct d3d12_video_decode_output_conversion_arguments
{
   BOOL                  Enable;
   DXGI_COLOR_SPACE_TYPE OutputColorSpace;
   D3D12_VIDEO_SAMPLE    ReferenceInfo;
   uint32_t              ReferenceFrameCount;
} d3d12_video_decode_output_conversion_arguments;

void
d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12,
                                                  uint32_t &                      specLevel,
                                                  uint32_t &                      constraint_set3_flag);
D3D12_VIDEO_ENCODER_PROFILE_H264
d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile);
D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(enum pipe_video_profile profile);
GUID
d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(enum pipe_video_profile profile);

#endif
