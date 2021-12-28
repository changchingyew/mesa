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

using namespace std;

D3D12VideoEncoderH264FIFOReferenceManager::D3D12VideoEncoderH264FIFOReferenceManager(
    UINT GOPLength,
    UINT PPicturePeriod,
    ID3D12VideoDPBStorageManager<ID3D12VideoEncoderHeap>& rDpbStorageManager,
    UINT MaxL0ReferencesForP,
    UINT MaxL0ReferencesForB,
    UINT MaxL1ReferencesForB,
    UINT MaxDPBCapacity,
    UCHAR pic_order_cnt_type,
    UCHAR log2_max_frame_num_minus4,
    UCHAR log2_max_pic_order_cnt_lsb_minus4,
    bool forceGOPIDRs    
) : 
    m_MaxL0ReferencesForP(MaxL0ReferencesForP),
    m_MaxL0ReferencesForB(MaxL0ReferencesForB),
    m_MaxL1ReferencesForB(MaxL1ReferencesForB),
    m_MaxDPBCapacity(MaxDPBCapacity),
    m_GOPTracker(GOPLength, PPicturePeriod, log2_max_frame_num_minus4, log2_max_pic_order_cnt_lsb_minus4, forceGOPIDRs),
    m_rDPBStorageManager(rDpbStorageManager),
    m_CurrentFrameReferencesData({ }),
    m_pCurrentGOPStateDescriptor(m_GOPTracker.GetCurrentGOPStateDescriptor())
{
    if(!m_GOPTracker.ValidatePicOrderCountType(pic_order_cnt_type, MaxDPBCapacity))
    {
        D3D12_LOG_ERROR("Invalid or unsupported pic_order_cnt_type\n");
    }

    if(!ConfigureGOP(GOPLength, PPicturePeriod, forceGOPIDRs))
    {
        D3D12_LOG_ERROR("Invalid or unsupported GOP\n");
    }    

    D3D12_LOG_DBG("Completed construction of D3D12VideoEncoderH264FIFOReferenceManager instance, settings are\n");
    D3D12_LOG_DBG("m_MaxL0ReferencesForP: %d\n", m_MaxL0ReferencesForP);
    D3D12_LOG_DBG("m_MaxL0ReferencesForB: %d\n", m_MaxL0ReferencesForB);
    D3D12_LOG_DBG("m_MaxL1ReferencesForB: %d\n", m_MaxL1ReferencesForB);    
    D3D12_LOG_DBG("m_MaxDPBCapacity: %d\n", m_MaxDPBCapacity);  
}

bool D3D12VideoEncoderH264FIFOReferenceManager::IsGopSupported(
    UINT GOPLength,
    UINT PPicturePeriod
)
{
   return m_GOPTracker.IsGOPSupportedByHW(PPicturePeriod, GOPLength, m_MaxDPBCapacity, m_MaxL0ReferencesForP, m_MaxL0ReferencesForB, m_MaxL1ReferencesForB);
}

bool D3D12VideoEncoderH264FIFOReferenceManager::ConfigureGOP(
    UINT GOPLength,
    UINT PPicturePeriod,
    bool forceGOPIDRs
)
{
    if(!IsGopSupported(GOPLength, PPicturePeriod))
    {
        return false;
    }

    ResetGOPTrackingAndDPB(GOPLength, PPicturePeriod, forceGOPIDRs, true);
    return true;
}

void D3D12VideoEncoderH264FIFOReferenceManager::ResetGOPTrackingAndDPB( 
    UINT GOPLength,
    UINT PPicturePeriod,
    bool forceGOPIDRs,
    bool resetIDRCount)
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
    UINT numPicsBeforeClearInDPB = m_rDPBStorageManager.GetNumberOfPicsInDPB();
    UINT cFreedResources = m_rDPBStorageManager.ClearDecodePictureBuffer();
    VERIFY_ARE_EQUAL(numPicsBeforeClearInDPB, cFreedResources);

    // Initialize if needed the reconstructed picture allocation for the first IDR picture in the GOP
    // This needs to be done after initializing the GOP tracking state above since it makes decisions based on the current picture type.
    PrepareCurrentFrameReconPicAllocation();

    // After clearing the DPB, outstanding used allocations should be 1u only for the first allocation for the reconstructed picture of the initial IDR in the GOP
    VERIFY_ARE_EQUAL(m_rDPBStorageManager.GetNumberOfInUseAllocations(), m_pCurrentGOPStateDescriptor->gopHasInterFrames ? 1u : 0u);
    VERIFY_IS_LESS_THAN_OR_EQUAL(m_rDPBStorageManager.GetNumberOfTrackedAllocations(), (m_MaxDPBCapacity + 1)); // pool is not extended beyond maximum expected usage
}

