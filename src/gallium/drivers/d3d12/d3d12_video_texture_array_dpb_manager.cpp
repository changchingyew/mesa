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

#include "d3d12_video_texture_array_dpb_manager.h"

#ifndef _WIN32
#include <wsl/winadapter.h>
#endif

#define D3D12_IGNORE_SDK_LAYERS
#include <directx/d3d12.h>
#include <d3dx12.h>

///
/// D3D12TexturesArrayDPBManager
///

// Differences with ArrayOfTextures
// Uses a D3D12 Texture Array instead of an std::vector with individual D3D resources as backing storage
// Doesn't support extension (by reallocation and copy) of the pool

void
D3D12TexturesArrayDPBManager::CreateReconstructedPicAllocations(ID3D12Resource **ppResource, UINT16 texArraySize)
{
   if (texArraySize > 0) {
      D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, m_nodeMask, m_nodeMask);
      CD3DX12_RESOURCE_DESC reconstructedPictureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_encodeFormat,
                                                                                            m_encodeResolution.Width,
                                                                                            m_encodeResolution.Height,
                                                                                            texArraySize,
                                                                                            1,
                                                                                            1,
                                                                                            0,
                                                                                            m_resourceAllocFlags);

      VERIFY_SUCCEEDED(m_pDevice->CreateCommittedResource(&Properties,
                                                          D3D12_HEAP_FLAG_NONE,
                                                          &reconstructedPictureResourceDesc,
                                                          D3D12_RESOURCE_STATE_COMMON,
                                                          nullptr,
                                                          IID_PPV_ARGS(ppResource)));
   }
}

D3D12TexturesArrayDPBManager::~D3D12TexturesArrayDPBManager()
{ }

D3D12TexturesArrayDPBManager::D3D12TexturesArrayDPBManager(
   UINT16                                      dpbTextureArraySize,
   ID3D12Device *                              pDevice,
   DXGI_FORMAT                                 encodeSessionFormat,
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC encodeSessionResolution,
   D3D12_RESOURCE_FLAGS                        resourceAllocFlags,
   UINT                                        nodeMask)
   : m_pDevice(pDevice),
     m_encodeFormat(encodeSessionFormat),
     m_encodeResolution(encodeSessionResolution),
     m_dpbTextureArraySize(dpbTextureArraySize),
     m_resourceAllocFlags(resourceAllocFlags),
     m_nodeMask(nodeMask)
{
   // Initialize D3D12 DPB exposed in this class implemented CRUD interface for a DPB
   VERIFY_IS_TRUE(0u == ClearDecodePictureBuffer());

   // Implement a reusable pool of D3D12 Resources as an array of textures
   UINT16 poolFixedSize = m_dpbTextureArraySize;
   m_ResourcesPool.resize(poolFixedSize);

   // Build resource pool with commitedresources with a d3ddevice and the encoding session settings (eg. resolution) and
   // the reference_only flag
   CreateReconstructedPicAllocations(m_baseTexArrayResource.GetAddressOf(), poolFixedSize);

   for (UINT idxSubres = 0; idxSubres < poolFixedSize; idxSubres++) {
      m_ResourcesPool[idxSubres].pResource   = m_baseTexArrayResource;
      m_ResourcesPool[idxSubres].subresource = idxSubres;
      m_ResourcesPool[idxSubres].isFree      = true;
   }
}

UINT
D3D12TexturesArrayDPBManager::ClearDecodePictureBuffer()
{
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());

   UINT untrackCount = 0;
   // Mark resources used in DPB as re-usable in the resources pool
   for (UINT idx = 0; idx < m_D3D12DPB.pResources.size(); idx++) {
      // Don't assert the untracking result here in case the DPB contains resources not adquired using the pool methods
      // in this interface
      untrackCount +=
         UntrackReconstructedPictureAllocation({ m_D3D12DPB.pResources[idx], m_D3D12DPB.pSubresources[idx] }) ? 1 : 0;
   }

   // Clear DPB
   m_D3D12DPB.pResources.clear();
   m_D3D12DPB.pSubresources.clear();
   m_D3D12DPB.pHeaps.clear();
   m_D3D12DPB.pResources.reserve(m_dpbTextureArraySize);
   m_D3D12DPB.pSubresources.reserve(m_dpbTextureArraySize);
   m_D3D12DPB.pHeaps.reserve(m_dpbTextureArraySize);

   return untrackCount;
}

