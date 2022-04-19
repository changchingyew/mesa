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

#include "d3d12_video_encoder_references_manager_h264.h"
#include <algorithm>
#include "d3d12_screen.h"
#define _Field_size_full_opt_(x)
#include <d3d12video.h>

using namespace std;

d3d12_video_encoder_references_manager_h264::d3d12_video_encoder_references_manager_h264(
   bool                                       gopHasIorPFrames,
   d3d12_video_dpb_storage_manager_interface &rDpbStorageManager,
   uint32_t                                   MaxL0ReferencesForP,
   uint32_t                                   MaxL0ReferencesForB,
   uint32_t                                   MaxL1ReferencesForB,
   uint32_t                                   MaxDPBCapacity)
   : m_MaxL0ReferencesForP(MaxL0ReferencesForP),
     m_MaxL0ReferencesForB(MaxL0ReferencesForB),
     m_MaxL1ReferencesForB(MaxL1ReferencesForB),
     m_MaxDPBCapacity(MaxDPBCapacity),
     m_rDPBStorageManager(rDpbStorageManager),
     m_CurrentFrameReferencesData({}),
     m_gopHasInterFrames(gopHasIorPFrames)
{
   assert((m_MaxDPBCapacity + 1 /*extra for cur frame output recon pic*/) ==
          m_rDPBStorageManager.get_number_of_tracked_allocations());

   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] Completed construction of "
                 "d3d12_video_encoder_references_manager_h264 instance, settings are\n");
   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] m_MaxL0ReferencesForP: %d\n", m_MaxL0ReferencesForP);
   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] m_MaxL0ReferencesForB: %d\n", m_MaxL0ReferencesForB);
   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] m_MaxL1ReferencesForB: %d\n", m_MaxL1ReferencesForB);
   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] m_MaxDPBCapacity: %d\n", m_MaxDPBCapacity);
}

void
d3d12_video_encoder_references_manager_h264::reset_gop_tracking_and_dpb()
{
   // Reset m_CurrentFrameReferencesData tracking
   m_CurrentFrameReferencesData.pList0ReferenceFrames.clear();
   m_CurrentFrameReferencesData.pList0ReferenceFrames.reserve(m_MaxDPBCapacity);
   m_CurrentFrameReferencesData.pList1ReferenceFrames.clear();
   m_CurrentFrameReferencesData.pList1ReferenceFrames.reserve(m_MaxDPBCapacity);
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.clear();
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.reserve(m_MaxDPBCapacity);
   m_CurrentFrameReferencesData.ReconstructedPicTexture = { nullptr, 0 };

   // Reset DPB storage
   uint32_t numPicsBeforeClearInDPB = m_rDPBStorageManager.get_number_of_pics_in_dpb();
   uint32_t cFreedResources         = m_rDPBStorageManager.clear_decode_picture_buffer();
   assert(numPicsBeforeClearInDPB == cFreedResources);

   // Initialize if needed the reconstructed picture allocation for the first IDR picture in the GOP
   // This needs to be done after initializing the GOP tracking state above since it makes decisions based on the
   // current picture type.
   prepare_current_frame_recon_pic_allocation();

   // After clearing the DPB, outstanding used allocations should be 1u only for the first allocation for the
   // reconstructed picture of the initial IDR in the GOP
   assert(m_rDPBStorageManager.get_number_of_in_use_allocations() == m_gopHasInterFrames ? 1u : 0u);
   assert(m_rDPBStorageManager.get_number_of_tracked_allocations() <=
          (m_MaxDPBCapacity + 1));   // pool is not extended beyond maximum expected usage
}

