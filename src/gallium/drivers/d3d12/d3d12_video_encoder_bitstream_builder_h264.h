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

#ifndef D3D12_VIDEO_ENC_BITSTREAM_BUILDER_H264_H
#define D3D12_VIDEO_ENC_BITSTREAM_BUILDER_H264_H

#include "d3d12_video_encoder_nalu_writer_h264.h"

class H264BitstreamBuilder
{

public:
    H264BitstreamBuilder() { };
    ~H264BitstreamBuilder() { };

    void BuildSPS
    (
        const D3D12_VIDEO_ENCODER_PROFILE_H264& profile,
        const D3D12_VIDEO_ENCODER_LEVELS_H264& level,
        const DXGI_FORMAT& inputFmt,
        const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264& codecConfig,
        const D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264& gopConfig,
        UINT seq_parameter_set_id,
        UINT max_num_ref_frames,
        D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC sequenceTargetResolution,
        std::vector<BYTE> &headerBitstream,
        std::vector<BYTE>::iterator placingPositionStart,
        size_t &writtenBytes
    );

    void BuildPPS
    (
        const D3D12_VIDEO_ENCODER_PROFILE_H264& profile,
        const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264& codecConfig,
        const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264& pictureControl,
        UINT pic_parameter_set_id,
        UINT seq_parameter_set_id,
        std::vector<BYTE> &headerBitstream,
        std::vector<BYTE>::iterator placingPositionStart,
        size_t &writtenBytes
    );   

    void WriteEndOfStreamNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);
    void WriteEndOfSequenceNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);

    void PrintPPS(const H264_PPS& pps);
    void PrintSPS(const H264_SPS& sps);

    UINT m_writtenSPSCount = 0;
    UINT m_writtenPPSCount = 0;

    UINT GetSPSCount() { return m_writtenSPSCount; };
    UINT GetPPSCount() { return m_writtenPPSCount; };

private:
    H264NaluWriter m_h264Encoder;
};

#endif