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

void
d3d12_video_decoder_refresh_dpb_active_references_h264(struct d3d12_video_decoder *pD3D12Dec)
{
   pD3D12Dec->m_spDPBManager->mark_all_references_as_unused();
   pD3D12Dec->m_spDPBManager->mark_references_in_use(
      d3d12_video_decoder_get_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec)->RefFrameList);
}

void
d3d12_video_decoder_get_frame_info_h264(
   struct d3d12_video_decoder *pD3D12Dec, uint32_t *pWidth, uint32_t *pHeight, uint16_t *pMaxDPB, bool &isInterlaced)
{
   auto pPicParams = d3d12_video_decoder_get_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec);
   // wFrameWidthInMbsMinus1 Width of the frame containing this picture, in units of macroblocks, minus 1. (The width in
   // macroblocks is wFrameWidthInMbsMinus1 plus 1.) wFrameHeightInMbsMinus1 Height of the frame containing this
   // picture, in units of macroblocks, minus 1. (The height in macroblocks is wFrameHeightInMbsMinus1 plus 1.) When the
   // picture is a field, the height of the frame is twice the height of the picture and is an integer multiple of 2 in
   // units of macroblocks.
   *pWidth = (pPicParams->wFrameWidthInMbsMinus1 + 1) * 16;
   *pHeight = (pPicParams->wFrameHeightInMbsMinus1 + 1) / (pPicParams->frame_mbs_only_flag ? 1 : 2);
   *pHeight = (2 - pPicParams->frame_mbs_only_flag) * *pHeight;
   *pHeight = *pHeight * 16;
   *pMaxDPB = pPicParams->num_ref_frames + 1;
   isInterlaced = !pPicParams->frame_mbs_only_flag;
}

///
/// Pushes the current frame as next reference, updates the DXVA H264 structure with the indices of the DPB and
/// transitions the references
///
void
d3d12_video_decoder_prepare_current_frame_references_h264(struct d3d12_video_decoder *pD3D12Dec,
                                                          ID3D12Resource *pTexture2D,
                                                          uint32_t subresourceIndex)
{
   DXVA_PicParams_H264 *pPicParams = d3d12_video_decoder_get_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec);
   pPicParams->CurrPic.Index7Bits = pD3D12Dec->m_spDPBManager->store_future_reference(pPicParams->CurrPic.Index7Bits,
                                                                                      pD3D12Dec->m_spVideoDecoderHeap,
                                                                                      pTexture2D,
                                                                                      subresourceIndex);

   // From H264 DXVA spec:
   // Index7Bits
   //     An index that identifies an uncompressed surface for the CurrPic or RefFrameList member of the picture
   //     parameters structure(section 4.0) or the RefPicList member of the slice control data structure(section 6.0)
   //     When Index7Bits is used in the CurrPic and RefFrameList members of the picture parameters structure, the value
   //     directly specifies the DXVA index of an uncompressed surface. When Index7Bits is used in the RefPicList member
   //     of the slice control data structure, the value identifies the surface indirectly, as an index into the
   //     RefFrameList array of the associated picture parameters structure.For more information, see section 6.2. In
   //     all cases, when Index7Bits does not contain a valid index, the value is 127.

   std::vector<D3D12_RESOURCE_BARRIER>
      neededStateTransitions;   // Returned by update_entries to perform by the method caller
   pD3D12Dec->m_spDPBManager->update_entries(
      d3d12_video_decoder_get_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec)->RefFrameList,
      neededStateTransitions);

   pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(neededStateTransitions.size(), neededStateTransitions.data());

   // Schedule reverse (back to common) transitions before command list closes for current frame
   for (auto BarrierDesc : neededStateTransitions) {
      std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(BarrierDesc);
   }

   D3D12_LOG_DBG("[d3d12_video_decoder_prepare_current_frame_references_h264] DXVA_PicParams_H264 after index remapping)\n");
   d3d12_video_decoder_log_pic_params_h264(d3d12_video_decoder_get_current_dxva_picparams<DXVA_PicParams_H264>(pD3D12Dec));
}

