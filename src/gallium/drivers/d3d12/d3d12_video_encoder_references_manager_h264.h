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

#ifndef D3D12_VIDEO_ENCODE_FIFO_REFERENCES_MANAGER_H264_H
#define D3D12_VIDEO_ENCODE_FIFO_REFERENCES_MANAGER_H264_H

#include "d3d12_video_types.h"
#include "d3d12_video_encoder_references_manager.h"
#include "d3d12_video_dpb_storage_manager.h"
#include <queue>

typedef struct D3D12VideoEncoderH264GOPStateDescriptor
{
    UINT GOPLength;
    UINT PPicturePeriod;
    bool gopHasInterFrames;    
    // idr_pic_id
    UINT64 IDRPictureIndex;

     // Current frame type after B-frames reordering
    D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 CurrentFrameType;

    // Tracks codec picture control frame numbers state (display and decode order)
    UINT m_curPictureOrderCountNumber;
    UINT m_curFrameDecodingOrderNumber;
    // Insert IDR frame instead of I frame on GOP cycle restart if set
    bool bForceGOPIDRs;
} D3D12VideoEncoderH264GOPStateDescriptor;

class H264GOPTracker
{

// H264GOPTracker
public:

    H264GOPTracker(
        UINT GOPLength,
        UINT PPicturePeriod,
        UCHAR log2_max_frame_num_minus4,
        UCHAR log2_max_pic_order_cnt_lsb_minus4,
        bool forceGOPIDRs = true);

    ~H264GOPTracker() { }

    const D3D12VideoEncoderH264GOPStateDescriptor* GetCurrentGOPStateDescriptor();

    void ResetGOPTracking( UINT GOPLength, UINT PPicturePeriod, bool forceGOPIDRs, bool resetIDRCount);
    bool IsCurrentFrameUsedAsReference();
    void CalculateNextReordFrame(/*inout*/ bool& producesDPBFlush);
    bool ValidatePicOrderCountType(UCHAR pic_order_cnt_type, UINT MaxDPBCapacity);
    bool IsGOPSupportedByHW(UINT PPicturePeriod, UINT GOPLength, UINT MaxDPBCapacity, UINT MaxL0ReferencesForP, UINT MaxL0ReferencesForB, UINT MaxL1ReferencesForB);

// H264GOPTracker
private:        
    void PeekNextDisplayOrderFrame(D3D12_VIDEO_ENCODER_FRAME_TYPE_H264& nextFrameType, UINT64& nextFrameIndexInGOP);

// Codec configuration
    UCHAR m_log2_max_frame_num_minus4 = 0;
    UCHAR m_log2_max_pic_order_cnt_lsb_minus4 = 0;

// GOP Tracking

    // On finite GOP points to the current position within m_DisplayGOPPattern example in IBP pattern, index = 0 points to I, 1 points to B, 2 points to P...
    // On infinite GOP keeps increasing as AdvanceFrame() is called
    UINT64 m_CurrentIndexInGOP = 0;    

    UINT m_max_frame_num = 0;
    UINT m_max_pic_order_cnt_lsb = 0;

    // Used to reorder B-Frames from display order in m_DisplayGOPPattern to encode order 
    typedef UINT DisplayOrderNumber;       
    std::queue<DisplayOrderNumber> m_PendingBFrames;
    UINT m_MaxPictureOrderCountSeen = 0;

    // User facing gop tracking state
    D3D12VideoEncoderH264GOPStateDescriptor m_GOPStateDescriptor;
};

class D3D12VideoEncoderH264FIFOReferenceManager : public ID3D12AutomaticVideoEncodeReferencePicManager
{

// ID3D12AutomaticVideoEncodeReferencePicManager
public:
    
    bool IsGopSupported(
        UINT GOPLength,
        UINT PPicturePeriod
    );    