// Calculates the picture control structure for the current frame
void
d3d12_video_encoder_references_manager_h264::get_current_frame_picture_control_data(
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA &codecAllocation)
{
   prepare_current_frame_l0_l1_lists();

   print_dpb();

   // Update reference picture control structures (L0/L1 and DPB descriptors lists based on current frame and next frame
   // in GOP) for next frame

   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] %d resources IN USE out of a total of %d ALLOCATED "
                 "resources at frame with POC: %d\n",
                 m_rDPBStorageManager.get_number_of_in_use_allocations(),
                 m_rDPBStorageManager.get_number_of_tracked_allocations(),
                 m_curFrameState.PictureOrderCountNumber);

   // See casts below
   assert(m_CurrentFrameReferencesData.pList0ReferenceFrames.size() < UINT32_MAX);
   assert(m_CurrentFrameReferencesData.pList1ReferenceFrames.size() < UINT32_MAX);
   assert(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size() < UINT32_MAX);

   bool needsL0List = (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) ||
                      (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME);
   bool needsL1List = (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME);

   assert(codecAllocation.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264));

   m_curFrameState.List0ReferenceFramesCount =
      needsL0List ? static_cast<uint32_t>(m_CurrentFrameReferencesData.pList0ReferenceFrames.size()) : 0;
   m_curFrameState.pList0ReferenceFrames =
      needsL0List ? m_CurrentFrameReferencesData.pList0ReferenceFrames.data() : nullptr,
   m_curFrameState.List1ReferenceFramesCount =
      needsL1List ? static_cast<uint32_t>(m_CurrentFrameReferencesData.pList1ReferenceFrames.size()) : 0,
   m_curFrameState.pList1ReferenceFrames =
      needsL1List ? m_CurrentFrameReferencesData.pList1ReferenceFrames.data() : nullptr,
   m_curFrameState.ReferenceFramesReconPictureDescriptorsCount =
      needsL0List ? static_cast<uint32_t>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size()) :
                    0,
   m_curFrameState.pReferenceFramesReconPictureDescriptors =
      needsL0List ? m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.data() : nullptr,

   *codecAllocation.pH264PicData = m_curFrameState;
}

// Returns the resource allocation for a reconstructed picture output for the current frame
D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE
d3d12_video_encoder_references_manager_h264::get_current_frame_recon_pic_output_allocation()
{
   return m_CurrentFrameReferencesData.ReconstructedPicTexture;
}

D3D12_VIDEO_ENCODE_REFERENCE_FRAMES
d3d12_video_encoder_references_manager_h264::get_current_reference_frames()
{
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES retVal = { 0,
                                                  // ppTexture2Ds
                                                  nullptr,
                                                  // pSubresources
                                                  nullptr };

   // Return nullptr for fully intra frames (eg IDR)
   // and return references information for inter frames (eg.P/B) and I frame that doesn't flush DPB

   if ((m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME) &&
       (m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME) && m_gopHasInterFrames) {
      auto curRef          = m_rDPBStorageManager.get_current_reference_frames();
      retVal.NumTexture2Ds = curRef.NumTexture2Ds;
      retVal.ppTexture2Ds  = curRef.ppTexture2Ds;
      retVal.pSubresources = curRef.pSubresources;
   }

   return retVal;
}

void
d3d12_video_encoder_references_manager_h264::prepare_current_frame_recon_pic_allocation()
{
   m_CurrentFrameReferencesData.ReconstructedPicTexture = { nullptr, 0 };

   // If all GOP are intra frames, no point in doing reference pic allocations
   if (is_current_frame_used_as_reference() && m_gopHasInterFrames) {
      auto reconPic = m_rDPBStorageManager.get_new_tracked_picture_allocation();
      m_CurrentFrameReferencesData.ReconstructedPicTexture.pReconstructedPicture = reconPic.pReconstructedPicture;
      m_CurrentFrameReferencesData.ReconstructedPicTexture.ReconstructedPictureSubresource =
         reconPic.ReconstructedPictureSubresource;
   }
}