void
d3d12_video_decoder_prepare_dxva_slices_control_h264(struct d3d12_video_decoder *pD3D12Dec,
                                                     size_t numSlices,
                                                     std::vector<DXVA_Slice_H264_Short> &pOutSliceControlBuffers)
{
   pOutSliceControlBuffers.resize(numSlices);
   size_t processedBitstreamBytes = 0u;
   for (size_t sliceIdx = 0; sliceIdx < numSlices; sliceIdx++) {
      // From DXVA spec: All bits for the slice are located within the corresponding bitstream data buffer.
      pOutSliceControlBuffers[sliceIdx].wBadSliceChopping = 0u;
      bool sliceFound =
         d3d12_video_decoder_get_slice_size_and_offset_h264(sliceIdx,
                                                            numSlices,
                                                            pD3D12Dec->m_stagingDecodeBitstream,
                                                            processedBitstreamBytes,
                                                            pOutSliceControlBuffers[sliceIdx].SliceBytesInBuffer,
                                                            pOutSliceControlBuffers[sliceIdx].BSNALunitDataLocation);
      if (!sliceFound) {
         D3D12_LOG_ERROR("[d3d12_video_decoder_h264] Slice NOT FOUND with index %ld for frame with fenceValue: %d\n",
                         sliceIdx,
                         pD3D12Dec->m_fenceValue);
      }

      D3D12_LOG_INFO("[d3d12_video_decoder_h264] Detected slice index %ld with size %d and offset %d for frame with "
                     "fenceValue: %d\n",
                     sliceIdx,
                     pOutSliceControlBuffers[sliceIdx].SliceBytesInBuffer,
                     pOutSliceControlBuffers[sliceIdx].BSNALunitDataLocation,
                     pD3D12Dec->m_fenceValue);

      processedBitstreamBytes += pOutSliceControlBuffers[sliceIdx].SliceBytesInBuffer;
   }
}

bool
d3d12_video_decoder_get_slice_size_and_offset_h264(size_t sliceIdx,
                                                   size_t numSlices,
                                                   std::vector<uint8_t> &buf,
                                                   unsigned int bufferOffset,
                                                   uint32_t &outSliceSize,
                                                   uint32_t &outSliceOffset)
{
   if (sliceIdx >= numSlices) {
      return false;
   }

   uint numBitsToSearchIntoBuffer =
      buf.size() - bufferOffset;   // Search the rest of the full frame buffer after the offset
   int currentSlicePosition = d3d12_video_decoder_get_next_startcode_offset(buf,
                                                                            bufferOffset,
                                                                            DXVA_H264_START_CODE,
                                                                            DXVA_H264_START_CODE_LEN_BITS,
                                                                            numBitsToSearchIntoBuffer);
   assert(currentSlicePosition >= 0);

   // Save the offset until the next slice in the output param
   outSliceOffset = currentSlicePosition + bufferOffset;

   if (sliceIdx == (numSlices - 1))   // If this is the last slice on the bitstream
   {
      // Save the offset until the next slice in the output param
      outSliceOffset = currentSlicePosition + bufferOffset;

      // As there is not another slice after this one, the size will be the difference between the bitsteam total size
      // and the offset current slice

      outSliceSize = buf.size() - outSliceOffset;
   } else   // If it's not the last slice on the bitstream
   {
      // As there's another slice after this one, look for it and calculate the size based on the next one's offset.

      // Skip current start code, to get the slice after this, to calculate its size
      bufferOffset += DXVA_H264_START_CODE_LEN_BITS;

      int nextSlicePosition =
         DXVA_H264_START_CODE_LEN_BITS + d3d12_video_decoder_get_next_startcode_offset(buf,
                                                                                       bufferOffset,
                                                                                       DXVA_H264_START_CODE,
                                                                                       DXVA_H264_START_CODE_LEN_BITS,
                                                                                       numBitsToSearchIntoBuffer);
      assert(nextSlicePosition >= 0);   // if currentSlicePosition was the last slice, this might fail

      outSliceSize = nextSlicePosition - currentSlicePosition;
   }
   return true;
}

static void
d3d12_video_decoder_log_pic_entry_h264(DXVA_PicEntry_H264 &picEntry)
{
   D3D12_LOG_DBG("\t\tIndex7Bits: %d\n"
                 "\t\tAssociatedFlag: %d\n"
                 "\t\tbPicEntry: %d\n",
                 picEntry.Index7Bits,
                 picEntry.AssociatedFlag,
                 picEntry.bPicEntry);
}