// Assigns a reference frame at a given position
void
D3D12TexturesArrayDPBManager::AssignReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition)
{
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pHeaps.size());

   if (dpbPosition > m_D3D12DPB.pResources.size()) {
      D3D12_LOG_ERROR("[D3D12TexturesArrayDPBManager] AssignReferenceFrame - dpbPosition out of bounds.\n");
   }

   m_D3D12DPB.pResources[dpbPosition]    = pReconPicture.pReconstructedPicture;
   m_D3D12DPB.pSubresources[dpbPosition] = pReconPicture.ReconstructedPictureSubresource;
   m_D3D12DPB.pHeaps[dpbPosition]        = pReconPicture.pVideoHeap;
}

// Adds a new reference frame at a given position
void
D3D12TexturesArrayDPBManager::InsertReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition)
{
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pHeaps.size());

   if (dpbPosition > m_D3D12DPB.pResources.size()) {
      // extend capacity
      m_D3D12DPB.pResources.resize(dpbPosition);
      m_D3D12DPB.pSubresources.resize(dpbPosition);
      m_D3D12DPB.pHeaps.resize(dpbPosition);
   }

   m_D3D12DPB.pResources.insert(m_D3D12DPB.pResources.begin() + dpbPosition, pReconPicture.pReconstructedPicture);
   m_D3D12DPB.pSubresources.insert(m_D3D12DPB.pSubresources.begin() + dpbPosition,
                                   pReconPicture.ReconstructedPictureSubresource);
   m_D3D12DPB.pHeaps.insert(m_D3D12DPB.pHeaps.begin() + dpbPosition, pReconPicture.pVideoHeap);
}

// Gets a reference frame at a given position
D3D12_VIDEO_RECONSTRUCTED_PICTURE
D3D12TexturesArrayDPBManager::GetReferenceFrame(UINT dpbPosition)
{
   if (dpbPosition >= m_D3D12DPB.pResources.size()) {
      D3D12_LOG_ERROR("[D3D12TexturesArrayDPBManager] GetReferenceFrame - dpbPosition out of bounds.\n");
   }

   D3D12_VIDEO_RECONSTRUCTED_PICTURE retVal = { m_D3D12DPB.pResources[dpbPosition],
                                                m_D3D12DPB.pSubresources[dpbPosition],
                                                m_D3D12DPB.pHeaps[dpbPosition] };

   return retVal;
}

// Removes a new reference frame at a given position and returns operation success
bool
D3D12TexturesArrayDPBManager::RemoveReferenceFrame(UINT dpbPosition, bool *pResourceUntracked)
{
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pHeaps.size());

   if (dpbPosition >= m_D3D12DPB.pResources.size()) {
      D3D12_LOG_ERROR("[D3D12TexturesArrayDPBManager] RemoveReferenceFrame - dpbPosition out of bounds.\n");
   }

   // If removed resource came from resource pool, mark it as free
   // to free it for a new usage
   // Don't assert the untracking result here in case the DPB contains resources not adquired using the pool methods in
   // this interface
   bool resUntracked = UntrackReconstructedPictureAllocation(
      { m_D3D12DPB.pResources[dpbPosition], m_D3D12DPB.pSubresources[dpbPosition] });

   if (pResourceUntracked != nullptr) {
      *pResourceUntracked = resUntracked;
   }

   // Remove from DPB tables
   m_D3D12DPB.pResources.erase(m_D3D12DPB.pResources.begin() + dpbPosition);
   m_D3D12DPB.pSubresources.erase(m_D3D12DPB.pSubresources.begin() + dpbPosition);
   m_D3D12DPB.pHeaps.erase(m_D3D12DPB.pHeaps.begin() + dpbPosition);

   return true;
}

