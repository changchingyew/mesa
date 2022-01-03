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

#include "d3d12_video_encoder_bitstream_builder_h264.h"

inline H264_SPEC_PROFILES Convert12ToSpecH264Profiles(D3D12_VIDEO_ENCODER_PROFILE_H264 profile12)
{
    switch (profile12)
    {
        case D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN:
        {
            return H264_PROFILE_MAIN;
        } break;
        case D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH:
        {
            return H264_PROFILE_HIGH;
        } break;
        case D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10:
        {
            return H264_PROFILE_HIGH10;
        } break;
        default:
        {
            D3D12_LOG_ERROR("[D3D12 D3D12VideoBitstreamBuilderH264] Not a supported profile");
            return static_cast<H264_SPEC_PROFILES>(0);
        } break;            
    }
}

void D3D12VideoBitstreamBuilderH264::BuildSPS
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
)
{
    H264_SPEC_PROFILES profile_idc = Convert12ToSpecH264Profiles(profile);
    UINT constraint_set3_flag = 0;
    UINT level_idc = 0;
    d3d12_video_encoder_convert_from_d3d12_level_h264(level, level_idc, constraint_set3_flag/*Always 0 except if level is 11 or 1b in which case 0 means 11, 1 means 1b*/);

    // constraint_set3_flag is for Main profile only and levels 11 or 1b: levels 11 if off, level 1b if on. Always 0 for HIGH/HIGH10 profiles
    if((profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH) || (profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10))
    {
        // Force 0 for high profiles
        constraint_set3_flag = 0;
    }

    VERIFY_IS_TRUE((inputFmt == DXGI_FORMAT_NV12) ||
        (inputFmt == DXGI_FORMAT_P010)); 

    // Assume NV12 YUV 420 8 bits
    UINT bit_depth_luma_minus8 = 0;
    UINT bit_depth_chroma_minus8 = 0;

    // In case is 420 10 bits fix it
    if(inputFmt == DXGI_FORMAT_P010)
    {
        bit_depth_luma_minus8 = 2;
        bit_depth_chroma_minus8 = 2;
    }

    // Calculate sequence resolution sizes in MBs
    // Always in MBs since we don't support interlace in D3D12 Encode
    UINT pic_width_in_mbs_minus1 = static_cast<UINT>(std::ceil(sequenceTargetResolution.Width / 16.0)) - 1;
    UINT pic_height_in_map_units_minus1 = static_cast<UINT>(std::ceil(sequenceTargetResolution.Height / 16.0)) - 1;

    // Calculate macroblock aligned resolution
    UINT alignedWidth = static_cast<UINT>(std::ceil(sequenceTargetResolution.Width / 16.0)) * 16;
    UINT alignedHeight = static_cast<UINT>(std::ceil(sequenceTargetResolution.Height / 16.0)) * 16;

    INT32 iCropRight = alignedWidth - sequenceTargetResolution.Width;
    INT32 iCropBottom = alignedHeight - sequenceTargetResolution.Height;

    UINT frame_cropping_flag = 0;
    UINT frame_cropping_rect_right_offset = 0;
    UINT frame_cropping_rect_bottom_offset = 0;

    if (iCropRight || iCropBottom)
    {
        frame_cropping_flag = 1;
        frame_cropping_rect_right_offset = iCropRight / 2;
        frame_cropping_rect_bottom_offset = iCropBottom / 2;
    }

    H264_SPS spsStructure = 
    {
        static_cast<UINT>(profile_idc),
        constraint_set3_flag, 
        level_idc,
        seq_parameter_set_id,
        bit_depth_luma_minus8,
        bit_depth_chroma_minus8,
        gopConfig.log2_max_frame_num_minus4,
        gopConfig.pic_order_cnt_type,
        gopConfig.log2_max_pic_order_cnt_lsb_minus4,
        max_num_ref_frames,
        0, // gaps_in_frame_num_value_allowed_flag
        pic_width_in_mbs_minus1,
        pic_height_in_map_units_minus1,
        ((codecConfig.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_ADAPTIVE_8x8_TRANSFORM) != 0) ? 1u : 0u, // direct_8x8_inference_flag
        frame_cropping_flag,    
        0, // frame_cropping_rect_left_offset
        frame_cropping_rect_right_offset,       
        0, // frame_cropping_rect_top_offset
        frame_cropping_rect_bottom_offset
    };

    // Print built PPS structure
    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_SPS Structure generated before writing to bitstream:\n");
    PrintSPS(spsStructure);

    // Convert the H264 SPS structure into bytes
    m_h264Encoder.SPSToNALUBytes(&spsStructure, headerBitstream, placingPositionStart, writtenBytes);
    m_writtenSPSCount++;
}   

