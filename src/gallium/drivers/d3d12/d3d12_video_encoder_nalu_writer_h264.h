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

#ifndef D3D12_VIDEO_ENC_NALU_WRITER_H264_H
#define D3D12_VIDEO_ENC_NALU_WRITER_H264_H

#include "d3d12_video_encoder_bitstream.h"

typedef enum
{
    NAL_REFIDC_REF          =   3,
    NAL_REFIDC_NONREF       =   0
} H264_NALREF_IDC;

typedef enum
{
    NAL_TYPE_UNSPECIFIED                =   0,
    NAL_TYPE_SLICE                      =   1,
    NAL_TYPE_SLICEDATA_A                =   2,
    NAL_TYPE_SLICEDATA_B                =   3,
    NAL_TYPE_SLICEDATA_C                =   4,
    NAL_TYPE_IDR                        =   5,
    NAL_TYPE_SEI                        =   6,
    NAL_TYPE_SPS                        =   7,
    NAL_TYPE_PPS                        =   8,
    NAL_TYPE_ACCESS_UNIT_DEMILITER      =   9,
    NAL_TYPE_END_OF_SEQUENCE            =   10,
    NAL_TYPE_END_OF_STREAM              =   11,
    NAL_TYPE_FILLER_DATA                =   12,
    NAL_TYPE_SPS_EXTENSION              =   13,
    NAL_TYPE_PREFIX                     =   14,
    /* 15...18 RESERVED */
    NAL_TYPE_AUXILIARY_SLICE            =   19,
    /* 20...23 RESERVED */
    /* 24...31 UNSPECIFIED */
} H264_NALU_TYPE;

typedef struct H264_SPS
{
    UINT profile_idc;
    UINT constraint_set3_flag;
    UINT level_idc;
    UINT seq_parameter_set_id;
    UINT bit_depth_luma_minus8;
    UINT bit_depth_chroma_minus8;
    UINT log2_max_frame_num_minus4;
    UINT pic_order_cnt_type;
    UINT log2_max_pic_order_cnt_lsb_minus4;
    UINT max_num_ref_frames;
    UINT gaps_in_frame_num_value_allowed_flag;
    UINT pic_width_in_mbs_minus1;
    UINT pic_height_in_map_units_minus1;
    UINT direct_8x8_inference_flag;
    UINT frame_cropping_flag;
    UINT frame_cropping_rect_left_offset;
    UINT frame_cropping_rect_right_offset;
    UINT frame_cropping_rect_top_offset;
    UINT frame_cropping_rect_bottom_offset;
} H264_SPS;

typedef struct H264_PPS
{
    UINT pic_parameter_set_id;
    UINT seq_parameter_set_id;
    UINT entropy_coding_mode_flag;
    UINT pic_order_present_flag;
    UINT num_ref_idx_l0_active_minus1;
    UINT num_ref_idx_l1_active_minus1;
    UINT constrained_intra_pred_flag;
    UINT transform_8x8_mode_flag;
} H264_PPS;

typedef enum H264_SPEC_PROFILES
{
    H264_PROFILE_MAIN            =   77,
    H264_PROFILE_HIGH            =   100,
    H264_PROFILE_HIGH10          =   110,
} H264_SPEC_PROFILES;


#define MAX_COMPRESSED_PPS 256
#define MAX_COMPRESSED_SPS 256

class D3D12VideoNaluWriterH264
{
public:
    D3D12VideoNaluWriterH264() { }
    ~D3D12VideoNaluWriterH264() { }

    // Writes the H264 SPS structure into a bitstream passed in headerBitstream
    // Function resizes bitstream accordingly and puts result in byte vector
    void SPSToNALUBytes(H264_SPS *pSPS, std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);

    // Writes the H264 PPS structure into a bitstream passed in headerBitstream
    // Function resizes bitstream accordingly and puts result in byte vector
    void PPSToNALUBytes(H264_PPS *pPPS, std::vector<BYTE> &headerBitstream, BOOL bIsFREXTProfile, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);

    void WriteEndOfStreamNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);
    void WriteEndOfSequenceNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t &writtenBytes);

private:
    // Writes from structure into bitstream with RBSP trailing but WITHOUT NAL unit wrap (eg. nal_idc_type, etc)
    UINT32 WriteSPSBytes (D3D12VideoBitstream *pBitstream, H264_SPS *pSPS);
    UINT32 WritePPSBytes (D3D12VideoBitstream *pBitstream, H264_PPS *pPPS, BOOL bIsFREXTProfile);

    // Adds NALU wrapping into structures and ending NALU control bits
    UINT32 WrapSPSNalu (D3D12VideoBitstream *pNALU, D3D12VideoBitstream *pRBSP);
    UINT32 WrapPPSNalu (D3D12VideoBitstream *pNALU, D3D12VideoBitstream *pRBSP);
    
    // Helpers
    void WriteNaluEnd (D3D12VideoBitstream *pNALU);
    void RBSPTrailing (D3D12VideoBitstream *pBitstream);
    UINT32 WrapRbspIntoNalu (D3D12VideoBitstream *pNALU, D3D12VideoBitstream *pRBSP, UINT iNaluIdc, UINT iNaluType);
};

#endif