// Returns true if the trackedItem was allocated (and is being tracked) by this class
bool
D3D12TexturesArrayDPBManager::IsTrackedAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem)
{
   for (auto &reusableRes : m_ResourcesPool) {
      if ((trackedItem.pReconstructedPicture == reusableRes.pResource.Get()) &&
          (trackedItem.ReconstructedPictureSubresource == reusableRes.subresource) && !reusableRes.isFree) {
         return true;
      }
   }
   return false;
}

// Returns whether it found the tracked resource on this instance pool tracking and was able to free it
bool
D3D12TexturesArrayDPBManager::UntrackReconstructedPictureAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem)
{
   for (auto &reusableRes : m_ResourcesPool) {
      if ((trackedItem.pReconstructedPicture == reusableRes.pResource.Get()) &&
          (trackedItem.ReconstructedPictureSubresource == reusableRes.subresource)) {
         reusableRes.isFree = true;
         return true;
      }
   }
   return false;
}

// Returns a fresh resource for a NEW picture to be written to
// this class implements the dpb allocations as an array of textures
D3D12_VIDEO_RECONSTRUCTED_PICTURE
D3D12TexturesArrayDPBManager::GetNewTrackedPictureAllocation()
{
   D3D12_VIDEO_RECONSTRUCTED_PICTURE freshAllocation = { // pResource
                                                         nullptr,
                                                         // subresource
                                                         0
   };

   // Find first (if any) available resource to (re-)use
   bool bAvailableResourceInPool = false;
   for (auto &reusableRes : m_ResourcesPool) {
      if (reusableRes.isFree) {
         bAvailableResourceInPool                        = true;
         freshAllocation.pReconstructedPicture           = reusableRes.pResource.Get();
         freshAllocation.ReconstructedPictureSubresource = reusableRes.subresource;
         reusableRes.isFree                              = false;
         break;
      }
   }

   if (!bAvailableResourceInPool) {
      D3D12_LOG_ERROR("[D3D12TexturesArrayDPBManager] ID3D12Resource pool is full - Pool capacity (%ld)",
                      m_ResourcesPool.size());
   }

   return freshAllocation;
}

UINT
D3D12TexturesArrayDPBManager::GetNumberOfPicsInDPB()
{
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());
   assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pHeaps.size());
   assert(m_D3D12DPB.pResources.size() < UINT_MAX);
   return static_cast<UINT>(m_D3D12DPB.pResources.size());
}

D3D12_VIDEO_REFERENCE_FRAMES
D3D12TexturesArrayDPBManager::GetCurrentFrameReferenceFrames()
{
   D3D12_VIDEO_REFERENCE_FRAMES retVal = {
      GetNumberOfPicsInDPB(),
      m_D3D12DPB.pResources.data(),
      m_D3D12DPB.pSubresources.data(),
      m_D3D12DPB.pHeaps.data(),
   };

   return retVal;
}

// number of resources in the pool that are marked as in use
UINT
D3D12TexturesArrayDPBManager::GetNumberOfInUseAllocations()
{
   UINT countOfInUseResourcesInPool = 0;
   for (auto &reusableRes : m_ResourcesPool) {
      if (!reusableRes.isFree) {
         countOfInUseResourcesInPool++;
      }
   }
   return countOfInUseResourcesInPool;
}

// Returns the number of pictures currently stored in the DPB
UINT
D3D12TexturesArrayDPBManager::GetNumberOfTrackedAllocations()
{
   assert(m_ResourcesPool.size() < UINT_MAX);
   return static_cast<UINT>(m_ResourcesPool.size());
}
