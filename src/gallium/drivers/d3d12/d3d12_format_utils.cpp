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

#include "d3d12_format_utils.h"
#include "d3d12_video_types.h"
#include "assert.h"
#include "util/macros.h"

#define D3DFORMATDESC 1

#define MAP_ALIGN_REQUIREMENT 16 // Map is required to return 16-byte aligned addresses

namespace D3D12VideoFormatHelper
{
    UINT GetDetailTableIndex(DXGI_FORMAT  Format )
    {
        if( (UINT)Format < ARRAY_SIZE( s_FormatDetail ) )
        {
            assert( s_FormatDetail[(UINT)Format].DXGIFormat == Format );
            return static_cast<UINT>(Format);
        }

        return (UINT)-1;
    }

    // Converts the sequential component index (range from 0 to GetNumComponentsInFormat()) to
    // the absolute component index (range 0 to 3).
    // GetBitsPerUnit - returns bits per pixel unless format is a block compress format then it returns bits per block. 
    // use IsBlockCompressFormat() to determine if block size is returned.
    UINT GetBitsPerUnit(DXGI_FORMAT Format)
    {
        return s_FormatDetail[GetDetailTableIndex( Format )].BitsPerUnit;
    }

    BOOL YUV(DXGI_FORMAT Format)
    {
        return s_FormatDetail[GetDetailTableIndex( Format )].bYUV;
    }

    DXGI_FORMAT d3d12_convert_pipe_video_profile_to_dxgi_format(enum pipe_video_profile profile)
    {
        switch (profile)
        {
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
                return DXGI_FORMAT_NV12;
            case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
                return DXGI_FORMAT_P010;
            default:
            {
                D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_convert_pipe_video_profile_to_dxgi_format", "Unsupported profile", profile);
                return DXGI_FORMAT_UNKNOWN;
            } break;
        }
    }
}
// End of file