void
d3d12_video_encoder_references_manager_h264::update_fifo_dpb_push_front_cur_recon_pic()
{
   // Keep the order of the dpb storage and dpb descriptors in a circular buffer
   // order such that the DPB array consists of a sequence of frames in DECREASING encoding order
   // eg. last frame encoded at first, followed by one to last frames encoded, and at the end
   // the most distant frame encoded (currentFrameEncodeOrderNumber - MaxDPBSize)

   // If current pic was not used as reference, current reconstructed picture resource is empty,
   // No need to to anything in that case.
   // Otherwise extract the reconstructed picture result and add it to the DPB

   // If GOP are all intra frames, do nothing also.
   if (is_current_frame_used_as_reference() && m_gopHasInterFrames) {
      D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] MaxDPBCapacity is %d - Number of pics in DPB is %d "
                    "when trying to put frame with POC %d at front of the DPB\n",
                    m_MaxDPBCapacity,
                    m_rDPBStorageManager.get_number_of_pics_in_dpb(),
                    m_curFrameState.PictureOrderCountNumber);

      // Release least recently used in DPB if we filled the m_MaxDPBCapacity allowed
      if (m_rDPBStorageManager.get_number_of_pics_in_dpb() == m_MaxDPBCapacity) {
         bool untrackedRes = false;
         m_rDPBStorageManager.remove_reference_frame(m_rDPBStorageManager.get_number_of_pics_in_dpb() - 1,
                                                     &untrackedRes);   // Remove last entry
         // Verify that resource was untracked since this class is using the pool completely for allocations
         assert(untrackedRes);
         m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.pop_back();   // Remove last entry
      }

      // Add new dpb to front of DPB
      D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE recAlloc     = get_current_frame_recon_pic_output_allocation();
      d3d12_video_reconstructed_picture         refFrameDesc = {};
      refFrameDesc.pReconstructedPicture                     = recAlloc.pReconstructedPicture;
      refFrameDesc.ReconstructedPictureSubresource           = recAlloc.ReconstructedPictureSubresource;
      refFrameDesc.pVideoHeap = nullptr;   // D3D12 Video Encode does not need the D3D12VideoEncoderHeap struct for H264
                                           // (used for no-key-frame resolution change in VC1, AV1, etc)
      m_rDPBStorageManager.insert_reference_frame(refFrameDesc, 0);

      // Prepare D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264 for added DPB member
      D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264 newDPBDescriptor = {
         // uint32_t ReconstructedPictureResourceIndex;
         0,   // the associated reconstructed picture is also being pushed_front in m_rDPBStorageManager
              // BOOL IsLongTermReference;
         false,
         // uint32_t LongTermPictureIdx;
         0,
         // uint32_t PictureOrderCountNumber;
         m_curFrameState.PictureOrderCountNumber,
         // uint32_t FrameDecodingOrderNumber;
         m_curFrameState.FrameDecodingOrderNumber,
         // uint32_t TemporalLayerIndex;
         0   // NO B-hierarchy in this impl of the picture manager
      };

      // Add DPB entry
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.insert(
         m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.begin(),
         newDPBDescriptor);

      // Update the indices for ReconstructedPictureResourceIndex in pReferenceFramesReconPictureDescriptors
      // to be in identity mapping with m_rDPBStorageManager indices
      // after pushing the elements to the right in the push_front operation
      for (uint32_t dpbResIdx = 1;
           dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size();
           dpbResIdx++) {
         auto &dpbDesc = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];
         dpbDesc.ReconstructedPictureResourceIndex = dpbResIdx;
      }
   }

   // Number of allocations, disregarding if they are used or not, should not exceed this limit due to reuse policies on
   // DPB items removal.
   assert(m_rDPBStorageManager.get_number_of_tracked_allocations() <= (m_MaxDPBCapacity + 1));
}

