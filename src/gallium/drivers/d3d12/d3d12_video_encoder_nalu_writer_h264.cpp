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

#include "d3d12_video_encoder_nalu_writer_h264.h"
#include <algorithm>

void H264NaluWriter::RBSPTrailing(CBitStream *pBitstream)
{
    pBitstream->PutBits(1, 1);
    INT32 iLeft = pBitstream->GetNumBitsForByteAlign();

    if (iLeft)
    {
        pBitstream->PutBits(iLeft, 0);
    }

    assert(pBitstream->IsByteAligned());
}

UINT32 H264NaluWriter::WriteSPSBytes(CBitStream *pBitstream, H264_SPS *pSPS)
{
    INT32 iBytesWritten = pBitstream->GetByteCount();

    // Standard constraint to be between 0 and 31 inclusive
    assert(pSPS->seq_parameter_set_id >= 0);
    assert(pSPS->seq_parameter_set_id < 32);

    pBitstream->PutBits(8, pSPS->profile_idc);
    pBitstream->PutBits(1, 0); // constraint_set0_flag
    pBitstream->PutBits(1, 0); // constraint_set1_flag
    pBitstream->PutBits(1, 0); // constraint_set2_flag
    pBitstream->PutBits(1, pSPS->constraint_set3_flag);
    pBitstream->PutBits(1, 0); // constraint_set4_flag
    pBitstream->PutBits(1, 0); // constraint_set5_flag
    pBitstream->PutBits(2, 0);
    pBitstream->PutBits(8, pSPS->level_idc);
    pBitstream->Exp_Golomb_ue(pSPS->seq_parameter_set_id);

    // Only support profiles defined in D3D12 Video Encode
    // If adding new profile support, check that the chroma_format_idc and bit depth are set correctly below
    // for the new additions
    assert((pSPS->profile_idc == H264_PROFILE_MAIN) ||
        (pSPS->profile_idc == H264_PROFILE_HIGH) ||
        (pSPS->profile_idc == H264_PROFILE_HIGH10)); 

    if((pSPS->profile_idc == H264_PROFILE_HIGH) || (pSPS->profile_idc == H264_PROFILE_HIGH10))
    {
        // chroma_format_idc always 4.2.0
        pBitstream->Exp_Golomb_ue(1);
        // Assume no separate_colour_plane_flag given chroma_format_idc = 1
        pBitstream->Exp_Golomb_ue(pSPS->bit_depth_luma_minus8);
        pBitstream->Exp_Golomb_ue(pSPS->bit_depth_chroma_minus8);
        // qpprime_y_zero_transform_bypass_flag
        pBitstream->PutBits(1, 0);
        // seq_scaling_matrix_present_flag)
        pBitstream->PutBits(1, 0);
    }

    pBitstream->Exp_Golomb_ue(pSPS->log2_max_frame_num_minus4);
    
    pBitstream->Exp_Golomb_ue(pSPS->pic_order_cnt_type);
    if(pSPS->pic_order_cnt_type == 0)
    {
        pBitstream->Exp_Golomb_ue(pSPS->log2_max_pic_order_cnt_lsb_minus4);
    }    
    pBitstream->Exp_Golomb_ue(pSPS->max_num_ref_frames);
    pBitstream->PutBits(1, pSPS->gaps_in_frame_num_value_allowed_flag);
    pBitstream->Exp_Golomb_ue(pSPS->pic_width_in_mbs_minus1);
    pBitstream->Exp_Golomb_ue(pSPS->pic_height_in_map_units_minus1);
    
    // No support for interlace in D3D12 Video Encode
    // frame_mbs_only_flag coded as 1
    pBitstream->PutBits(1, 1); // frame_mbs_only_flag
    pBitstream->PutBits(1, pSPS->direct_8x8_inference_flag);

    // no cropping
    pBitstream->PutBits(1, pSPS->frame_cropping_flag); // frame_cropping_flag
    if (pSPS->frame_cropping_flag)
    {
        pBitstream->Exp_Golomb_ue(pSPS->frame_cropping_rect_left_offset);
        pBitstream->Exp_Golomb_ue(pSPS->frame_cropping_rect_right_offset);
        pBitstream->Exp_Golomb_ue(pSPS->frame_cropping_rect_top_offset);
        pBitstream->Exp_Golomb_ue(pSPS->frame_cropping_rect_bottom_offset);
    }

    // We're not including the VUI so this better be zero.    
    pBitstream->PutBits(1, 0); // vui_paramenters_present_flag

    RBSPTrailing(pBitstream);
    pBitstream->Flush();

    iBytesWritten = pBitstream->GetByteCount() - iBytesWritten;
    return (UINT32)iBytesWritten;
}

