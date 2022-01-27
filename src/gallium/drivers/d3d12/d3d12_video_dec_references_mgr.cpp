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

#include "d3d12_video_dec_references_mgr.h"
#include "d3d12_video_dec_h264.h"
#include "d3d12_video_texture_array_dpb_manager.h"
#include "d3d12_video_array_of_textures_dpb_manager.h"
#include "d3d12_screen.h"

//----------------------------------------------------------------------------------------------------------------------------------
static UINT16
GetInvalidReferenceIndex(d3d12_video_decode_profile_type DecodeProfileType)
{
   assert(DecodeProfileType <= d3d12_video_decode_profile_type_max_valid);

   switch (DecodeProfileType) {
      case d3d12_video_decode_profile_type_h264:
         return DXVA_H264_INVALID_PICTURE_INDEX;
      default:
         return 0;
   };
}

//----------------------------------------------------------------------------------------------------------------------------------
///
/// This should always be a clear (non ref only) texture, to be presented downstream as the decoded texture
/// Please see get_reference_only_output for the current frame recon pic ref only allocation
///
void
d3d12_video_decoder_references_manager::get_current_frame_decode_output_texture(ID3D12Resource **ppOutTexture2D,
                                                                                UINT *           pOutSubresourceIndex)
{
   if (is_reference_only()) {
      // When using clear DPB references (not ReferenceOnly) the decode output allocations come from
      // m_upD3D12TexturesStorageManager as decode output == reconpic decode output Otherwise, when ReferenceOnly is
      // true, both the reference frames in the DPB and the current frame reconpic output must be REFERENCE_ONLY, all
      // the allocations are stored in m_upD3D12TexturesStorageManager but we need a +1 allocation without the
      // REFERENCE_FRAME to use as clear decoded output. In this case d3d12_video_decoder_references_manager allocates
      // and provides m_pClearDecodedOutputTexture Please note that m_pClearDecodedOutputTexture needs to be copied/read
      // by the client before calling end_frame again, as the allocation will be reused for the next frame.

      if (m_pClearDecodedOutputTexture == nullptr) {
         D3D12_HEAP_PROPERTIES Properties =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, m_dpbDescriptor.m_NodeMask, m_dpbDescriptor.m_NodeMask);
         CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_dpbDescriptor.Format,
                                                                      m_dpbDescriptor.Width,
                                                                      m_dpbDescriptor.Height,
                                                                      1,
                                                                      1,
                                                                      1,
                                                                      0,
                                                                      D3D12_RESOURCE_FLAG_NONE);

         VERIFY_SUCCEEDED(
            m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
                                                         D3D12_HEAP_FLAG_NONE,
                                                         &resDesc,
                                                         D3D12_RESOURCE_STATE_COMMON,
                                                         nullptr,
                                                         IID_PPV_ARGS(m_pClearDecodedOutputTexture.GetAddressOf())));
      }

      *ppOutTexture2D       = m_pClearDecodedOutputTexture.Get();
      *pOutSubresourceIndex = 0;
   } else {
      // The DPB Storage only has standard (without the ref only flags) allocations, directly use one of those.
      d3d12_video_reconstructed_picture pFreshAllocation =
         m_upD3D12TexturesStorageManager->get_new_tracked_picture_allocation();
      *ppOutTexture2D       = pFreshAllocation.pReconstructedPicture;
      *pOutSubresourceIndex = pFreshAllocation.ReconstructedPictureSubresource;
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_ void
d3d12_video_decoder_references_manager::get_reference_only_output(
   ID3D12Resource **ppOutputReference,     // out -> new reference slot assigned or nullptr
   UINT *           pOutputSubresource,    // out -> new reference slot assigned or nullptr
   bool &outNeedsTransitionToDecodeWrite   // out -> indicates if output resource argument has to be transitioned to
                                           // D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
)
{
   if (!is_reference_only()) {
      D3D12_LOG_ERROR(
         "[d3d12_video_decoder_references_manager] d3d12_video_decoder_references_manager::get_reference_only_output "
         "expected is_reference_only() to be true.\n");
   }

   // The DPB Storage only has REFERENCE_ONLY allocations, use one of those.
   d3d12_video_reconstructed_picture pFreshAllocation =
      m_upD3D12TexturesStorageManager->get_new_tracked_picture_allocation();
   *ppOutputReference              = pFreshAllocation.pReconstructedPicture;
   *pOutputSubresource             = pFreshAllocation.ReconstructedPictureSubresource;
   outNeedsTransitionToDecodeWrite = true;
}

//----------------------------------------------------------------------------------------------------------------------------------
D3D12_VIDEO_DECODE_REFERENCE_FRAMES
d3d12_video_decoder_references_manager::get_current_reference_frames()
{
   d3d12_video_reference_frames args = m_upD3D12TexturesStorageManager->get_current_reference_frames();


   // Convert generic IUnknown into the actual decoder heap object
   m_ppHeaps.resize(args.NumTexture2Ds, nullptr);
   for (UINT i = 0; i < args.NumTexture2Ds; i++) {
      if (args.ppHeaps[i]) {
         VERIFY_SUCCEEDED(args.ppHeaps[i]->QueryInterface(IID_PPV_ARGS(&m_ppHeaps[i])));
      } else {
         m_ppHeaps[i] = nullptr;
      }
   }

   D3D12_VIDEO_DECODE_REFERENCE_FRAMES retVal = {
      args.NumTexture2Ds,
      args.ppTexture2Ds,
      args.pSubresources,
      m_ppHeaps.data(),
   };

   return retVal;
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
d3d12_video_decoder_references_manager::d3d12_video_decoder_references_manager(
   const struct d3d12_screen *       pD3D12Screen,
   UINT                              NodeMask,
   d3d12_video_decode_profile_type   DecodeProfileType,
   d3d12_video_decode_dpb_descriptor m_dpbDescriptor)
   : m_pD3D12Screen(pD3D12Screen),
     m_NodeMask(NodeMask),
     m_invalidIndex(GetInvalidReferenceIndex(DecodeProfileType)),
     m_dpbDescriptor(m_dpbDescriptor),
     m_formatInfo({ m_dpbDescriptor.Format })
{
   VERIFY_SUCCEEDED(
      m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &m_formatInfo, sizeof(m_formatInfo)));
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC targetFrameResolution = { static_cast<UINT>(m_dpbDescriptor.Width),
                                                                         m_dpbDescriptor.Height };
   D3D12_RESOURCE_FLAGS                        resourceAllocFlags =
      m_dpbDescriptor.fReferenceOnly ?
         (D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) :
         D3D12_RESOURCE_FLAG_NONE;

   if (m_dpbDescriptor.fArrayOfTexture) {
      // If all subresources are 0, the DPB is loaded with an array of individual textures, the D3D Encode API expects
      // pSubresources to be null in this case The D3D Decode API expects it to be non-null even with all zeroes.
      bool setNullSubresourcesOnAllZero = false;
      m_upD3D12TexturesStorageManager =
         std::make_unique<d3d12_array_of_textures_dpb_manager>(m_dpbDescriptor.dpbSize,
                                                               m_pD3D12Screen->dev,
                                                               m_dpbDescriptor.Format,
                                                               targetFrameResolution,
                                                               resourceAllocFlags,
                                                               setNullSubresourcesOnAllZero,
                                                               m_dpbDescriptor.m_NodeMask);
   } else {
      m_upD3D12TexturesStorageManager = std::make_unique<d3d12_texture_array_dpb_manager>(m_dpbDescriptor.dpbSize,
                                                                                          m_pD3D12Screen->dev,
                                                                                          m_dpbDescriptor.Format,
                                                                                          targetFrameResolution,
                                                                                          resourceAllocFlags,
                                                                                          m_dpbDescriptor.m_NodeMask);
   }

   m_referenceDXVAIndices.resize(m_dpbDescriptor.dpbSize);

   d3d12_video_reconstructed_picture reconPicture = { nullptr, 0, nullptr };

   for (UINT dpbIdx = 0; dpbIdx < m_dpbDescriptor.dpbSize; dpbIdx++) {
      m_upD3D12TexturesStorageManager->insert_reference_frame(reconPicture, dpbIdx);
   }

   mark_all_references_as_unused();
   release_unused_references_texture_memory();
}

//----------------------------------------------------------------------------------------------------------------------------------
UINT16
d3d12_video_decoder_references_manager::find_remapped_index(UINT16 originalIndex)
{
   // Check if the index is already mapped.
   for (UINT16 remappedIndex = 0; remappedIndex < m_dpbDescriptor.dpbSize; remappedIndex++) {
      if (m_referenceDXVAIndices[remappedIndex].originalIndex == originalIndex) {
         return remappedIndex;
      }
   }

   return m_invalidIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
UINT16
d3d12_video_decoder_references_manager::update_entry(
   UINT16           index,                // in
   ID3D12Resource *&pOutputReference,     // out -> new reference slot assigned or nullptr
   UINT &           OutputSubresource,    // out -> new reference slot assigned or 0
   bool &outNeedsTransitionToDecodeRead   // out -> indicates if output resource argument has to be transitioned to
                                          // D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
)
{
   UINT16 remappedIndex           = m_invalidIndex;
   outNeedsTransitionToDecodeRead = false;

   if (index != m_invalidIndex) {
      remappedIndex = find_remapped_index(index);

      outNeedsTransitionToDecodeRead = true;
      if (remappedIndex == m_invalidIndex || remappedIndex == m_currentOutputIndex) {
         D3D12_LOG_INFO("[d3d12_video_decoder_references_manager] update_entry - Invalid Reference Index\n");

         remappedIndex                  = m_currentOutputIndex;
         outNeedsTransitionToDecodeRead = false;
      }

      d3d12_video_reconstructed_picture reconPicture =
         m_upD3D12TexturesStorageManager->get_reference_frame(remappedIndex);
      pOutputReference  = outNeedsTransitionToDecodeRead ? reconPicture.pReconstructedPicture : nullptr;
      OutputSubresource = outNeedsTransitionToDecodeRead ? reconPicture.ReconstructedPictureSubresource : 0u;
   }

   return remappedIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_ UINT16
d3d12_video_decoder_references_manager::store_future_reference(UINT16                          index,
                                                               ComPtr<ID3D12VideoDecoderHeap> &decoderHeap,
                                                               ID3D12Resource *                pTexture2D,
                                                               UINT                            subresourceIndex)
{
   // Check if the index was in use.
   UINT16 remappedIndex = find_remapped_index(index);

   if (remappedIndex == m_invalidIndex) {
      // If not already mapped, see if the same index in the remapped space is available.
      if (index < m_dpbDescriptor.dpbSize && m_referenceDXVAIndices[index].originalIndex == m_invalidIndex) {
         remappedIndex = index;
      }
   }

   if (remappedIndex == m_invalidIndex) {
      // The current output index was not used last frame.  Get an unused entry.
      remappedIndex = find_remapped_index(m_invalidIndex);
   }

   if (remappedIndex == m_invalidIndex) {
      D3D12_LOG_ERROR(
         "[d3d12_video_decoder_references_manager] d3d12_video_decoder_references_manager - Decode - No available "
         "reference map entry for output.\n");
   }

   // Set the index as the key in this map entry.
   m_referenceDXVAIndices[remappedIndex].originalIndex = index;
   IUnknown *pUnkHeap                                  = nullptr;
   VERIFY_SUCCEEDED(decoderHeap.Get()->QueryInterface(IID_PPV_ARGS(&pUnkHeap)));
   d3d12_video_reconstructed_picture reconPic = { pTexture2D, subresourceIndex, pUnkHeap };

   m_upD3D12TexturesStorageManager->assign_reference_frame(reconPic, remappedIndex);

   // Store the index to use for error handling when caller specifies and invalid reference index.
   m_currentOutputIndex = remappedIndex;

   return remappedIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
void
d3d12_video_decoder_references_manager::mark_reference_in_use(UINT16 index)
{
   if (index != m_invalidIndex) {
      UINT16 remappedIndex = find_remapped_index(index);
      if (remappedIndex != m_invalidIndex) {
         m_referenceDXVAIndices[remappedIndex].fUsed = true;
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void
d3d12_video_decoder_references_manager::release_unused_references_texture_memory()
{
   for (UINT index = 0; index < m_dpbDescriptor.dpbSize; index++) {
      if (!m_referenceDXVAIndices[index].fUsed) {
         d3d12_video_reconstructed_picture reconPicture = m_upD3D12TexturesStorageManager->get_reference_frame(index);
         if (reconPicture.pReconstructedPicture != nullptr) {
            // Untrack this resource, will mark it as free un the underlying storage buffer pool
            VERIFY_IS_TRUE(m_upD3D12TexturesStorageManager->untrack_reconstructed_picture_allocation(reconPicture));
            d3d12_video_reconstructed_picture nullReconPic = { nullptr, 0, nullptr };

            // Mark the unused refpic as null/empty in the DPB
            m_upD3D12TexturesStorageManager->assign_reference_frame(nullReconPic, index);
         }


         m_referenceDXVAIndices[index].originalIndex = m_invalidIndex;
      }
   }
}

//----------------------------------------------------------------------------------------------------------------------------------
void
d3d12_video_decoder_references_manager::mark_all_references_as_unused()
{
   for (UINT index = 0; index < m_dpbDescriptor.dpbSize; index++) {
      m_referenceDXVAIndices[index].fUsed = false;
   }
}
