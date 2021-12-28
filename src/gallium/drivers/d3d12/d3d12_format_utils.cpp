// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#include "d3d12_format_utils.h"

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
                assert(0); // Unusupported for now.
                return DXGI_FORMAT_P010;
            default:
                assert(0);
                return DXGI_FORMAT_UNKNOWN;
        }
    }
}
// End of file