UINT32 H264NaluWriter::WritePPSBytes(CBitStream *pBitstream, H264_PPS *pPPS, BOOL bIsHighProfile)
{
    INT32 iBytesWritten = pBitstream->GetByteCount();

    // Standard constraint to be between 0 and 31 inclusive
    assert(pPPS->seq_parameter_set_id >= 0);
    assert(pPPS->seq_parameter_set_id < 32);
    
    // Standard constraint to be between 0 and 255 inclusive
    assert(pPPS->pic_parameter_set_id >=0 );
    assert(pPPS->pic_parameter_set_id < 256);
    
    pBitstream->Exp_Golomb_ue(pPPS->pic_parameter_set_id);
    pBitstream->Exp_Golomb_ue(pPPS->seq_parameter_set_id);
    pBitstream->PutBits(1, pPPS->entropy_coding_mode_flag);
    pBitstream->PutBits(1, pPPS->pic_order_present_flag); // bottom_field_pic_order_in_frame_present_flag
    pBitstream->Exp_Golomb_ue(0); // num_slice_groups_minus1


    pBitstream->Exp_Golomb_ue(pPPS->num_ref_idx_l0_active_minus1);
    pBitstream->Exp_Golomb_ue(pPPS->num_ref_idx_l1_active_minus1);
    pBitstream->PutBits(1, 0); // weighted_pred_flag
    pBitstream->PutBits(2, 0); // weighted_bipred_idc
    pBitstream->Exp_Golomb_se(0); // pic_init_qp_minus26
    pBitstream->Exp_Golomb_se(0); // pic_init_qs_minus26
    pBitstream->Exp_Golomb_se(0); // chroma_qp_index_offset
    pBitstream->PutBits(1, 1); // deblocking_filter_control_present_flag
    pBitstream->PutBits(1, pPPS->constrained_intra_pred_flag);
    pBitstream->PutBits(1, 0); // redundant_pic_cnt_present_flag

    if (bIsHighProfile)
    {
        pBitstream->PutBits(1, pPPS->transform_8x8_mode_flag);
        pBitstream->PutBits(1, 0); // pic_scaling_matrix_present_flag
        pBitstream->Exp_Golomb_se(0); // second_chroma_qp_index_offset
    }

    RBSPTrailing(pBitstream);
    pBitstream->Flush();

    iBytesWritten = pBitstream->GetByteCount() - iBytesWritten;
    return (UINT32)iBytesWritten;
}

UINT32 H264NaluWriter::WrapSPSNalu(CBitStream *pNALU, CBitStream *pRBSP)
{
    return WrapRbspIntoNalu(pNALU, pRBSP, NAL_REFIDC_REF, NAL_TYPE_SPS);
}

UINT32 H264NaluWriter::WrapPPSNalu(CBitStream *pNALU, CBitStream *pRBSP)
{
    return WrapRbspIntoNalu(pNALU, pRBSP, NAL_REFIDC_REF, NAL_TYPE_PPS);
}

void H264NaluWriter::WriteNaluEnd(CBitStream *pNALU)
{
    pNALU->Flush();
    pNALU->SetStartCodePrevention(FALSE);
    INT32 iNALUnitLen = pNALU->GetByteCount();

    if (FALSE == pNALU->m_bBufferOverflow &&
            0x00 == pNALU->GetBitstreamBuffer()[iNALUnitLen - 1])
    {
        pNALU->PutBits(8, 0x03);
        pNALU->Flush();
    }
}

UINT32 H264NaluWriter::WrapRbspIntoNalu(CBitStream *pNALU,
                              CBitStream *pRBSP,
                              UINT iNaluIdc,
                              UINT iNaluType)
{
    assert(pRBSP->IsByteAligned());

    INT32 iBytesWritten = pNALU->GetByteCount();

    pNALU->SetStartCodePrevention(FALSE);

    // NAL start code
    pNALU->PutBits(24, 0);
    pNALU->PutBits(8, 1);

    // NAL header
    pNALU->PutBits(1, 0);
    pNALU->PutBits(2, iNaluIdc);
    pNALU->PutBits(5, iNaluType);
    pNALU->Flush();

    // NAL body
    pRBSP->Flush();

    if (pRBSP->GetStartCodePreventionStatus())
    {
        // Direct copying.
        pNALU->AppendByteStream(pRBSP);
    }
    else
    {
        // Copy with start code prevention.
        pNALU->SetStartCodePrevention(TRUE);
        INT32 iLength = pRBSP->GetByteCount();
        BYTE *pBuffer = pRBSP->GetBitstreamBuffer();

        for (INT32 i = 0; i < iLength; i++)
        {
            pNALU->PutBits(8, pBuffer[i]);
        }
    }

    assert(pNALU->IsByteAligned());
    WriteNaluEnd(pNALU);

    pNALU->Flush();

    iBytesWritten = pNALU->GetByteCount() - iBytesWritten;
    return (UINT32)iBytesWritten;
}