// Calculates the picture control structure for the current frame
void D3D12VideoEncoderH264FIFOReferenceManager::GetCurrentFramePictureControlData(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA& codecAllocation)
{
    // See casts below
    assert(m_CurrentFrameReferencesData.pList0ReferenceFrames.size() < UINT_MAX);
    assert(m_CurrentFrameReferencesData.pList1ReferenceFrames.size() < UINT_MAX);
    assert(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size() < UINT_MAX);

    bool needsL0List = (m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) || (m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME);
    bool needsL1List = (m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME);    

    assert(codecAllocation.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264));
    
    D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 curFrameState =
    {
        //D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_FLAGS
        D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_FLAG_NONE,
        //D3D12_VIDEO_ENCODER_FRAME_TYPE_H264
        m_pCurrentGOPStateDescriptor->CurrentFrameType,    
        // pic_parameter_set_id;
        0,
        // idr_pic_id;
        static_cast<UCHAR>(m_pCurrentGOPStateDescriptor->IDRPictureIndex),
        // UINT PictureOrderCountNumber;
        m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber,
        // UINT FrameDecodingOrderNumber;
        m_pCurrentGOPStateDescriptor->m_curFrameDecodingOrderNumber,
        // UINT TemporalLayerIndex;
        0,
        // UINT List0ReferenceFramesCount;
        needsL0List ? static_cast<UINT>(m_CurrentFrameReferencesData.pList0ReferenceFrames.size()) : 0,
        // [annotation("_Field_size_full_(List0ReferenceFramesCount)")] UINT* pList0ReferenceFrames;
        needsL0List ? m_CurrentFrameReferencesData.pList0ReferenceFrames.data() : nullptr,
        // UINT List1ReferenceFramesCount;
        needsL1List ? static_cast<UINT>(m_CurrentFrameReferencesData.pList1ReferenceFrames.size()) : 0,
        // [annotation("_Field_size_full_(List1ReferenceFramesCount)")] UINT* pList1ReferenceFrames;
        needsL1List ? m_CurrentFrameReferencesData.pList1ReferenceFrames.data() : nullptr,
        // UINT ReferenceFramesReconPictureDescriptorsCount;
        needsL0List ? static_cast<UINT>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size()) : 0,
        // [annotation("_Field_size_full_(ReferenceFramesReconPictureDescriptorsCount)")] D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264* pReferenceFramesReconPictureDescriptors;
        needsL0List ? m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.data() : nullptr,
        // UCHAR adaptive_ref_pic_marking_mode_flag;
        0,
        // UINT RefPicMarkingOperationsCommandsCount;
        0,
        // [annotation("_Field_size_full_(RefPicMarkingOperationsCommandsCount)")] D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_MARKING_OPERATION* pRefPicMarkingOperationsCommands;
        nullptr,
        // List0RefPicModificationsCount
        0,
        // pList0RefPicModifications
        nullptr,
        // List1RefPicModificationsCount
        0,
        // pList1RefPicModifications
        nullptr,
        // UINT QPMapValuesCount;
        0,
        // [annotation("_Field_size_full_(QPMapValuesCount)")] INT8 *pRateControlQPMap;
        nullptr,
    };

    *codecAllocation.pH264PicData = curFrameState;
}

// Returns the resource allocation for a reconstructed picture output for the current frame
D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE D3D12VideoEncoderH264FIFOReferenceManager::GetCurrentFrameReconPicOutputAllocation()
{
    return m_CurrentFrameReferencesData.ReconstructedPicTexture;
}

D3D12_VIDEO_ENCODE_REFERENCE_FRAMES D3D12VideoEncoderH264FIFOReferenceManager::GetCurrentFrameReferenceFrames()
{
    D3D12_VIDEO_ENCODE_REFERENCE_FRAMES retVal =
    {
        0,
        // ppTexture2Ds
        nullptr,
        // pSubresources
        nullptr
    };

    // Return nullptr for fully intra frames (eg IDR)
    // and return references information for inter frames (eg.P/B) and I frame that doesn't flush DPB

    if((m_pCurrentGOPStateDescriptor->CurrentFrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME) && (m_pCurrentGOPStateDescriptor->CurrentFrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME) && m_pCurrentGOPStateDescriptor->gopHasInterFrames)
    {
        auto curRef = m_rDPBStorageManager.GetCurrentFrameReferenceFrames();
        retVal.NumTexture2Ds = curRef.NumTexture2Ds;
        retVal.ppTexture2Ds = curRef.ppTexture2Ds;
        retVal.pSubresources = curRef.pSubresources;
    }

    return retVal;
}

void D3D12VideoEncoderH264FIFOReferenceManager::PrepareCurrentFrameReconPicAllocation()
{
    m_CurrentFrameReferencesData.ReconstructedPicTexture = { nullptr, 0 };

    // If all GOP are intra frames, no point in doing reference pic allocations
    if (m_GOPTracker.IsCurrentFrameUsedAsReference() && m_pCurrentGOPStateDescriptor->gopHasInterFrames)
    {
        auto reconPic = m_rDPBStorageManager.GetNewTrackedPictureAllocation();
        m_CurrentFrameReferencesData.ReconstructedPicTexture.pReconstructedPicture = reconPic.pReconstructedPicture;
        m_CurrentFrameReferencesData.ReconstructedPicTexture.ReconstructedPictureSubresource = reconPic.ReconstructedPictureSubresource;
    }
}