    // Method to configure GOP pattern; call on implementation class construction and allows possibility to reconfigure GOP later
    // Returns true if GOP was successfully (re)-initialized
    // Return false if GOP cannot be reconfigured by implementation
    // This method flushes the DPB and resets all the state tracking to the beginning of the new GOP when successful.
    bool ConfigureGOP(
        // Indicates the distance between I-frames in the sequence, or the number of pictures on a GOP. If set to 0, only the first frame will be an I frame (infinite GOP).
        UINT GOPLength,
        // Indicates the period for P-frames to be inserted within the GOP. Note that if GOPLength is set to 0 for infinite GOP, this value must be greater than zero.
        UINT PPicturePeriod,
        // Insert IDR frame instead of I frame on GOP cycle restart if set
        bool forceGOPIDRs = true
    );

    // Advances internal state to next frame in GOP; subsequent calls to GetCurrentFrame* point to the advanced frame status    
    void AdvanceFrame();

    // Returns the resource allocation for a reconstructed picture output for the current frame
    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE GetCurrentFrameReconPicOutputAllocation();

    // Calculates the picture control structure for the current frame
    void GetCurrentFramePictureControlData(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA& codecAllocation);

    bool IsCurrentFrameUsedAsReference();

    D3D12_VIDEO_ENCODE_REFERENCE_FRAMES GetCurrentFrameReferenceFrames();    

    void GetCurrentGOP(UINT& GOPLength, UINT& PPicturePeriod);

// D3D12VideoEncoderH264FIFOReferenceManager
public:

    D3D12VideoEncoderH264FIFOReferenceManager(
        UINT GOPLength,
        UINT PPicturePeriod,
        // Component to handle the DPB ID3D12Resource allocations
        ID3D12VideoDPBStorageManager<ID3D12VideoEncoderHeap>& rDpbStorageManager,
        // Cap based limitations from Encode12 API
        UINT MaxL0ReferencesForP,
        UINT MaxL0ReferencesForB,
        UINT MaxL1ReferencesForB,        
        UINT MaxDPBCapacity,
        // Codec config
        UCHAR pic_order_cnt_type,
        UCHAR log2_max_frame_num_minus4,
        UCHAR log2_max_pic_order_cnt_lsb_minus4,
        bool forceGOPIDRs = true
        );   

    ~D3D12VideoEncoderH264FIFOReferenceManager() { }

// D3D12VideoEncoderH264FIFOReferenceManager
private:

// Class helpers
    void PrepareCurrentFrameReconPicAllocation();
    void ResetGOPTrackingAndDPB(UINT GOPLength, UINT PPicturePeriod, bool forceGOPIDRs, bool resetIDRCount);    
    void UpdateFIFODPB_PushFrontCurReconPicture();
    void PrepareCurrentFrameL0L1Lists();
    void PrintDPB();
    void PrintL0L1();

// Class members

    // Limitations passed from HW from D3D12 API encoder query caps and encoder configs
    UINT m_MaxL0ReferencesForP = 0;
    UINT m_MaxL0ReferencesForB = 0;
    UINT m_MaxL1ReferencesForB = 0;
    UINT m_MaxDPBCapacity = 0;

    H264GOPTracker m_GOPTracker;

    // aliasing the m_GOPTracker current state for convenience to avoid repeatedly calling getters from m_GOPTracker
    const D3D12VideoEncoderH264GOPStateDescriptor* m_pCurrentGOPStateDescriptor;

    // Reference pictures information and codec picture information tracking
    // Gets adjusted as AdvanceFrame() is called
    typedef struct CurrentFrameReferencesData
    {        
        std::vector<UINT> pList0ReferenceFrames;
        std::vector<UINT> pList1ReferenceFrames;
        std::vector<D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pReferenceFramesReconPictureDescriptors;
        D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE ReconstructedPicTexture;
    } CurrentFrameReferencesData;

    // Manages the DPB storage
    ID3D12VideoDPBStorageManager<ID3D12VideoEncoderHeap>& m_rDPBStorageManager;

    CurrentFrameReferencesData m_CurrentFrameReferencesData;
};

#endif