void
d3d12_video_decoder_log_pic_params_h264(DXVA_PicParams_H264 *pPicParams)
{
   if (D3D12_LOG_DBG_ON) {
      const UINT16 RefPicListLength = _countof(DXVA_PicParams_H264::RefFrameList);

      // RefPicList = pPicParams->RefFrameList;
      // CurrPic = pPicParams->CurrPic;

      D3D12_LOG_DBG("[D3D12 Video Decoder H264 DXVA PicParams info]\n"
                    "\t[Current Picture Entry]\n");
      d3d12_video_decoder_log_pic_entry_h264(pPicParams->CurrPic);

      D3D12_LOG_DBG("[Decode RefFrameList Pic_Entry list] Entries where bPicEntry == DXVA_H264_INVALID_PICTURE_ENTRY_VALUE are not printed\n");
      for (uint32_t refIdx = 0; refIdx < RefPicListLength; refIdx++) {
         if (DXVA_H264_INVALID_PICTURE_ENTRY_VALUE != pPicParams->RefFrameList[refIdx].bPicEntry) {
            D3D12_LOG_DBG("\t[Reference PicEntry %d]\n", refIdx);
            d3d12_video_decoder_log_pic_entry_h264(pPicParams->RefFrameList[refIdx]);
         }
      }
   }
}