void H264NaluWriter::SPSToNALUBytes(H264_SPS *pSPS, std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t& writtenBytes)
{
    // Wrap SPS into NALU and copy full NALU into output byte array
    CBitStream rbsp, nalu;
    assert(rbsp.CreateBitStream(MAX_COMPRESSED_SPS));
    assert(nalu.CreateBitStream(2*MAX_COMPRESSED_SPS));

    rbsp.SetStartCodePrevention(TRUE);
    assert(WriteSPSBytes(&rbsp, pSPS) > 0u);
    assert(WrapSPSNalu(&nalu, &rbsp) > 0u);

    // Deep copy nalu into headerBitstream, nalu gets out of scope here and its destructor frees the nalu object buffer memory.
    BYTE* naluBytes = nalu.GetBitstreamBuffer();
    size_t naluByteSize = nalu.GetByteCount();
    
    auto startDstIndex = std::distance( headerBitstream.begin(), placingPositionStart);
    if(headerBitstream.size() < (startDstIndex + naluByteSize))
    {
        headerBitstream.resize(startDstIndex + naluByteSize);
    }

    std::copy_n(
        &naluBytes[0],
        naluByteSize,
        &headerBitstream.data()[startDstIndex]);

    writtenBytes = naluByteSize;
}

void H264NaluWriter::PPSToNALUBytes(H264_PPS *pPPS, std::vector<BYTE> &headerBitstream, BOOL bIsHighProfile, std::vector<BYTE>::iterator placingPositionStart, size_t& writtenBytes)
{
    // Wrap PPS into NALU and copy full NALU into output byte array
    CBitStream rbsp, nalu;
    assert(rbsp.CreateBitStream(MAX_COMPRESSED_PPS));
    assert(nalu.CreateBitStream(2*MAX_COMPRESSED_PPS));

    rbsp.SetStartCodePrevention(TRUE);
    assert(WritePPSBytes(&rbsp, pPPS, bIsHighProfile) > 0u);
    assert(WrapPPSNalu(&nalu, &rbsp) > 0u);

    // Deep copy nalu into headerBitstream, nalu gets out of scope here and its destructor frees the nalu object buffer memory.
    BYTE* naluBytes = nalu.GetBitstreamBuffer();
    size_t naluByteSize = nalu.GetByteCount();

    auto startDstIndex = std::distance( headerBitstream.begin(), placingPositionStart);
    if(headerBitstream.size() < (startDstIndex + naluByteSize))
    {
        headerBitstream.resize(startDstIndex + naluByteSize);
    }

    std::copy_n(
        &naluBytes[0],
        naluByteSize,
        &headerBitstream.data()[startDstIndex]);

    writtenBytes = naluByteSize;
}

void H264NaluWriter::WriteEndOfStreamNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t& writtenBytes)
{
    CBitStream rbsp, nalu;
    assert(rbsp.CreateBitStream(8));
    assert(nalu.CreateBitStream(2*MAX_COMPRESSED_PPS));

    rbsp.SetStartCodePrevention(TRUE);
    assert(WrapRbspIntoNalu(&nalu, &rbsp, NAL_REFIDC_REF, NAL_TYPE_END_OF_STREAM) > 0u);

    // Deep copy nalu into headerBitstream, nalu gets out of scope here and its destructor frees the nalu object buffer memory.
    BYTE* naluBytes = nalu.GetBitstreamBuffer();
    size_t naluByteSize = nalu.GetByteCount();

    auto startDstIndex = std::distance( headerBitstream.begin(), placingPositionStart);
    if(headerBitstream.size() < (startDstIndex + naluByteSize))
    {
        headerBitstream.resize(startDstIndex + naluByteSize);
    }

    std::copy_n(
        &naluBytes[0],
        naluByteSize,
        &headerBitstream.data()[startDstIndex]);

    writtenBytes = naluByteSize;
}

void H264NaluWriter::WriteEndOfSequenceNALU(std::vector<BYTE> &headerBitstream, std::vector<BYTE>::iterator placingPositionStart, size_t& writtenBytes)
{    
    CBitStream rbsp, nalu;
    assert(rbsp.CreateBitStream(8));
    assert(nalu.CreateBitStream(2*MAX_COMPRESSED_PPS));

    rbsp.SetStartCodePrevention(TRUE);
    assert(WrapRbspIntoNalu(&nalu, &rbsp, NAL_REFIDC_REF, NAL_TYPE_END_OF_SEQUENCE) > 0u);

    // Deep copy nalu into headerBitstream, nalu gets out of scope here and its destructor frees the nalu object buffer memory.
    BYTE* naluBytes = nalu.GetBitstreamBuffer();
    size_t naluByteSize = nalu.GetByteCount();

    auto startDstIndex = std::distance( headerBitstream.begin(), placingPositionStart);
    if(headerBitstream.size() < (startDstIndex + naluByteSize))
    {
        headerBitstream.resize(startDstIndex + naluByteSize);
    }

    std::copy_n(
        &naluBytes[0],
        naluByteSize,
        &headerBitstream.data()[startDstIndex]);

    writtenBytes = naluByteSize;
}