void D3D12VideoBitstreamBuilderH264::WriteEndOfStreamNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart,size_t &writtenBytes)
{
    m_h264Encoder.WriteEndOfStreamNALU(headerBitstream, placingPositionStart, writtenBytes);
}

void D3D12VideoBitstreamBuilderH264::WriteEndOfSequenceNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes)
{
    m_h264Encoder.WriteEndOfSequenceNALU(headerBitstream, placingPositionStart, writtenBytes);
}

void D3D12VideoBitstreamBuilderH264::BuildPPS
(
    const D3D12_VIDEO_ENCODER_PROFILE_H264& profile,
    const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264& codecConfig,
    const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264& pictureControl,
    UINT pic_parameter_set_id,
    UINT seq_parameter_set_id,
    std::vector<BYTE> &headerBitstream,
    std::vector<BYTE>::iterator placingPositionStart,
    size_t &writtenBytes
)
{
    BOOL bIsHighProfile = ((profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH)
        || (profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10));

    H264_PPS ppsStructure = 
    {
        pic_parameter_set_id,
        seq_parameter_set_id,
        ((codecConfig.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_ENABLE_CABAC_ENCODING) != 0) ? 1u : 0u, // entropy_coding_mode_flag
        0, // pic_order_present_flag (bottom_field_pic_order_in_frame_present_flag) - will use pic_cnt 0 or 2, always off ; used with pic_cnt_type 1 and deltas.
        static_cast<UINT>(std::max(static_cast<INT>(pictureControl.List0ReferenceFramesCount) - 1, 0)), // num_ref_idx_l0_active_minus1
        static_cast<UINT>(std::max(static_cast<INT>(pictureControl.List1ReferenceFramesCount) - 1, 0)), // num_ref_idx_l1_active_minus1
        ((codecConfig.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_CONSTRAINED_INTRAPREDICTION) != 0) ? 1u : 0u, // constrained_intra_pred_flag
        ((codecConfig.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_ADAPTIVE_8x8_TRANSFORM) != 0) ? 1u : 0u // transform_8x8_mode_flag
    };

    // Print built PPS structure
    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_PPS Structure generated before writing to bitstream:\n");
    PrintPPS(ppsStructure);

    // Convert the H264 SPS structure into bytes
    m_h264Encoder.PPSToNALUBytes(&ppsStructure, headerBitstream, bIsHighProfile, placingPositionStart, writtenBytes);
    m_writtenPPSCount++;
}

