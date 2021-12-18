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

#include "d3d12_video_dec.h"
#include "d3d12_video_dec_h264.h"

void d3d12_decoder_get_frame_info_h264(struct d3d12_video_decoder *pD3D12Dec, UINT *pWidth, UINT *pHeight, UINT16 *pMaxDPB)
{
	auto pPicParams = d3d12_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec);
	// wFrameWidthInMbsMinus1 Width of the frame containing this picture, in units of macroblocks, minus 1. (The width in macroblocks is wFrameWidthInMbsMinus1 plus 1.)
	// wFrameHeightInMbsMinus1 Height of the frame containing this picture, in units of macroblocks, minus 1. 
	// (The height in macroblocks is wFrameHeightInMbsMinus1 plus 1.) When the picture is a field, the height of the frame is 
	// twice the height of the picture and is an integer multiple of 2 in units of macroblocks.
	*pWidth = (pPicParams->wFrameWidthInMbsMinus1 + 1) * 16;
	*pHeight = (pPicParams->wFrameHeightInMbsMinus1 + 1)/ (pPicParams->frame_mbs_only_flag ? 1 : 2);
	*pHeight = (2 - pPicParams->frame_mbs_only_flag) * *pHeight;
	*pHeight = *pHeight * 16;
	*pMaxDPB = pPicParams->num_ref_frames + 1;
}

void d3d12_decoder_release_unused_references_h264(struct d3d12_video_decoder *pD3D12Dec)
{	
	// References residency policy: Mark all references as unused and only mark again as used the ones used by this frame
	pD3D12Dec->m_spDPBManager->ResetInternalTrackingReferenceUsage();
	pD3D12Dec->m_spDPBManager->MarkReferencesInUse(d3d12_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec)->RefFrameList);
}

void d3d12_decoder_prepare_h264_reference_pic_settings(
	struct d3d12_video_decoder *pD3D12Dec,
	ID3D12Resource* pTexture2D,
	UINT subresourceIndex
)
{
	DXVA_PicParams_H264* pPicParams = d3d12_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec);
	pPicParams->CurrPic.Index7Bits = pD3D12Dec->m_spDPBManager->StoreFutureReference(
		pPicParams->CurrPic.Index7Bits, 
		pD3D12Dec->m_spVideoDecoderHeap, 
		pTexture2D, 
		subresourceIndex);

	// From H264 DXVA spec:
	// Index7Bits
	//     An index that identifies an uncompressed surface for the CurrPic or RefFrameList member of the picture parameters structure(section 4.0) or the RefPicList member of the slice control data structure(section 6.0)
	//     When Index7Bits is used in the CurrPic and RefFrameList members of the picture parameters structure, the value directly specifies the DXVA index of an uncompressed surface.
	//     When Index7Bits is used in the RefPicList member of the slice control data structure, the value identifies the surface indirectly, as an index into the RefFrameList array of the associated picture parameters structure.For more information, see section 6.2.
	//     In all cases, when Index7Bits does not contain a valid index, the value is 127.
	
	std::vector<D3D12_RESOURCE_BARRIER> neededStateTransitions; // Returned by UpdateEntries to perform by the method caller
	pD3D12Dec->m_spDPBManager->UpdateEntries(d3d12_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec)->RefFrameList, neededStateTransitions);

	d3d12_record_state_transitions
	(
	pD3D12Dec->m_spDecodeCommandList,
	neededStateTransitions
	);

	// Schedule reverse (back to common) transitions before command list closes for current frame
	for (auto BarrierDesc : neededStateTransitions)
	{
		std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
		pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(BarrierDesc);
	}
}

#define D3D12_VIDEO_H264_MB_IN_PIXELS 16

