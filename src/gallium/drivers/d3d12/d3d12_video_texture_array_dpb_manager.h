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


#ifndef D3D12_VIDEO_TEXTURE_ARRAY_DPB_MANAGER_H
#define D3D12_VIDEO_TEXTURE_ARRAY_DPB_MANAGER_H

#include "d3d12_video_dpb_storage_manager.h"
#include "d3d12_video_types.h"

class D3D12TexturesArrayDPBManager : public ID3D12VideoDPBStorageManager
{
   // ID3D12VideoDPBStorageManager
 public:
   // Adds a new reference frame at a given position
   void InsertReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition);

   // Assigns a reference frame at a given position
   void AssignReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition);

   // Gets a reference frame at a given position
   D3D12_VIDEO_RECONSTRUCTED_PICTURE GetReferenceFrame(UINT dpbPosition);

   // Removes a new reference frame at a given position and returns operation success
   // pResourceUntracked is an optional output indicating if the removed resource was being tracked by the pool
   bool RemoveReferenceFrame(UINT dpbPosition, bool *pResourceUntracked = nullptr);

   // Returns the resource allocation for a NEW picture
   D3D12_VIDEO_RECONSTRUCTED_PICTURE GetNewTrackedPictureAllocation();

   // Returns true if the trackedItem was allocated (and is being tracked) by this class
   bool IsTrackedAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem);

   // Returns whether it found the tracked resource on this instance pool tracking and was able to free it
   bool UntrackReconstructedPictureAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem);

   // Returns the number of pictures currently stored in the DPB
   UINT GetNumberOfPicsInDPB();

   // Returns all the current reference frames stored
   D3D12_VIDEO_REFERENCE_FRAMES GetCurrentFrameReferenceFrames();

   // Removes all pictures from DPB
   // returns the number of resources marked as reusable
   UINT ClearDecodePictureBuffer();

   // number of resources in the pool that are marked as in use
   UINT GetNumberOfInUseAllocations();

   UINT GetNumberOfTrackedAllocations();

   // D3D12TexturesArrayDPBManager
 public:
   D3D12TexturesArrayDPBManager(
      UINT16 dpbInitialSize,   // Maximum in use resources for a DPB of size x should be x+1 for cases when a P frame is
                               // using the x references in the L0 list and also using an extra resource to output it's
                               // own recon pic.
      ID3D12Device *                              pDevice,
      DXGI_FORMAT                                 encodeSessionFormat,
      D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC encodeSessionResolution,
      D3D12_RESOURCE_FLAGS                        resourceAllocFlags = D3D12_RESOURCE_FLAG_NONE,
      UINT                                        nodeMask           = 0);
   ~D3D12TexturesArrayDPBManager();

   // D3D12TexturesArrayDPBManager
 private:
   void CreateReconstructedPicAllocations(ID3D12Resource **ppResource, UINT16 texArraySize);

   ID3D12Device *                              m_pDevice;
   DXGI_FORMAT                                 m_encodeFormat;
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC m_encodeResolution;
   UINT16                                      m_dpbTextureArraySize = 0;

   // DPB with array of resources backing storage

   typedef struct d3d12_video_dpb
   {
      std::vector<ID3D12Resource *> pResources;
      std::vector<UINT>             pSubresources;
      std::vector<IUnknown *> pHeaps;
   } d3d12_video_dpb;

   d3d12_video_dpb m_D3D12DPB;

   // Flags used when creating the resource pool
   // Usually if reference only is needed for d3d12 video use
   // D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
   // D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
   D3D12_RESOURCE_FLAGS m_resourceAllocFlags;

   // Pool of resources to be aliased by the DPB without giving memory ownership
   // This resources are allocated and released by this implementation
   typedef struct ReusableResource
   {
      ComPtr<ID3D12Resource> pResource;
      UINT                   subresource;
      bool                   isFree;
   } ReusableResource;

   ComPtr<ID3D12Resource>        m_baseTexArrayResource;
   std::vector<ReusableResource> m_ResourcesPool;

   UINT m_nodeMask = 0u;
};

#endif