void
d3d12_video_encoder_references_manager_h264::prepare_current_frame_l0_l1_lists()
{
   // Clear the lists always since this method will be called for every frame advanced
   // If frames are not B or P, lists need to be cleared and empty.
   m_CurrentFrameReferencesData.pList0ReferenceFrames.clear();
   m_CurrentFrameReferencesData.pList0ReferenceFrames.reserve(m_MaxDPBCapacity);
   m_CurrentFrameReferencesData.pList1ReferenceFrames.clear();
   m_CurrentFrameReferencesData.pList1ReferenceFrames.reserve(m_MaxDPBCapacity);

   // If current frame require L0 and maybe L1 lists, build them below
   if (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) {
      // Only List 0 for P frame
      // Default H264 order - take first m_MaxL0ListForP DPB descriptors sorted by DECREASING FrameDecodingOrderNumber

      // DPB and descriptors are stored in this exact way, so the L0 list of incremental indices into the
      // m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors

      for (uint32_t refIdx = 0;
           refIdx < static_cast<uint32_t>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
           refIdx++) {
         m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(refIdx);
      }

      // Trim L0 list according to driver cap report for P frames
      if (m_CurrentFrameReferencesData.pList0ReferenceFrames.size() > m_MaxL0ReferencesForP) {
         m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_MaxL0ReferencesForP);
      }
   } else if (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME) {
      // List 0 for B Frames

      // Copy DPB to tmp vector with pairs <dpbIdx, dpbObj> to preserve the initial indices position from
      // pReferenceFramesReconPictureDescriptors when doing sorting operation
      std::vector<pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264>> dpbDescs;
      dpbDescs.resize(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
      for (uint32_t dpbIdx = 0; dpbIdx < dpbDescs.size(); dpbIdx++) {
         dpbDescs[dpbIdx] =
            make_pair(dpbIdx, m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbIdx]);
      }

      // Partition the array in two groups -> Where x:dpbList x.POC < curFrame.POC and Where x:dpbList x.POC > curFrame.POC
      uint32_t curFramePOC = m_curFrameState.PictureOrderCountNumber;
      // partSplitIterator has the iterator in the vector to the first element of the second group
      auto partSplitIterator =
         std::partition(dpbDescs.begin(),
                        dpbDescs.end(),
                        [curFramePOC](pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntry) {
                           return pairEntry.second.PictureOrderCountNumber > curFramePOC;
                        });

      // sort in place the two partitions individually (partial_sort might affect the rest of the array)
      // And copy them over to the final L0 list

      // partial sort (unexpected order for elements outside of declared range)
      // by INCREASING POC order, take DPB Descriptors Where x:list0 x.POC > curFrame.POC
      // for the range of pics with POC earlier than current pic [partSplitIterator..dpbSize)
      std::partial_sort(
         partSplitIterator,
         dpbDescs.end(),
         dpbDescs.end(),
         [curFramePOC](pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryI,
                       pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryJ) {
            return pairEntryI.second.PictureOrderCountNumber < pairEntryJ.second.PictureOrderCountNumber;
         });

      // Copy over to final list
      for (auto it = partSplitIterator; it != dpbDescs.end(); it++) {
         m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(it->first);
      }

      // partial sort (unexpected order for elements outside of declared range)
      // by DECREASING picture order count Where x:list0 x.POC < curFrame.POC
      // for the range of pics with POC later than current pic [0..partSplitIterator)
      std::partial_sort(
         dpbDescs.begin(),
         partSplitIterator,
         dpbDescs.end(),
         [curFramePOC](pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryI,
                       pair<uint32_t, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryJ) {
            return pairEntryI.second.PictureOrderCountNumber > pairEntryJ.second.PictureOrderCountNumber;
         });

      // Copy over to final list
      uint32_t L0SecondPartSplitStartIdx = 0;
      for (auto it = dpbDescs.begin(); it != partSplitIterator; it++) {
         m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(it->first);
         L0SecondPartSplitStartIdx++;
      }

      D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] [Generating L0] DPB partition position for pics in DPB "
                    "with POC < cur picture POC(%d) is DPB table index: %d\n",
                    curFramePOC,
                    L0SecondPartSplitStartIdx);
      D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] L0 is built as an concat of the dpb subregions [0..%d) "
                    "and [%d..%ld) plus the required ordering inside each subinterval\n",
                    L0SecondPartSplitStartIdx,
                    L0SecondPartSplitStartIdx,
                    dpbDescs.size());

      // List 1 for B Frames - reuse DPBDescs Copy

      // Default H264 codec order - take first m_MaxL1ListForB DPB descriptors sorted by INCREASING picture order count
      // (2*displayorder) Where x:list1 x.POC > curFrame.POC Then in DECREASING POC order, DPB Descriptors Where x:list0
      // x.POC < curFrame.POC

      // This exactly having the 2 partitions of L0 swapped in the final list for L1.

      // Copy second partition to the beginning of L1

      std::copy(m_CurrentFrameReferencesData.pList0ReferenceFrames.begin() + L0SecondPartSplitStartIdx,
                m_CurrentFrameReferencesData.pList0ReferenceFrames.end(),
                std::back_inserter(m_CurrentFrameReferencesData.pList1ReferenceFrames));

      // Copy first partition to the later part of L1

      std::copy(m_CurrentFrameReferencesData.pList0ReferenceFrames.begin(),
                m_CurrentFrameReferencesData.pList0ReferenceFrames.begin() + L0SecondPartSplitStartIdx,
                std::back_inserter(m_CurrentFrameReferencesData.pList1ReferenceFrames));

      // Trim L0 and L1 lists according to driver cap report for B frames
      if (m_CurrentFrameReferencesData.pList0ReferenceFrames.size() > m_MaxL0ReferencesForB) {
         m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_MaxL0ReferencesForB);
      }

      if (m_CurrentFrameReferencesData.pList1ReferenceFrames.size() > m_MaxL1ReferencesForB) {
         m_CurrentFrameReferencesData.pList1ReferenceFrames.resize(m_MaxL1ReferencesForB);
      }
   }

   print_l0_l1_lists();
}

