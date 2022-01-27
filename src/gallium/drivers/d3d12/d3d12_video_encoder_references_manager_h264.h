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

class D3D12VideoEncoderReferencesManagerH264 : public ID3D12VideoEncodeReferencePicManager
{
 public:
   void                                      EndFrame();
   void                                      BeginFrame(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curFrameData);
   D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE GetCurrentFrameReconPicOutputAllocation();
   void GetCurrentFramePictureControlData(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA &codecAllocation);
   bool IsCurrentFrameUsedAsReference();
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES GetCurrentFrameReferenceFrames();

   D3D12VideoEncoderReferencesManagerH264(bool                          gopHasInterCodedFrames,
                                          ID3D12VideoDPBStorageManager &rDpbStorageManager,
                                          UINT                          MaxL0ReferencesForP,
                                          UINT                          MaxL0ReferencesForB,
                                          UINT                          MaxL1ReferencesForB,
                                          UINT                          MaxDPBCapacity);

   ~D3D12VideoEncoderReferencesManagerH264()
   { }

 private:
   // Class helpers
   void PrepareCurrentFrameReconPicAllocation();
   void ResetGOPTrackingAndDPB();
   void UpdateFIFODPB_PushFrontCurReconPicture();
   void PrepareCurrentFrameL0L1Lists();
   void PrintDPB();
   void PrintL0L1();

   // Class members

   UINT m_MaxL0ReferencesForP = 0;
   UINT m_MaxL0ReferencesForB = 0;
   UINT m_MaxL1ReferencesForB = 0;
   UINT m_MaxDPBCapacity      = 0;

   typedef struct CurrentFrameReferencesData
   {
      std::vector<UINT>                                                  pList0ReferenceFrames;
      std::vector<UINT>                                                  pList1ReferenceFrames;
      std::vector<D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264> pReferenceFramesReconPictureDescriptors;
      D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE                          ReconstructedPicTexture;
   } CurrentFrameReferencesData;

   ID3D12VideoDPBStorageManager &m_rDPBStorageManager;

   CurrentFrameReferencesData m_CurrentFrameReferencesData;

   bool m_gopHasInterFrames = false;

   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 m_curFrameState = {};
};

#endif