void D3D12VideoEncoderH264FIFOReferenceManager::UpdateFIFODPB_PushFrontCurReconPicture()
{
    // Keep the order of the dpb storage and dpb descriptors in a circular buffer
    // order such that the DPB array consists of a sequence of frames in DECREASING encoding order
    // eg. last frame encoded at first, followed by one to last frames encoded, and at the end
    // the most distant frame encoded (currentFrameEncodeOrderNumber - MaxDPBSize)

    // If current pic was not used as reference, current reconstructed picture resource is empty,
    // No need to to anything in that case.
    // Otherwise extract the reconstructed picture result and add it to the DPB    

    // If GOP are all intra frames, do nothing also.
    if(m_GOPTracker.IsCurrentFrameUsedAsReference() && m_pCurrentGOPStateDescriptor->gopHasInterFrames)
    {
        D3D12_LOG_DBG("MaxDPBCapacity is %d - Number of pics in DPB is %d when trying to put frame with POC %d at front of the DPB\n", 
            m_MaxDPBCapacity,
            m_rDPBStorageManager.GetNumberOfPicsInDPB(),
            m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber
        );        

        // Release least recently used in DPB if we filled the m_MaxDPBCapacity allowed
        if(m_rDPBStorageManager.GetNumberOfPicsInDPB() == m_MaxDPBCapacity)
        {
            bool untrackedRes = false;
            m_rDPBStorageManager.RemoveReferenceFrame(m_rDPBStorageManager.GetNumberOfPicsInDPB() - 1, &untrackedRes); // Remove last entry
            // Verify that resource was untracked since this class is using the pool completely for allocations
            assert(untrackedRes);
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.pop_back(); // Remove last entry
        }

        // Add new dpb to front of DPB
        D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE recAlloc = GetCurrentFrameReconPicOutputAllocation();
        D3D12_VIDEO_RECONSTRUCTED_PICTURE<ID3D12VideoEncoderHeap> refFrameDesc = { };
        refFrameDesc.pReconstructedPicture = recAlloc.pReconstructedPicture;
        refFrameDesc.ReconstructedPictureSubresource = recAlloc.ReconstructedPictureSubresource;
        refFrameDesc.pVideoHeap = nullptr; // D3D12 Video Encode does not need the D3D12VideoEncoderHeap struct for H264 (used for no-key-frame resolution change in VC1, AV1, etc)
        m_rDPBStorageManager.InsertReferenceFrame(refFrameDesc, 0);

        // Prepare D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264 for added DPB member
        D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264 newDPBDescriptor = 
        {
            // UINT ReconstructedPictureResourceIndex;
            0, // the associated reconstructed picture is also being pushed_front in m_rDPBStorageManager
            // BOOL IsLongTermReference;
            false,
            // UINT LongTermPictureIdx;
            0,
            // UINT PictureOrderCountNumber;
            m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber,
            // UINT FrameDecodingOrderNumber;
            m_pCurrentGOPStateDescriptor->m_curFrameDecodingOrderNumber,
            // UINT TemporalLayerIndex;
            0 // NO B-hierarchy in this impl of the picture manager
        };

        // Add DPB entry
        m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.insert(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.begin(), newDPBDescriptor);

        // Update the indices for ReconstructedPictureResourceIndex in pReferenceFramesReconPictureDescriptors
        // to be in identity mapping with m_rDPBStorageManager indices
        // after pushing the elements to the right in the push_front operation
        for(UINT dpbResIdx = 1; dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size() ; dpbResIdx++)
        {
            auto& dpbDesc = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];
            dpbDesc.ReconstructedPictureResourceIndex = dpbResIdx;
        }
    }

    // Number of allocations, disregarding if they are used or not, should not exceed this limit due to reuse policies on DPB items removal.
    VERIFY_IS_LESS_THAN_OR_EQUAL(m_rDPBStorageManager.GetNumberOfTrackedAllocations(), (m_MaxDPBCapacity + 1));
}