DXVA_PicParams_H264
d3d12_video_decoder_dxva_picparams_from_pipe_picparams_h264(
   uint32_t frameNum,
   pipe_video_profile profile,
   uint32_t decodeWidth,    // pipe_h264_picture_desc doesn't have the size of the frame for H264, but it does for other
                            // codecs.
   uint32_t decodeHeight,   // pipe_h264_picture_desc doesn't have the size of the frame for H264, but it does for other
                            // codecs.
   pipe_h264_picture_desc *pPipeDesc)
{
   DXVA_PicParams_H264 dxvaStructure = {};

   // uint16_t  wFrameWidthInMbsMinus1;
   uint width_in_mb = decodeWidth / D3D12_VIDEO_H264_MB_IN_PIXELS;
   dxvaStructure.wFrameWidthInMbsMinus1 = width_in_mb - 1;
   // uint16_t  wFrameHeightInMbsMinus1;
   uint height_in_mb = static_cast<uint>(std::ceil(decodeHeight / D3D12_VIDEO_H264_MB_IN_PIXELS));
   dxvaStructure.wFrameHeightInMbsMinus1 = height_in_mb - 1;
   // CurrPic.Index7Bits
   dxvaStructure.CurrPic.Index7Bits = pPipeDesc->frame_num;
   // uint8_t   num_ref_frames;
   dxvaStructure.num_ref_frames = pPipeDesc->num_ref_frames;
   // union {
   // struct {
   // uint16_t  field_pic_flag                 : 1;
   dxvaStructure.field_pic_flag = pPipeDesc->field_pic_flag;
   // uint16_t  chroma_format_idc              : 2;
   dxvaStructure.chroma_format_idc = 1;   // This is always 4:2:0 for D3D12 Video. NV12/P010 DXGI formats only.
   // uint16_t  constrained_intra_pred_flag    : 1;
   dxvaStructure.constrained_intra_pred_flag = pPipeDesc->pps->constrained_intra_pred_flag;
   // uint16_t  weighted_pred_flag             : 1;
   dxvaStructure.weighted_pred_flag = pPipeDesc->pps->weighted_pred_flag;
   // uint16_t  weighted_bipred_idc            : 2;
   dxvaStructure.weighted_bipred_idc = pPipeDesc->pps->weighted_bipred_idc;
   // uint16_t  frame_mbs_only_flag            : 1;
   dxvaStructure.frame_mbs_only_flag = pPipeDesc->pps->sps->frame_mbs_only_flag;
   // uint16_t  transform_8x8_mode_flag        : 1;
   dxvaStructure.transform_8x8_mode_flag = pPipeDesc->pps->transform_8x8_mode_flag;
   // };
   // uint16_t  wBitFields;
   // };
   // uint8_t  bit_depth_luma_minus8;
   dxvaStructure.bit_depth_luma_minus8 = pPipeDesc->pps->sps->bit_depth_luma_minus8;
   // uint8_t  bit_depth_chroma_minus8;
   dxvaStructure.bit_depth_chroma_minus8 = pPipeDesc->pps->sps->bit_depth_chroma_minus8;
   // uint8_t   chroma_qp_index_offset;   /* also used for QScb */
   dxvaStructure.chroma_qp_index_offset = pPipeDesc->pps->chroma_qp_index_offset;
   // uint8_t   second_chroma_qp_index_offset; /* also for QScr */
   dxvaStructure.second_chroma_qp_index_offset = pPipeDesc->pps->second_chroma_qp_index_offset;

   /* remainder for parsing */
   // uint8_t   pic_init_qp_minus26;
   dxvaStructure.pic_init_qp_minus26 = pPipeDesc->pps->pic_init_qp_minus26;
   // uint8_t  num_ref_idx_l0_active_minus1;
   dxvaStructure.num_ref_idx_l0_active_minus1 = pPipeDesc->num_ref_idx_l0_active_minus1;
   // uint8_t  num_ref_idx_l1_active_minus1;
   dxvaStructure.num_ref_idx_l1_active_minus1 = pPipeDesc->num_ref_idx_l1_active_minus1;

   // uint16_t frame_num;
   dxvaStructure.frame_num = pPipeDesc->frame_num;

   // uint8_t  log2_max_frame_num_minus4;
   dxvaStructure.log2_max_frame_num_minus4 = pPipeDesc->pps->sps->log2_max_frame_num_minus4;
   // uint8_t  pic_order_cnt_type;
   dxvaStructure.pic_order_cnt_type = pPipeDesc->pps->sps->pic_order_cnt_type;
   // uint8_t  log2_max_pic_order_cnt_lsb_minus4;
   dxvaStructure.log2_max_pic_order_cnt_lsb_minus4 = pPipeDesc->pps->sps->log2_max_pic_order_cnt_lsb_minus4;
   // uint8_t  delta_pic_order_always_zero_flag;
   dxvaStructure.delta_pic_order_always_zero_flag = pPipeDesc->pps->sps->delta_pic_order_always_zero_flag;
   // uint8_t  direct_8x8_inference_flag;
   dxvaStructure.direct_8x8_inference_flag = pPipeDesc->pps->sps->direct_8x8_inference_flag;
   // uint8_t  entropy_coding_mode_flag;
   dxvaStructure.entropy_coding_mode_flag = pPipeDesc->pps->entropy_coding_mode_flag;
   // uint8_t  num_slice_groups_minus1;
   dxvaStructure.num_slice_groups_minus1 = pPipeDesc->pps->num_slice_groups_minus1;

   // uint8_t  slice_group_map_type;
   dxvaStructure.slice_group_map_type = pPipeDesc->pps->slice_group_map_type;
   // uint8_t  deblocking_filter_control_present_flag;
   dxvaStructure.deblocking_filter_control_present_flag = pPipeDesc->pps->deblocking_filter_control_present_flag;
   // uint8_t  redundant_pic_cnt_present_flag;
   dxvaStructure.redundant_pic_cnt_present_flag = pPipeDesc->pps->redundant_pic_cnt_present_flag;
   // uint16_t slice_group_change_rate_minus1;
   dxvaStructure.slice_group_change_rate_minus1 = pPipeDesc->pps->slice_group_change_rate_minus1;
   // int32_t    CurrFieldOrderCnt[2];
   dxvaStructure.CurrFieldOrderCnt[0] = pPipeDesc->field_order_cnt[0];
   dxvaStructure.CurrFieldOrderCnt[1] = pPipeDesc->field_order_cnt[1];

   // uint16_t  RefPicFlag                     : 1;
   dxvaStructure.RefPicFlag = pPipeDesc->is_reference;

   dxvaStructure.UsedForReferenceFlags = 0;            // initialize to zero and set only the appropiate values

   // DXVA_PicEntry_H264  RefFrameList[16]; /* DXVA_PicEntry_H264.AssociatedFlag 1 means LongTermRef */
   // From DXVA spec:
   // RefFrameList
   // Contains a list of 16 uncompressed frame buffer surfaces.  All uncompressed surfaces that correspond to pictures
   // currently marked as "used for reference" must appear in the RefFrameList array. Non-reference surfaces (those
   // which only contain pictures for which the value of RefPicFlag was 0 when the picture was decoded) shall not appear
   // in RefFrameList for a subsequent picture. In addition, surfaces that contain only pictures marked as "unused for
   // reference" shall not appear in RefFrameList for a subsequent picture.

   bool frameUsesAnyRefPicture = false;
   for (uint i = 0; i < 16; i++) {
      // If both top and bottom reference flags are false, this is an invalid entry
      bool validEntry = (pPipeDesc->top_is_reference[i] || pPipeDesc->bottom_is_reference[i]);
      if (!validEntry) {
         // From DXVA spec:
         // Entries that will not be used for decoding the current picture, or any subsequent pictures, are indicated by
         // setting bPicEntry to 0xFF. If bPicEntry is not 0xFF, the entry may be used as a reference surface for
         // decoding the current picture or a subsequent picture (in decoding order).
         dxvaStructure.RefFrameList[i].bPicEntry = DXVA_H264_INVALID_PICTURE_ENTRY_VALUE;
      } else {
         frameUsesAnyRefPicture = true;
         // From DXVA spec:
         // For each entry whose value is not 0xFF, the value of AssociatedFlag is interpreted as follows:
         // 0 - Not a long-term reference frame.
         // 1 - Long-term reference frame. The uncompressed frame buffer contains a reference frame or one or more
         // reference fields marked as "used for long-term reference." If field_pic_flag is 1, the current uncompressed
         // frame surface may appear in the list for the purpose of decoding the second field of a complementary
         // reference field pair.
         dxvaStructure.RefFrameList[i].AssociatedFlag = pPipeDesc->is_long_term[i] ? 1u : 0u;

         // From H264 DXVA spec:
         // Index7Bits
         //     An index that identifies an uncompressed surface for the CurrPic or RefFrameList member of the picture
         //     parameters structure(section 4.0) or the RefPicList member of the slice control data
         //     structure(section 6.0) When Index7Bits is used in the CurrPic and RefFrameList members of the picture
         //     parameters structure, the value directly specifies the DXVA index of an uncompressed surface. When
         //     Index7Bits is used in the RefPicList member of the slice control data structure, the value identifies
         //     the surface indirectly, as an index into the RefFrameList array of the associated picture parameters
         //     structure.For more information, see section 6.2. In all cases, when Index7Bits does not contain a valid
         //     index, the value is 127.

         dxvaStructure.RefFrameList[i].Index7Bits = pPipeDesc->frame_num_list[i];

         // uint16_t FrameNumList[16];
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
         dxvaStructure.FrameNumList[i] =
            (dxvaStructure.RefFrameList[i].bPicEntry != DXVA_H264_INVALID_PICTURE_ENTRY_VALUE) ?
               pPipeDesc->frame_num_list[i] :
               0;

         // int32_t    FieldOrderCntList[16][2];
         // Contains the picture order counts for the reference frames listed in RefFrameList.
         // For each entry i in the RefFrameList array, FieldOrderCntList[i][0] contains the
         // value of TopFieldOrderCnt for entry i, and FieldOrderCntList[i][1] contains the
         // value of BottomFieldOrderCnt for entry i.
         //
         // If an element of the list is not relevent (for example, if the corresponding entry in
         // RefFrameList is empty or is marked as "not used for reference"), the value of
         // TopFieldOrderCnt or BottomFieldOrderCnt in FieldOrderCntList shall be 0.
         // Accelerators can rely on this constraint being fulfilled.

         for (uint i = 0; i < 16; i++) {
            for (uint j = 0; j < 2; j++) {
               if (dxvaStructure.RefFrameList[i].bPicEntry != DXVA_H264_INVALID_PICTURE_ENTRY_VALUE) {
                  dxvaStructure.FieldOrderCntList[i][j] = pPipeDesc->field_order_cnt_list[i][j];
               } else {
                  dxvaStructure.FieldOrderCntList[i][j] = 0;
               }
            }
         }

         // From DXVA spec
         // UsedForReferenceFlags
         // Contains two 1-bit flags for each entry in RefFrameList. For the ith entry in RefFrameList, the two flags
         // are accessed as follows:  Flag1i = (UsedForReferenceFlags >> (2 * i)) & 1  Flag2i = (UsedForReferenceFlags
         // >> (2 * i + 1)) & 1 If Flag1i is 1, the top field of frame number i is marked as "used for reference," as
         // defined by the H.264/AVC specification. If Flag2i is 1, the bottom field of frame number i is marked as
         // "used for reference." (Otherwise, if either flag is 0, that field is not marked as "used for reference.") If
         // an element in the list of frames is not relevent (for example, if the corresponding entry in RefFrameList is
         // empty), the value of both flags for that entry shall be 0. Accelerators may rely on this constraint being
         // fulfilled.

         if (pPipeDesc->top_is_reference[i]) {
            dxvaStructure.UsedForReferenceFlags |= (1 << (2 * i));
         }

         if (pPipeDesc->bottom_is_reference[i]) {
            dxvaStructure.UsedForReferenceFlags |= (1 << (2 * i + 1));
         }
      }
   }

   // From H264 codec spec
   // The variable MbaffFrameFlag is derived as
   // MbaffFrameFlag = ( mb_adaptive_frame_field_flag && !field_pic_flag )
   dxvaStructure.MbaffFrameFlag = (pPipeDesc->pps->sps->mb_adaptive_frame_field_flag && !pPipeDesc->field_pic_flag);

   // From DXVA spec:
   // The value shall be 1 unless the restricted-mode profile in use explicitly supports the value 0.
   dxvaStructure.MbsConsecutiveFlag = 1;

   if (((profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN) || (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH) ||
        (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10) || (profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422) ||
        (profile ==
         PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444)) &&   // profile is Main,High,High 10, High 4:2:2 or High 4:4:4
       (pPipeDesc->pps->sps->level_idc >= 31))       // AND level 3.1 and higher
   {
      dxvaStructure.MinLumaBipredSize8x8Flag = 1;
   } else {
      dxvaStructure.MinLumaBipredSize8x8Flag = 0;
   }

   // frame type (I, P, B, etc) is not included in pipeDesc data, let's try to derive it
   // from the reference list...if frame doesn't use any references, it should be an I frame.
   dxvaStructure.IntraPicFlag = !frameUsesAnyRefPicture;

   // uint8_t  pic_order_present_flag; /* Renamed to bottom_field_pic_order_in_frame_present_flag in newer standard
   // versions. */
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
   // If this flag is 1, the remainder of this structure is present in the buffer and contains valid values. If this
   // flag is 0, the structure might be truncated at this point in the buffer, or the remaining fields may be set to 0
   // and shall be ignored by the accelerator. The remaining members of this structure are needed only for off-host
   // bitstream parsing. If the host decoder parses the bitstream, the decoder can truncate the picture parameters data
   // structure buffer after the ContinuationFlag or set the remaining members to zero. uint8_t  ContinuationFlag;
   dxvaStructure.ContinuationFlag =
      1;   // DXVA destination struct does contain members from the slice section of pipeDesc...

   return dxvaStructure;
}

void
d3d12_video_decoder_dxva_qmatrix_from_pipe_picparams_h264(pipe_h264_picture_desc *pPipeDesc,
                                                          DXVA_Qmatrix_H264 &outMatrixBuffer,
                                                          bool &outSeq_scaling_matrix_present_flag)
{
   outSeq_scaling_matrix_present_flag = pPipeDesc->pps->sps->seq_scaling_matrix_present_flag;
   if (outSeq_scaling_matrix_present_flag) {
      memcpy(&outMatrixBuffer.bScalingLists4x4, pPipeDesc->pps->ScalingList4x4, 6 * 16);
      memcpy(&outMatrixBuffer.bScalingLists8x8, pPipeDesc->pps->ScalingList8x8, 2 * 64);
   } else {
      memset(&outMatrixBuffer.bScalingLists4x4, 0, 6 * 16);
      memset(&outMatrixBuffer.bScalingLists8x8, 0, 2 * 64);
   }
}