void D3D12VideoBitstreamBuilderH264::PrintPPS(const H264_PPS& pps)
{
    // Be careful that BuildPPS also wraps some other NALU bytes in PPSToNALUBytes so bitstream returned by BuildPPS won't be exactly the bytes from the H264_PPS struct
    
    static_assert(sizeof(H264_PPS) == (sizeof(UINT)*8)); // Update the number of UINT in struct in assert and add case below if structure changes

    // Declared fields from definition in d3d12_video_encoder_bitstream_builder_h264.h

    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_PPS values below:\n");
    D3D12_LOG_DBG("pic_parameter_set_id: %d\n", pps.pic_parameter_set_id);
    D3D12_LOG_DBG("seq_parameter_set_id: %d\n", pps.seq_parameter_set_id);
    D3D12_LOG_DBG("entropy_coding_mode_flag: %d\n", pps.entropy_coding_mode_flag);
    D3D12_LOG_DBG("pic_order_present_flag: %d\n", pps.pic_order_present_flag);
    D3D12_LOG_DBG("num_ref_idx_l0_active_minus1: %d\n", pps.num_ref_idx_l0_active_minus1);
    D3D12_LOG_DBG("num_ref_idx_l1_active_minus1: %d\n", pps.num_ref_idx_l1_active_minus1);
    D3D12_LOG_DBG("constrained_intra_pred_flag: %d\n", pps.constrained_intra_pred_flag);
    D3D12_LOG_DBG("transform_8x8_mode_flag: %d\n", pps.transform_8x8_mode_flag);
    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_PPS values end\n--------------------------------------\n");
}

void D3D12VideoBitstreamBuilderH264::PrintSPS(const H264_SPS& sps)
{
    // Be careful when calling this method that BuildSPS also wraps some other NALU bytes in SPSToNALUBytes so bitstream returned by BuildSPS won't be exactly the bytes from the H264_SPS struct
    // From definition in d3d12_video_encoder_bitstream_builder_h264.h

    static_assert(sizeof(H264_SPS) == (sizeof(UINT)*19)); // Update the number of UINT in struct in assert and add case below if structure changes   

// Declared fields from definition in d3d12_video_encoder_bitstream_builder_h264.h

    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_SPS values below:\n");
    D3D12_LOG_DBG("profile_idc: %d\n", sps.profile_idc);
    D3D12_LOG_DBG("constraint_set3_flag: %d\n", sps.constraint_set3_flag);
    D3D12_LOG_DBG("level_idc: %d\n", sps.level_idc);
    D3D12_LOG_DBG("seq_parameter_set_id: %d\n", sps.seq_parameter_set_id);
    D3D12_LOG_DBG("bit_depth_luma_minus8: %d\n", sps.bit_depth_luma_minus8);
    D3D12_LOG_DBG("bit_depth_chroma_minus8: %d\n", sps.bit_depth_chroma_minus8);
    D3D12_LOG_DBG("log2_max_frame_num_minus4: %d\n", sps.log2_max_frame_num_minus4);
    D3D12_LOG_DBG("pic_order_cnt_type: %d\n", sps.pic_order_cnt_type);
    D3D12_LOG_DBG("log2_max_pic_order_cnt_lsb_minus4: %d\n", sps.log2_max_pic_order_cnt_lsb_minus4);
    D3D12_LOG_DBG("max_num_ref_frames: %d\n", sps.max_num_ref_frames);
    D3D12_LOG_DBG("gaps_in_frame_num_value_allowed_flag: %d\n", sps.gaps_in_frame_num_value_allowed_flag);
    D3D12_LOG_DBG("pic_width_in_mbs_minus1: %d\n", sps.pic_width_in_mbs_minus1);
    D3D12_LOG_DBG("pic_height_in_map_units_minus1: %d\n", sps.pic_height_in_map_units_minus1);
    D3D12_LOG_DBG("direct_8x8_inference_flag: %d\n", sps.direct_8x8_inference_flag);
    D3D12_LOG_DBG("frame_cropping_flag: %d\n", sps.frame_cropping_flag);
    D3D12_LOG_DBG("frame_cropping_rect_left_offset: %d\n", sps.frame_cropping_rect_left_offset);
    D3D12_LOG_DBG("frame_cropping_rect_right_offset: %d\n", sps.frame_cropping_rect_right_offset);
    D3D12_LOG_DBG("frame_cropping_rect_top_offset: %d\n", sps.frame_cropping_rect_top_offset);
    D3D12_LOG_DBG("frame_cropping_rect_bottom_offset: %d\n", sps.frame_cropping_rect_bottom_offset);
    D3D12_LOG_DBG("[D3D12 D3D12VideoBitstreamBuilderH264] H264_SPS values end\n--------------------------------------\n");
}