DXVA_PicParams_H264 d3d12_dec_dxva_picparams_from_pipe_picparams_h264 (
	UINT frameNum,
	pipe_video_profile profile,
	UINT decodeWidth, // pipe_h264_picture_desc doesn't have the size of the frame for H264, but it does for other codecs.
	UINT decodeHeight, // pipe_h264_picture_desc doesn't have the size of the frame for H264, but it does for other codecs.
	pipe_h264_picture_desc* pPipeDesc)
{
	DXVA_PicParams_H264 dxvaStructure = { };

	// USHORT  wFrameWidthInMbsMinus1;
   	uint width_in_mb = decodeWidth / D3D12_VIDEO_H264_MB_IN_PIXELS;
	dxvaStructure.wFrameWidthInMbsMinus1 = width_in_mb - 1;
	// USHORT  wFrameHeightInMbsMinus1;
	uint height_in_mb = align(decodeHeight / D3D12_VIDEO_H264_MB_IN_PIXELS, 2);
	dxvaStructure.wFrameHeightInMbsMinus1 = height_in_mb - 1;	
	// CurrPic.Index7Bits
	dxvaStructure.CurrPic.Index7Bits = pPipeDesc->frame_num;
	// UCHAR   num_ref_frames;
	dxvaStructure.num_ref_frames = pPipeDesc->num_ref_frames;
	// union {
	// struct {
		// USHORT  field_pic_flag                 : 1;
		dxvaStructure.field_pic_flag = pPipeDesc->field_pic_flag;
		// USHORT  chroma_format_idc              : 2;
		dxvaStructure.chroma_format_idc = 1; // This is always 4:2:0 for D3D12 Video. NV12/P010 DXGI formats only.
		// USHORT  constrained_intra_pred_flag    : 1;
		dxvaStructure.constrained_intra_pred_flag = pPipeDesc->pps->constrained_intra_pred_flag;
		// USHORT  weighted_pred_flag             : 1;
		dxvaStructure.weighted_pred_flag = pPipeDesc->pps->weighted_pred_flag;
		// USHORT  weighted_bipred_idc            : 2;
		dxvaStructure.weighted_bipred_idc = pPipeDesc->pps->weighted_bipred_idc;
		// USHORT  frame_mbs_only_flag            : 1;
		dxvaStructure.frame_mbs_only_flag = pPipeDesc->pps->sps->frame_mbs_only_flag;
		// USHORT  transform_8x8_mode_flag        : 1;
		dxvaStructure.transform_8x8_mode_flag = pPipeDesc->pps->transform_8x8_mode_flag;
		// };
	// USHORT  wBitFields;
	// };
	// UCHAR  bit_depth_luma_minus8;
	dxvaStructure.bit_depth_luma_minus8 = pPipeDesc->pps->sps->bit_depth_luma_minus8;
	// UCHAR  bit_depth_chroma_minus8;
	dxvaStructure.bit_depth_chroma_minus8 = pPipeDesc->pps->sps->bit_depth_chroma_minus8;
	// CHAR   chroma_qp_index_offset;   /* also used for QScb */
	dxvaStructure.chroma_qp_index_offset = pPipeDesc->pps->chroma_qp_index_offset;
	// CHAR   second_chroma_qp_index_offset; /* also for QScr */
	dxvaStructure.second_chroma_qp_index_offset = pPipeDesc->pps->second_chroma_qp_index_offset;

	/* remainder for parsing */
	// CHAR   pic_init_qp_minus26;
	dxvaStructure.pic_init_qp_minus26 = pPipeDesc->pps->pic_init_qp_minus26;
	// UCHAR  num_ref_idx_l0_active_minus1;
	dxvaStructure.num_ref_idx_l0_active_minus1 = pPipeDesc->num_ref_idx_l0_active_minus1;
	// UCHAR  num_ref_idx_l1_active_minus1;
	dxvaStructure.num_ref_idx_l1_active_minus1 = pPipeDesc->num_ref_idx_l1_active_minus1;

	// USHORT frame_num;
	dxvaStructure.frame_num = pPipeDesc->frame_num;

	// UCHAR  log2_max_frame_num_minus4;
	dxvaStructure.log2_max_frame_num_minus4 = pPipeDesc->pps->sps->log2_max_frame_num_minus4;
	// UCHAR  pic_order_cnt_type;
	dxvaStructure.pic_order_cnt_type = pPipeDesc->pps->sps->pic_order_cnt_type;
	// UCHAR  log2_max_pic_order_cnt_lsb_minus4;
	dxvaStructure.log2_max_pic_order_cnt_lsb_minus4 = pPipeDesc->pps->sps->log2_max_pic_order_cnt_lsb_minus4;
	// UCHAR  delta_pic_order_always_zero_flag;
	dxvaStructure.delta_pic_order_always_zero_flag = pPipeDesc->pps->sps->delta_pic_order_always_zero_flag;
	// UCHAR  direct_8x8_inference_flag;
	dxvaStructure.direct_8x8_inference_flag = pPipeDesc->pps->sps->direct_8x8_inference_flag;
	// UCHAR  entropy_coding_mode_flag;
	dxvaStructure.entropy_coding_mode_flag = pPipeDesc->pps->entropy_coding_mode_flag;
	// UCHAR  num_slice_groups_minus1;
	dxvaStructure.num_slice_groups_minus1 = pPipeDesc->pps->num_slice_groups_minus1;

	// UCHAR  slice_group_map_type;
	dxvaStructure.slice_group_map_type = pPipeDesc->pps->slice_group_map_type;
	// UCHAR  deblocking_filter_control_present_flag;
	dxvaStructure.deblocking_filter_control_present_flag = pPipeDesc->pps->deblocking_filter_control_present_flag;
	// UCHAR  redundant_pic_cnt_present_flag;
	dxvaStructure.redundant_pic_cnt_present_flag = pPipeDesc->pps->redundant_pic_cnt_present_flag;
	// USHORT slice_group_change_rate_minus1;
	dxvaStructure.slice_group_change_rate_minus1 = pPipeDesc->pps->slice_group_change_rate_minus1;
	// INT    CurrFieldOrderCnt[2];	
	dxvaStructure.CurrFieldOrderCnt[0] = pPipeDesc->field_order_cnt[0];
	dxvaStructure.CurrFieldOrderCnt[1] = pPipeDesc->field_order_cnt[1];

	// USHORT  RefPicFlag                     : 1;
	dxvaStructure.RefPicFlag = pPipeDesc->is_reference;

	// DXVA_PicEntry_H264  RefFrameList[16]; /* DXVA_PicEntry_H264.AssociatedFlag 1 means LongTermRef */
	// From DXVA spec:
		// RefFrameList
		// Contains a list of 16 uncompressed frame buffer surfaces.  All uncompressed surfaces that correspond to pictures currently marked as "used for reference" must appear in the RefFrameList array. Non-reference surfaces (those which only contain pictures for which the value of RefPicFlag was 0 when the picture was decoded) shall not appear in RefFrameList for a subsequent picture. In addition, surfaces that contain only pictures marked as "unused for reference" shall not appear in RefFrameList for a subsequent picture.		

	bool frameUsesAnyRefPicture = false;
	for (uint i = 0; i < 16; i++)
	{
		// If both top and bottom reference flags are false, this is an invalid entry
		bool validEntry = (pPipeDesc->top_is_reference[i] || pPipeDesc->bottom_is_reference[i]);
		if(!validEntry)
		{
			// From DXVA spec: 
				// Entries that will not be used for decoding the current picture, or any subsequent pictures, are indicated by setting bPicEntry to 0xFF. If bPicEntry is not 0xFF, the entry may be used as a reference surface for decoding the current picture or a subsequent picture (in decoding order).
				dxvaStructure.RefFrameList[i].bPicEntry = DXVA_H264_INVALID_PICTURE_ENTRY_VALUE;
		}
		else
		{
			frameUsesAnyRefPicture = true;
			// From DXVA spec: 
				// For each entry whose value is not 0xFF, the value of AssociatedFlag is interpreted as follows:
				// 0 - Not a long-term reference frame.
				// 1 - Long-term reference frame. The uncompressed frame buffer contains a reference frame or one or more reference fields marked as "used for long-term reference."
				// If field_pic_flag is 1, the current uncompressed frame surface may appear in the list for the purpose of decoding the second field of a complementary reference field pair.
			dxvaStructure.RefFrameList[i].AssociatedFlag = pPipeDesc->is_long_term[i] ? 1u : 0u;

			// From H264 DXVA spec:
			// Index7Bits
			//     An index that identifies an uncompressed surface for the CurrPic or RefFrameList member of the picture parameters structure(section 4.0) or the RefPicList member of the slice control data structure(section 6.0)
			//     When Index7Bits is used in the CurrPic and RefFrameList members of the picture parameters structure, the value directly specifies the DXVA index of an uncompressed surface.
			//     When Index7Bits is used in the RefPicList member of the slice control data structure, the value identifies the surface indirectly, as an index into the RefFrameList array of the associated picture parameters structure.For more information, see section 6.2.
			//     In all cases, when Index7Bits does not contain a valid index, the value is 127.

			dxvaStructure.RefFrameList[i].Index7Bits = pPipeDesc->frame_num_list[i]; 

			// USHORT FrameNumList[16];
			// 	 FrameNumList
			// For each entry in RefFrameList, the corresponding entry in FrameNumList
			// contains the value of FrameNum or LongTermFrameIdx, depending on the value of 
			// AssociatedFlag in the RefFrameList entry. (FrameNum is assigned to short-term 
			// reference pictures, and LongTermFrameIdx is assigned to long-term reference 
			// pictures.)
			// If an element in the list of frames is not relevent (for example, if the corresponding 
			// entry in RefFrameList is empty or is marked as "not used for reference"), the value 
			// of the FrameNumList entry shall be 0. Accelerators can rely on this constraint being 
			// fulfilled. 
			dxvaStructure.FrameNumList[i] = (dxvaStructure.RefFrameList[i].bPicEntry != DXVA_H264_INVALID_PICTURE_ENTRY_VALUE) ? pPipeDesc->frame_num_list[i] : 0;

			// INT    FieldOrderCntList[16][2];
			// Contains the picture order counts for the reference frames listed in RefFrameList. 
			// For each entry i in the RefFrameList array, FieldOrderCntList[i][0] contains the 
			// value of TopFieldOrderCnt for entry i, and FieldOrderCntList[i][1] contains the 
			// value of BottomFieldOrderCnt for entry i. 
			// 
			// If an element of the list is not relevent (for example, if the corresponding entry in 
			// RefFrameList is empty or is marked as "not used for reference"), the value of 
			// TopFieldOrderCnt or BottomFieldOrderCnt in FieldOrderCntList shall be 0. 
			// Accelerators can rely on this constraint being fulfilled. 

			for (uint i = 0; i < 16; i++)
			{				
				for (uint j = 0; j < 2; j++)
				{
					if(dxvaStructure.RefFrameList[i].bPicEntry != DXVA_H264_INVALID_PICTURE_ENTRY_VALUE)
					{
						dxvaStructure.FieldOrderCntList[i][j] = pPipeDesc->field_order_cnt_list[i][j];
					}
					else
					{
						dxvaStructure.FieldOrderCntList[i][j] = 0;
					}
				}
			}

			// From DXVA spec
			// UsedForReferenceFlags
				// Contains two 1-bit flags for each entry in RefFrameList. For the ith entry in RefFrameList, the two flags are accessed as follows:
				//  Flag1i = (UsedForReferenceFlags >> (2 * i)) & 1
				//  Flag2i = (UsedForReferenceFlags >> (2 * i + 1)) & 1
				// If Flag1i is 1, the top field of frame number i is marked as "used for reference," as defined by the H.264/AVC specification. If Flag2i is 1, the bottom field of frame number i is marked as "used for reference." (Otherwise, if either flag is 0, that field is not marked as "used for reference.")
				// If an element in the list of frames is not relevent (for example, if the corresponding entry in RefFrameList is empty), the value of both flags for that entry shall be 0. Accelerators may rely on this constraint being fulfilled.

			dxvaStructure.UsedForReferenceFlags = 0; // initialize to zero and set only the appropiate values
			if(pPipeDesc->top_is_reference[i])
			{
				dxvaStructure.UsedForReferenceFlags |= 	(1 << ( 2*i ));
			}

			if(pPipeDesc->bottom_is_reference[i])
			{
				dxvaStructure.UsedForReferenceFlags |= 	(1 << ( 2*i + 1 ));
			}			
		}
	}

	// From H264 codec spec
	// The variable MbaffFrameFlag is derived as
	// MbaffFrameFlag = ( mb_adaptive_frame_field_flag && !field_pic_flag )
	dxvaStructure.MbaffFrameFlag = ( pPipeDesc->pps->sps->mb_adaptive_frame_field_flag && !pPipeDesc->field_pic_flag );

	dxvaStructure.MbsConsecutiveFlag = 1; // Depends upon the current decoder GUID.

	// profile input argument can be the following values for H264...
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422
	// PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444

	// TODO: constrained_set1_flag is not included in pPipeDesc, assume profiles are main+ for now.
    // if (profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH
    //     && profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN
    //     && pPipeDesc->pps->sps->constrained_set1_flag != 1)
    // {
    //     dxvaStructure.MbsConsecutiveFlag = 0;
    // }

    if (
        ((profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN) ||
        (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH) ||
        (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10) ||
        (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422) ||
        (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444)
        ) &&   // profile is Main,High,High 10, High 4:2:2 or High 4:4:4
        (pPipeDesc->pps->sps->level_idc >= 31))// AND level 3.1 and higher
    {
        dxvaStructure.MinLumaBipredSize8x8Flag = 1;
    }
    else
    {
        dxvaStructure.MinLumaBipredSize8x8Flag = 0;
    }

	// frame type (I, P, B, etc) is not included in pipeDesc data, let's try to derive it
	// from the reference list...if frame doesn't use any references, it should be an I frame.
	dxvaStructure.IntraPicFlag = !frameUsesAnyRefPicture;

	// UCHAR  pic_order_present_flag; /* Renamed to bottom_field_pic_order_in_frame_present_flag in newer standard versions. */
	dxvaStructure.pic_order_present_flag = pPipeDesc->pps->bottom_field_pic_order_in_frame_present_flag;

	// Software decoders should be implemented, as soon as feasible, to set the value of 
	// Reserved16Bits to 3. The value 0 was previously assigned for uses prior to July 20, 
	// 2007. The value 1 was previously assigned for uses prior to October 12, 2007. The 
	// value 2 was previously assigned for uses prior to January 15, 2009. Software 
	// decoders shall not set Reserved16Bits to any value other than those listed here.
	dxvaStructure.Reserved16Bits = 3;

	// DXVA spec: Arbitrary number set by the host decoder to use as a tag in the status report 
   // feedback data. The value should not equal 0, and should be different in each call to 
   // Execute. For more information, see section 12.0, Status Report Data Structure.
   dxvaStructure.StatusReportFeedbackNumber = frameNum;

// from DXVA spec
	// ContinuationFlag
	// If this flag is 1, the remainder of this structure is present in the buffer and contains valid values. If this flag is 0, the structure might be truncated at this point in the buffer, or the remaining fields may be set to 0 and shall be ignored by the accelerator.
	// The remaining members of this structure are needed only for off-host bitstream parsing. If the host decoder parses the bitstream, the decoder can truncate the picture parameters data structure buffer after the ContinuationFlag or set the remaining members to zero.
	// UCHAR  ContinuationFlag;
	dxvaStructure.ContinuationFlag = 1;// DXVA destination struct does contain members from the slice section of pipeDesc...

  	return dxvaStructure;
}

void d3d12_dec_dxva_qmatrix_from_pipe_picparams_h264 (pipe_h264_picture_desc* pPipeDesc, DXVA_Qmatrix_H264 & outMatrixBuffer, bool & outSeq_scaling_matrix_present_flag)
{
	outSeq_scaling_matrix_present_flag = pPipeDesc->pps->sps->seq_scaling_matrix_present_flag;
	if(outSeq_scaling_matrix_present_flag)
	{
		memcpy(&outMatrixBuffer.bScalingLists4x4, pPipeDesc->pps->ScalingList4x4, 6 * 16);
		memcpy(&outMatrixBuffer.bScalingLists8x8, pPipeDesc->pps->ScalingList8x8, 2 * 64);
	}
	else
	{
		memset(&outMatrixBuffer.bScalingLists4x4, 0, 6 * 16);
		memset(&outMatrixBuffer.bScalingLists8x8, 0, 2 * 64);
	}	
}