void D3D12VideoEncoderH264FIFOReferenceManager::PrepareCurrentFrameL0L1Lists()
{
    // Clear the lists always since this method will be called for every frame advanced
    // If frames are not B or P, lists need to be cleared and empty.
    m_CurrentFrameReferencesData.pList0ReferenceFrames.clear();
    m_CurrentFrameReferencesData.pList0ReferenceFrames.reserve(m_MaxDPBCapacity);
    m_CurrentFrameReferencesData.pList1ReferenceFrames.clear();
    m_CurrentFrameReferencesData.pList1ReferenceFrames.reserve(m_MaxDPBCapacity);

    // If current frame require L0 and maybe L1 lists, build them below
    if(m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME)
    {
        // Only List 0 for P frame
        // Default H264 order - take first m_MaxL0ListForP DPB descriptors sorted by DECREASING FrameDecodingOrderNumber

        // DPB and descriptors are stored in this exact way, so the L0 list of incremental indices into the
        // m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors

        for(UINT refIdx = 0 ; refIdx < static_cast<UINT>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size()) ; refIdx++)
        {
            m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(refIdx);
        }                

        // Trim L0 list according to driver cap report for P frames
        if(m_CurrentFrameReferencesData.pList0ReferenceFrames.size() > m_MaxL0ReferencesForP)
        {
            m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_MaxL0ReferencesForP);
        }
    }
    else if(m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME)
    {
        // List 0 for B Frames

        // Copy DPB to tmp vector with pairs <dpbIdx, dpbObj> to preserve the initial indices position from pReferenceFramesReconPictureDescriptors when doing sorting operation
        std::vector<pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264>> dpbDescs;
        dpbDescs.resize(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
        for(UINT dpbIdx = 0 ; dpbIdx < dpbDescs.size() ; dpbIdx++ )
        {
            dpbDescs[dpbIdx] = make_pair(dpbIdx, m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbIdx]);
        }

        // Partition the array in two groups -> Where x:dpbList x.POC < curFrame.POC and Where x:dpbList x.POC > curFrame.POC
        UINT curFramePOC = m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber;
        // partSplitIterator has the iterator in the vector to the first element of the second group
        auto partSplitIterator = std::partition(dpbDescs.begin(), dpbDescs.end(), 
            [curFramePOC](pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntry)
            {
                return pairEntry.second.PictureOrderCountNumber > curFramePOC;
            });

        // sort in place the two partitions individually (partial_sort might affect the rest of the array)
        // And copy them over to the final L0 list

            // partial sort (unexpected order for elements outside of declared range)
            // by INCREASING POC order, take DPB Descriptors Where x:list0 x.POC > curFrame.POC
            // for the range of pics with POC earlier than current pic [partSplitIterator..dpbSize)
            std::partial_sort(partSplitIterator, dpbDescs.end(), dpbDescs.end(), 
            [curFramePOC](pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryI, pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryJ)
            {
                return pairEntryI.second.PictureOrderCountNumber < pairEntryJ.second.PictureOrderCountNumber;
            });
            
            // Copy over to final list
            for(auto it = partSplitIterator ; it != dpbDescs.end() ; it++)
            {
                m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(it->first);
            }

            // partial sort (unexpected order for elements outside of declared range)
            // by DECREASING picture order count Where x:list0 x.POC < curFrame.POC 
            // for the range of pics with POC later than current pic [0..partSplitIterator)
            std::partial_sort(dpbDescs.begin(), partSplitIterator, dpbDescs.end(), 
                [curFramePOC](pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryI, pair<UINT, D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pairEntryJ)
                {
                    return pairEntryI.second.PictureOrderCountNumber > pairEntryJ.second.PictureOrderCountNumber;
                });
            
            // Copy over to final list
            UINT L0SecondPartSplitStartIdx = 0;
            for(auto it = dpbDescs.begin() ; it != partSplitIterator ; it++)
            {
                m_CurrentFrameReferencesData.pList0ReferenceFrames.push_back(it->first);
                L0SecondPartSplitStartIdx++;
            }

            D3D12_LOG_DBG("[Generating L0] DPB partition position for pics in DPB with POC < cur picture POC(%d) is DPB table index: %d\n", curFramePOC, L0SecondPartSplitStartIdx);
            D3D12_LOG_DBG("L0 is built as an concat of the dpb subregions [0..%d) and [%d..%ld) plus the required ordering inside each subinterval\n", L0SecondPartSplitStartIdx, L0SecondPartSplitStartIdx, dpbDescs.size());

    // List 1 for B Frames - reuse DPBDescs Copy

        // Default H264 codec order - take first m_MaxL1ListForB DPB descriptors sorted by INCREASING picture order count (2*displayorder)
        // Where x:list1 x.POC > curFrame.POC 
        // Then in DECREASING POC order, DPB Descriptors
        // Where x:list0 x.POC < curFrame.POC 

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
        if(m_CurrentFrameReferencesData.pList0ReferenceFrames.size() > m_MaxL0ReferencesForB)
        {
            m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_MaxL0ReferencesForB);
        }

        if(m_CurrentFrameReferencesData.pList1ReferenceFrames.size() > m_MaxL0ReferencesForP)
        {
            m_CurrentFrameReferencesData.pList1ReferenceFrames.resize(m_MaxL1ReferencesForB);
        }        
    }    

    PrintL0L1();
}