void
d3d12_video_encoder_references_manager_h264::print_l0_l1_lists()
{
   if (D3D12_LOG_DBG_ON && ((m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) ||
                            (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME))) {
      std::string list0ContentsString;
      for (uint32_t idx = 0; idx < m_CurrentFrameReferencesData.pList0ReferenceFrames.size(); idx++) {
         uint32_t value = m_CurrentFrameReferencesData.pList0ReferenceFrames[idx];
         list0ContentsString += "{ DPBidx: ";
         list0ContentsString += std::to_string(value);
         list0ContentsString += " - POC: ";
         list0ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
         list0ContentsString += " - FrameDecodingOrderNumber: ";
         list0ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
         list0ContentsString += "}\n";
      }

      D3D12_LOG_DBG(
         "[D3D12 Video Encoder Picture Manager H264] L0 list for frame with POC %d - frame_num (%d) is: \n %s \n",
         m_curFrameState.PictureOrderCountNumber,
         m_curFrameState.FrameDecodingOrderNumber,
         list0ContentsString.c_str());

      std::string list1ContentsString;
      for (uint32_t idx = 0; idx < m_CurrentFrameReferencesData.pList1ReferenceFrames.size(); idx++) {
         uint32_t value = m_CurrentFrameReferencesData.pList1ReferenceFrames[idx];
         list1ContentsString += "{ DPBidx: ";
         list1ContentsString += std::to_string(value);
         list1ContentsString += " - POC: ";
         list1ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
         list1ContentsString += " - FrameDecodingOrderNumber: ";
         list1ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
         list1ContentsString += "}\n";
      }

      D3D12_LOG_DBG(
         "[D3D12 Video Encoder Picture Manager H264] L1 list for frame with POC %d - frame_num (%d) is: \n %s \n",
         m_curFrameState.PictureOrderCountNumber,
         m_curFrameState.FrameDecodingOrderNumber,
         list1ContentsString.c_str());
   }
}

void
d3d12_video_encoder_references_manager_h264::print_dpb()
{
   if (D3D12_LOG_DBG_ON) {
      std::string dpbContents;
      for (uint32_t dpbResIdx = 0;
           dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size();
           dpbResIdx++) {
         auto &dpbDesc  = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];
         auto  dpbEntry = m_rDPBStorageManager.get_reference_frame(dpbDesc.ReconstructedPictureResourceIndex);

         dpbContents += "{ DPBidx: ";
         dpbContents += std::to_string(dpbResIdx);
         dpbContents += " - POC: ";
         dpbContents += std::to_string(dpbDesc.PictureOrderCountNumber);
         dpbContents += " - FrameDecodingOrderNumber: ";
         dpbContents += std::to_string(dpbDesc.FrameDecodingOrderNumber);
         dpbContents += " - DPBStorageIdx: ";
         dpbContents += std::to_string(dpbDesc.ReconstructedPictureResourceIndex);
         dpbContents += " - DPBStorageResourcePtr: ";
         char strBuf[256];
         memset(&strBuf, '\0', 256);
         sprintf(strBuf, "%p", dpbEntry.pReconstructedPicture);
         dpbContents += std::string(strBuf);
         dpbContents += " - DPBStorageSubresource: ";
         dpbContents += std::to_string(dpbEntry.ReconstructedPictureSubresource);
         dpbContents += "}\n";
      }

      D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] DPB has %d frames - DPB references for frame with POC "
                    "%d (frame_num: %d) are: \n %s \n",
                    m_rDPBStorageManager.get_number_of_pics_in_dpb(),
                    m_curFrameState.PictureOrderCountNumber,
                    m_curFrameState.FrameDecodingOrderNumber,
                    dpbContents.c_str());
   }
}

// Advances state to next frame in GOP; subsequent calls to GetCurrentFrame* point to the advanced frame status
void
d3d12_video_encoder_references_manager_h264::end_frame()
{
   D3D12_LOG_DBG("[D3D12 Video Encoder Picture Manager H264] %d resources IN USE out of a total of %d ALLOCATED "
                 "resources at end_frame for frame with POC: %d\n",
                 m_rDPBStorageManager.get_number_of_in_use_allocations(),
                 m_rDPBStorageManager.get_number_of_tracked_allocations(),
                 m_curFrameState.PictureOrderCountNumber);

   // Adds last used (if not null) get_current_frame_recon_pic_output_allocation to DPB for next EncodeFrame if
   // necessary updates pReferenceFramesReconPictureDescriptors and updates the dpb storage

   update_fifo_dpb_push_front_cur_recon_pic();
}

bool
d3d12_video_encoder_references_manager_h264::is_current_frame_used_as_reference()
{
   // This class doesn't provide support for hierarchical Bs and TemporalLayerIds
   // so we should only use as references IDR, I, and P frames
   return ((m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME));
}

void
d3d12_video_encoder_references_manager_h264::begin_frame(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curFrameData)
{
   m_curFrameState = *curFrameData.pH264PicData;

   // Advance the GOP tracking state
   bool isDPBFlushNeeded = (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME);
   if (isDPBFlushNeeded) {
      reset_gop_tracking_and_dpb();
   } else {
      // Get new allocation from DPB storage for reconstructed picture
      // This is only necessary for the frames that come after an IDR
      // since in the initial state already has this initialized
      // and re-initialized by reset_gop_tracking_and_dpb above

      prepare_current_frame_recon_pic_allocation();
   }
}