void D3D12VideoEncoderH264FIFOReferenceManager::PrintL0L1()
{
    if( D3D12_LOG_DBG_ON &&
        (m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME)
        || (m_pCurrentGOPStateDescriptor->CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME)
    )
    {
        std::string list0ContentsString;
        for(UINT idx = 0; idx < m_CurrentFrameReferencesData.pList0ReferenceFrames.size() ; idx++)
        {
            UINT value = m_CurrentFrameReferencesData.pList0ReferenceFrames[idx];
            list0ContentsString += "{ DPBidx: ";
            list0ContentsString += std::to_string(value);
            list0ContentsString += " - POC: ";
            list0ContentsString += std::to_string(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
            list0ContentsString += " - FrameDecodingOrderNumber: ";
            list0ContentsString += std::to_string(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
            list0ContentsString += "}\n";
        }

        D3D12_LOG_DBG("L0 list for frame with POC %d - frame_num (%d) is: \n %s \n", 
            m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber,
            m_pCurrentGOPStateDescriptor->m_curFrameDecodingOrderNumber,
            list0ContentsString.c_str()
        );

        std::string list1ContentsString;
        for(UINT idx = 0; idx < m_CurrentFrameReferencesData.pList1ReferenceFrames.size() ; idx++)
        {
            UINT value = m_CurrentFrameReferencesData.pList1ReferenceFrames[idx];
            list1ContentsString += "{ DPBidx: ";
            list1ContentsString += std::to_string(value);
            list1ContentsString += " - POC: ";
            list1ContentsString += std::to_string(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
            list1ContentsString += " - FrameDecodingOrderNumber: ";
            list1ContentsString += std::to_string(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
            list1ContentsString += "}\n";
        }

        D3D12_LOG_DBG("L1 list for frame with POC %d - frame_num (%d) is: \n %s \n", 
            m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber,
            m_pCurrentGOPStateDescriptor->m_curFrameDecodingOrderNumber,
            list1ContentsString.c_str()
        );        
    }
}

void D3D12VideoEncoderH264FIFOReferenceManager::PrintDPB()
{
    if(D3D12_LOG_DBG_ON)
    {
        std::string dpbContents;
        for(UINT dpbResIdx = 0; dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size() ; dpbResIdx++)
        {
            auto& dpbDesc = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];
            auto dpbEntry = m_rDPBStorageManager.GetReferenceFrame(dpbDesc.ReconstructedPictureResourceIndex);

            dpbContents += "{ DPBidx: ";
            dpbContents += std::to_string(dpbResIdx);
            dpbContents += " - POC: ";
            dpbContents += std::to_string(dpbDesc.PictureOrderCountNumber);
            dpbContents += " - FrameDecodingOrderNumber: ";
            dpbContents += std::to_string(dpbDesc.FrameDecodingOrderNumber);
            dpbContents += " - DPBStorageIdx: ";
            dpbContents += std::to_string(dpbDesc.ReconstructedPictureResourceIndex);
            dpbContents += " - DPBStorageResourcePtr: ";      
            dpbContents += reinterpret_cast<uintptr_t>(dpbEntry.pReconstructedPicture);
            dpbContents += " - DPBStorageSubresource: ";
            dpbContents += std::to_string(dpbEntry.ReconstructedPictureSubresource);
            dpbContents += "}\n";
        }

        D3D12_LOG_DBG("DPB has %d frames - DPB references for frame with POC %d (frame_num: %d) are: \n %s \n", 
                m_rDPBStorageManager.GetNumberOfPicsInDPB(),
                m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber,
                m_pCurrentGOPStateDescriptor->m_curFrameDecodingOrderNumber,
                dpbContents.c_str()
            );
    }
}

// Advances state to next frame in GOP; subsequent calls to GetCurrentFrame* point to the advanced frame status    
void D3D12VideoEncoderH264FIFOReferenceManager::AdvanceFrame()
{

    // TODO: Update m_GopDescriptor from caller with arguments on this method instead of keeping track internally

    D3D12_LOG_DBG("%d resources IN USE out of a total of %d ALLOCATED resources before advancing frame with POC: %d\n",
        m_rDPBStorageManager.GetNumberOfInUseAllocations(),
        m_rDPBStorageManager.GetNumberOfTrackedAllocations(),
        m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber);

    // Phase 1: Extracting results from previous encode such as the reconstructed picture buffer with the written data
    // from the last executed EncodeFrame results

    // Adds last used (if not null) GetCurrentFrameReconPicOutputAllocation to DPB for next EncodeFrame if necessary
    // updates pReferenceFramesReconPictureDescriptors and updates the dpb storage    
    
    UpdateFIFODPB_PushFrontCurReconPicture();    

    // Phase 2: Advance the GOP tracking state
    bool isDPBFlushNeeded = false;
    m_GOPTracker.CalculateNextReordFrame(isDPBFlushNeeded);
    if(isDPBFlushNeeded)
    {
        ResetGOPTrackingAndDPB(
            m_pCurrentGOPStateDescriptor->GOPLength,
            m_pCurrentGOPStateDescriptor->PPicturePeriod,
            m_pCurrentGOPStateDescriptor->bForceGOPIDRs,
            false // do not reset the idr_pic_id IDR count between GOPS
            );
    }

    PrintDPB();

    // Phase 3: Update reference picture control structures (L0/L1 and DPB descriptors lists based on current frame and next frame in GOP) for next frame

    // Get new allocation from DPB storage for reconstructed picture
    // This is only necessary for the frames that come after an IDR
    // since in the initial state already has this initialized
    // and re-initialized by ResetGOPTrackingAndDPB above
    if(!isDPBFlushNeeded)
    {
        // At the moment of calling this method, UpdateFIFODPB_PushFrontCurReconPicture moved the existing allocation to the front of the DPB
        // so it is safe to overwrite m_CurrentFrameReferencesData.ReconstructedPicTexture with a new allocation
        PrepareCurrentFrameReconPicAllocation();
    }

    // Prepare new L0/L1 lists from updated DPB descriptors updated by UpdateFIFODPB_PushFrontCurReconPicture
    PrepareCurrentFrameL0L1Lists();

    D3D12_LOG_DBG("%d resources IN USE out of a total of %d ALLOCATED resources AFTER advancing TO frame with POC: %d\n",
        m_rDPBStorageManager.GetNumberOfInUseAllocations(),
        m_rDPBStorageManager.GetNumberOfTrackedAllocations(),
        m_pCurrentGOPStateDescriptor->m_curPictureOrderCountNumber);
}

bool D3D12VideoEncoderH264FIFOReferenceManager::IsCurrentFrameUsedAsReference()
{
    return m_GOPTracker.IsCurrentFrameUsedAsReference();
}

void D3D12VideoEncoderH264FIFOReferenceManager::GetCurrentGOP(UINT& GOPLength, UINT& PPicturePeriod)
{
    GOPLength = m_pCurrentGOPStateDescriptor->GOPLength;
    PPicturePeriod = m_pCurrentGOPStateDescriptor->PPicturePeriod;
}

H264GOPTracker::H264GOPTracker(
    UINT GOPLength,
    UINT PPicturePeriod,
    UCHAR log2_max_frame_num_minus4,
    UCHAR log2_max_pic_order_cnt_lsb_minus4,
    bool forceGOPIDRs
) :    
    m_log2_max_frame_num_minus4(log2_max_frame_num_minus4),
    m_log2_max_pic_order_cnt_lsb_minus4(log2_max_pic_order_cnt_lsb_minus4),
    m_CurrentIndexInGOP(0),
    m_MaxPictureOrderCountSeen(0),
    m_GOPStateDescriptor({ })
{
    bool gopHasPFrames = (PPicturePeriod > 0) && ((GOPLength == 0) || (PPicturePeriod < GOPLength));
    bool gopHasBFrames = (PPicturePeriod > 1);

    if(
        // GOP is only I frames
        (!gopHasPFrames && !gopHasBFrames) 
        // and no IDR flush is forced after each GOP ends
        && !forceGOPIDRs)
    {
        // This is a IIIIIII... sequence in which all the frames are still being added in the DPB but they will never be used
        // since when advancing the GOP we'll never reach an inter-frame.
        D3D12_LOG_ERROR("Cyclic I GOPS should always force an IDR for this implementation.\n");
    }

    ResetGOPTracking(GOPLength, PPicturePeriod, forceGOPIDRs, true);    

    D3D12_LOG_DBG("Completed construction of H264GOPTracker instance, settings are\n");
    D3D12_LOG_DBG("m_log2_max_frame_num_minus4: %d\n", m_log2_max_frame_num_minus4);
    D3D12_LOG_DBG("m_log2_max_pic_order_cnt_lsb_minus4: %d\n", m_log2_max_pic_order_cnt_lsb_minus4);
    D3D12_LOG_DBG("max_frame_num: %d\n", m_max_frame_num);
    D3D12_LOG_DBG("max_pic_order_cnt_lsb: %d\n", m_max_pic_order_cnt_lsb);

    D3D12_LOG_DBG("GOPStateDescriptor initial status\n");
    D3D12_LOG_DBG("GOPLength: %d\n", m_GOPStateDescriptor.GOPLength);
    D3D12_LOG_DBG("PPicturePeriod: %d\n", m_GOPStateDescriptor.PPicturePeriod);
    D3D12_LOG_DBG("gopHasInterFrames: %d\n", m_GOPStateDescriptor.gopHasInterFrames);    
    D3D12_LOG_DBG("IDRPictureIndex: %ld\n", m_GOPStateDescriptor.IDRPictureIndex);    
    D3D12_LOG_DBG("CurrentFrameType: %d\n", m_GOPStateDescriptor.CurrentFrameType);    
    D3D12_LOG_DBG("m_curPictureOrderCountNumber: %d\n", m_GOPStateDescriptor.m_curPictureOrderCountNumber);    
    D3D12_LOG_DBG("m_curFrameDecodingOrderNumber: %d\n", m_GOPStateDescriptor.m_curFrameDecodingOrderNumber);    
    D3D12_LOG_DBG("bForceGOPIDRs: %d\n", m_GOPStateDescriptor.bForceGOPIDRs);  
}

bool H264GOPTracker::IsGOPSupportedByHW(UINT PPicturePeriod, UINT GOPLength, UINT MaxDPBCapacity, UINT MaxL0ReferencesForP, UINT MaxL0ReferencesForB, UINT MaxL1ReferencesForB)
{
    bool bSupportedGOP = true;
    bool gopHasPFrames = (PPicturePeriod > 0) && ((GOPLength == 0) || (PPicturePeriod < GOPLength));
    bool gopHasBFrames = (PPicturePeriod > 1);    

    if(gopHasPFrames && (MaxL0ReferencesForP == 0))
    {
        D3D12_LOG_ERROR("GOP not supported based on HW capabilities - Reason: no P frames support - GOP Length: %d GOP PPicPeriod: %d\n", GOPLength, PPicturePeriod);
        bSupportedGOP = false;
    }

    if(gopHasBFrames && ((MaxL0ReferencesForB + MaxL1ReferencesForB) == 0))
    {
        D3D12_LOG_ERROR("GOP not supported based on HW capabilities - Reason: no B frames support - GOP Length: %d GOP PPicPeriod: %d\n", GOPLength, PPicturePeriod);
        bSupportedGOP = false;
    }

    if(gopHasPFrames && !gopHasBFrames && (MaxL0ReferencesForP < MaxDPBCapacity))
    {
        D3D12_LOG_ERROR("MaxL0ReferencesForP must be equal or higher than the reported MaxDPBCapacity -- P frames should be able to address all the DPB unique indices at least once\n");
        bSupportedGOP = false;
    }

    if(gopHasPFrames && gopHasBFrames && ((MaxL0ReferencesForB + MaxL1ReferencesForB) < MaxDPBCapacity))
    {
        D3D12_LOG_ERROR("Insufficient L0 and L1 lists size to address all the unique ref pic indices reported by MaxDPBCapacity\n");
        bSupportedGOP = false;
    }
    
    return bSupportedGOP;
}

bool H264GOPTracker::ValidatePicOrderCountType(UCHAR pic_order_cnt_type, UINT MaxDPBCapacity)
{
    bool bIsValid = true;

    if((pic_order_cnt_type != 0) && (pic_order_cnt_type != 2))
    {
        D3D12_LOG_ERROR("D3D12 Encode support only for pic_order_cnt_type = 0 or pic_order_cnt_type = 2\n");
        bIsValid = false;
    }

    if( 
        (m_max_frame_num < MaxDPBCapacity)
        || (m_max_pic_order_cnt_lsb < MaxDPBCapacity)
    )
    {
        D3D12_LOG_ERROR("log2_max_frame_num_minus4 or m_log2_max_pic_order_cnt_lsb_minus4 not compatible with MaxDPBCapacity\n");
        bIsValid = false;
    }

    bool gopHasPFrames = (m_GOPStateDescriptor.PPicturePeriod > 0) && (m_GOPStateDescriptor.PPicturePeriod < m_GOPStateDescriptor.GOPLength);
    bool gopHasBFrames = (m_GOPStateDescriptor.PPicturePeriod > 1);

     // This class implements pic_order_cnt_type = 0 or pic_order_cnt_type = 2
    // 0 should be selected if B and P frames are present
    if(
        // Gop has P and B frames
        (gopHasPFrames && gopHasBFrames) 
        // and the order count type is not 0
        && (pic_order_cnt_type != 0))
    {
        D3D12_LOG_ERROR("Invalid pic_order_cnt_type should be 2 for GOPS with B Frames\n");
        bIsValid = false;
    }
    
    // 2 should be selected if only P frames are present
    if(
        // Only I and P frames GOP
        (gopHasPFrames && !gopHasBFrames)
        && (pic_order_cnt_type != 2)
    )
    {
        D3D12_LOG_ERROR("Invalid pic_order_cnt_type should be 0 for GOPS with only I and P Frames\n");
        bIsValid = false;
    }
    
    // 2 should be selected if only I frames are present
    if(
        // Only I and P frames GOP
        (!gopHasPFrames && !gopHasBFrames)
        && (pic_order_cnt_type != 2)
    )
    {
        D3D12_LOG_ERROR("Invalid pic_order_cnt_type should be 2 for GOPS with only I Frames\n");
        bIsValid = false;
    }

    return bIsValid;
}

void H264GOPTracker::ResetGOPTracking( UINT GOPLength,
    UINT PPicturePeriod,
    bool forceGOPIDRs,
    bool resetIDRCount)
{
    m_GOPStateDescriptor = 
    {
        GOPLength,
        PPicturePeriod,
        (PPicturePeriod > 0),        
        resetIDRCount ? 0 : m_GOPStateDescriptor.IDRPictureIndex,
        D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME,
        0,
        0,
        forceGOPIDRs
    };

    m_CurrentIndexInGOP = 0;
    m_max_frame_num = static_cast<UINT>(std::pow(2, m_log2_max_frame_num_minus4 + 4));
    m_max_pic_order_cnt_lsb = static_cast<UINT>(std::pow(2, m_log2_max_pic_order_cnt_lsb_minus4 + 4));

    m_MaxPictureOrderCountSeen = 0;
    while(!m_PendingBFrames.empty())
    {
        m_PendingBFrames.pop();
    }
}


bool H264GOPTracker::IsCurrentFrameUsedAsReference()
{
    // This class doesn't provide support for hierarchical Bs and TemporalLayerIds
    // so we should only use as references IDR, I, and P frames
    return (
        (m_GOPStateDescriptor.CurrentFrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME)
    );
}

//
// Peeks the next frame type and GOP index without changing the internal tracking status
// and returning the results of the advance on the display order gop in the by ref arguments
//
void H264GOPTracker::PeekNextDisplayOrderFrame(D3D12_VIDEO_ENCODER_FRAME_TYPE_H264& nextFrameType, UINT64& nextFrameIndexInGOP)
{
    nextFrameIndexInGOP = m_CurrentIndexInGOP;

    if(m_GOPStateDescriptor.GOPLength == 0) // infinite gop
    {
        nextFrameIndexInGOP++;

        // Determine if I, P or B
        if(m_GOPStateDescriptor.PPicturePeriod == 0)
        {
            // All I Frames GOP
            nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
        }
        else
        {
            if(nextFrameIndexInGOP % m_GOPStateDescriptor.PPicturePeriod == 0)
            {
                nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME;
            }
            else
            {
                nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
            }
        }
    }
    else // finite cyclic gop
    {
        nextFrameIndexInGOP = (nextFrameIndexInGOP + 1) % m_GOPStateDescriptor.GOPLength;
        
        // Force IDR after each GOP ends and flush DPB
        if(nextFrameIndexInGOP == 0)
        {
            if(m_GOPStateDescriptor.bForceGOPIDRs)
            {
                nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME;                        
            }
            else
            {
                nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
            }            
        }
        else
        {
            // Determine if I, P or B
            if(m_GOPStateDescriptor.PPicturePeriod == 0)
            {
                // All I Frames GOP
                nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
            }
            else
            {
                if(nextFrameIndexInGOP % m_GOPStateDescriptor.PPicturePeriod == 0)
                {
                    nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME;
                }
                else
                {
                    nextFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
                }
            }
        }
    }    
}

void H264GOPTracker::CalculateNextReordFrame(/*inout*/ bool& producesDPBFlush)
{
    // Calculates next frame type taking into account B-Frame reordering
    // If the pending B frames queue is not empty
    //   The next frame to encode is the front of that queue
    //
    // Otherwise (no pending B frames to flush)
    //   Advances the display order gop set in m_GOPStateDescriptor,
    //   incrementing IDRPictureIndex, CurrentIndexInGOP accordingly until a P or I frame is found.
    //   While I or P frames are not found, the B frames in between will be enqueued in the PendingBFrames queue

    D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 nextFrameType = { };
    // Calculate POC for all the frames advanced in display order
    // if B frame, the parameter is enqueued as pending frames with precalculated values
    // if not B frame, the param is calculated to be set as the next frame codec data params

    // Used to calculate decoding order of next reordered frame
    bool isPreviousFrameUsedAsReference = IsCurrentFrameUsedAsReference();

    // Get next reordered frame either from the B frames queue or advance to next I/P on display order GOP
    if(m_PendingBFrames.empty())
    {        
        UINT PictureOrderCountNumber = m_MaxPictureOrderCountSeen;
        // No pending B frames swapped, advance display order GOP
        do
        {
            // Calculates next frame in display order GOP
            // and advances m_CurrentIndexInGOP by passing it as reference
            // for the peek result
            PeekNextDisplayOrderFrame(nextFrameType, m_CurrentIndexInGOP);

            // Calculate display and encode order for the P/I frame we're at now.
            PictureOrderCountNumber += 2; // 2 because we're not using field mode (no interlace)
            // FrameDisplayOrder =  PictureOrderCountNumber / 2

            if(nextFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME)
            {
                m_PendingBFrames.push(PictureOrderCountNumber);
            }
        } while(nextFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME);

        // After the while and enqueueing of pending B frames, nextFrameType should be P frame (or I if no P frames in GOP with B frames)
        // Either way that one should be encoded first
        m_GOPStateDescriptor.CurrentFrameType = nextFrameType;

        // Update picture control display order value to next frame picked.
        m_GOPStateDescriptor.m_curPictureOrderCountNumber = PictureOrderCountNumber;
    }
    else
    {
        // There are pending B frames in the queue, set those as next
        m_GOPStateDescriptor.CurrentFrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
        m_GOPStateDescriptor.m_curPictureOrderCountNumber = m_PendingBFrames.front();
        m_PendingBFrames.pop();
    }    

    // Save the highest POC seen to restore it every time the queue goes empty (eg. the next frame should start from all the reordered ones that were flushed started)
    m_MaxPictureOrderCountSeen = std::max(m_MaxPictureOrderCountSeen, m_GOPStateDescriptor.m_curPictureOrderCountNumber);

    // Set the new frame encode order (based on reordered GOP selection)
    m_GOPStateDescriptor.m_curFrameDecodingOrderNumber = m_GOPStateDescriptor.m_curFrameDecodingOrderNumber + (isPreviousFrameUsedAsReference ? 1 : 0);

    // Additional steps if we moved to an IDR frame.
    if(m_GOPStateDescriptor.CurrentFrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME)
    {
        // If next frame is IDR, inform about DPB flushing
        // and increment the IDR index.
        m_GOPStateDescriptor.IDRPictureIndex++;
        producesDPBFlush = true;
    }
    
    // Wrap up FrameDecodingOrderNumber by m_max_frame_num
    m_GOPStateDescriptor.m_curFrameDecodingOrderNumber = m_GOPStateDescriptor.m_curFrameDecodingOrderNumber % m_max_frame_num;    
    // Wrap up PictureOrderCountNumber by m_max_pic_order_cnt_lsb
    m_GOPStateDescriptor.m_curPictureOrderCountNumber = m_GOPStateDescriptor.m_curPictureOrderCountNumber % m_max_pic_order_cnt_lsb;    
}

const D3D12VideoEncoderH264GOPStateDescriptor* H264GOPTracker::GetCurrentGOPStateDescriptor()
{
    return &m_GOPStateDescriptor;
}