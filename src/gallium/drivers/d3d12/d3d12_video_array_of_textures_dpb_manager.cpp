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

#include "d3d12_video_array_of_textures_dpb_manager.h"
#include <algorithm>
///
/// ArrayOfTexturesDPBManager
///
// Differences with TexturesArrayDPBManager
// Uses an std::vector with individual D3D resources as backing storage instead of an D3D12 Texture Array
// Supports dynamic pool capacity extension (by pushing back a new D3D12Resource) of the pool

void ArrayOfTexturesDPBManager::CreateReconstructedPicAllocation(ID3D12Resource** ppResource)
{
    D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        CD3DX12_RESOURCE_DESC reconstructedPictureResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            m_encodeFormat,
            m_encodeResolution.Width,
            m_encodeResolution.Height,
            1,
            1,
            1,
            0,
            m_resourceAllocFlags
        );

        VERIFY_SUCCEEDED(m_pDevice->CreateCommittedResource(
            &Properties,
            D3D12_HEAP_FLAG_NONE,
            &reconstructedPictureResourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(ppResource)));
}

ArrayOfTexturesDPBManager::ArrayOfTexturesDPBManager(
    UINT dpbInitialSize,
    ID3D12Device* pDevice,
    DXGI_FORMAT encodeSessionFormat,
    D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC encodeSessionResolution,
    D3D12_RESOURCE_FLAGS resourceAllocFlags,
    bool setNullSubresourcesOnAllZero
) :
    m_dpbInitialSize(dpbInitialSize),
    m_pDevice(pDevice),
    m_encodeFormat(encodeSessionFormat),
    m_encodeResolution(encodeSessionResolution),
    m_resourceAllocFlags(resourceAllocFlags),
    m_NullSubresourcesOnAllZero(setNullSubresourcesOnAllZero)
{
    // Initialize D3D12 DPB exposed in this class implemented CRUD interface for a DPB
    assert(0u == ClearDecodePictureBuffer());
    
    // Implement a reusable pool of D3D12 Resources as an array of textures
    // Maximum in use resources for a DPB of size x is x+1 for cases when a P frame is using the x references in the L0 list and also using an extra resource to output it's own recon pic.
    m_ResourcesPool.resize(m_dpbInitialSize + 1u);

    // Build resource pool with commitedresources with a d3ddevice and the encoding session settings (eg. resolution) and the reference_only flag
    for(auto& reusableRes : m_ResourcesPool)
    {
        reusableRes.isFree = true;
        CreateReconstructedPicAllocation(reusableRes.pResource.GetAddressOf());
    }
}

UINT ArrayOfTexturesDPBManager::ClearDecodePictureBuffer()
{
    UINT untrackCount = 0;
    // Mark resources used in DPB as re-usable in the resources pool
    for(auto& dpbResource : m_D3D12DPB.pResources)
    {
        // Don't assert the untracking result here in case the DPB contains resources not adquired using the pool methods in this interface
        untrackCount += UntrackReconstructedPictureAllocation({ dpbResource, 0 }) ? 1 : 0;
    }

    // Clear DPB
    m_D3D12DPB.pResources.clear();
    m_D3D12DPB.pSubresources.clear();
    m_D3D12DPB.pResources.reserve(m_dpbInitialSize);
    m_D3D12DPB.pSubresources.reserve(m_dpbInitialSize);
    
    return untrackCount;
}

// Adds a new reference frame at a given position and returns operation success
void ArrayOfTexturesDPBManager::InsertReferenceFrame(D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition)
{
    if(pReconPicture.pReconstructedPicture == nullptr)
    {
        D3D12_LOG_ERROR("[ArrayOfTexturesDPBManager] InsertReferenceFrame - pReconPicture.pReconstructedPicture Cannot be nullptr.\n");
    }

    assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());

    if(dpbPosition > m_D3D12DPB.pResources.size())
    {
        // extend capacity
        m_D3D12DPB.pResources.resize(dpbPosition);
        m_D3D12DPB.pSubresources.resize(dpbPosition);
    }

    m_D3D12DPB.pResources.insert(m_D3D12DPB.pResources.begin() + dpbPosition, pReconPicture.pReconstructedPicture);
    m_D3D12DPB.pSubresources.insert(m_D3D12DPB.pSubresources.begin() + dpbPosition, pReconPicture.ReconstructedPictureSubresource);
}

// Gets a reference frame at a given position
D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE ArrayOfTexturesDPBManager::GetReferenceFrame(UINT dpbPosition)
{
    if(dpbPosition >= m_D3D12DPB.pResources.size())
    {
        D3D12_LOG_ERROR("[ArrayOfTexturesDPBManager] GetReferenceFrame - dpbPosition out of bounds.\n");
    }

    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE retVal = 
    {
        m_D3D12DPB.pResources[dpbPosition],
        m_D3D12DPB.pSubresources[dpbPosition]
    };

    return retVal;
}

// Removes a new reference frame at a given position and returns operation success
bool ArrayOfTexturesDPBManager::RemoveReferenceFrame(UINT dpbPosition, bool* pResourceUntracked)
{
    assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());

    if(dpbPosition >= m_D3D12DPB.pResources.size())
    {
        D3D12_LOG_ERROR("[ArrayOfTexturesDPBManager] RemoveReferenceFrame - dpbPosition out of bounds.\n");
    }

    // If removed resource came from resource pool, mark it as free
    // to free it for a new usage
    // Don't assert the untracking result here in case the DPB contains resources not adquired using the pool methods in this interface
    bool resUntracked = UntrackReconstructedPictureAllocation({ m_D3D12DPB.pResources[dpbPosition], 0 });

    if(pResourceUntracked != nullptr)
    {
        *pResourceUntracked = resUntracked;
    }

    // Remove from DPB tables
    m_D3D12DPB.pResources.erase(m_D3D12DPB.pResources.begin() + dpbPosition);
    m_D3D12DPB.pSubresources.erase(m_D3D12DPB.pSubresources.begin() + dpbPosition);

    return true;
}

// Returns true if the trackedItem was allocated (and is being tracked) by this class
bool ArrayOfTexturesDPBManager::IsTrackedAllocation(D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE trackedItem)
{
    for(auto& reusableRes : m_ResourcesPool)
    {
        if(trackedItem.pReconstructedPicture == reusableRes.pResource.Get() && !reusableRes.isFree)
        {
            return true;
        }            
    }
    return false;
}

// Returns whether it found the tracked resource on this instance pool tracking and was able to free it
bool ArrayOfTexturesDPBManager::UntrackReconstructedPictureAllocation(D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE trackedItem)
{
    for(auto& reusableRes : m_ResourcesPool)
    {
        if(trackedItem.pReconstructedPicture == reusableRes.pResource.Get())
        {
            reusableRes.isFree = true;
            return true;
        }            
    }
    return false;
}

// Returns a fresh resource for a new reconstructed picture to be written to
// this class implements the dpb allocations as an array of textures
D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE ArrayOfTexturesDPBManager::GetNewTrackedReconstructedPictureAllocation()
{
    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE freshAllocation = 
    {
        // pResource
        nullptr,
        // subresource
        0
    };

    // Find first (if any) available resource to (re-)use
    bool bAvailableResourceInPool = false;
    for(auto& reusableRes : m_ResourcesPool)
    {
        if(reusableRes.isFree)
        {
            bAvailableResourceInPool = true;
            freshAllocation.pReconstructedPicture = reusableRes.pResource.Get();
            reusableRes.isFree = false;
            break;
        }
    }

    if(!bAvailableResourceInPool)
    {
        // Expand resources pool by one
        D3D12_LOG_DBG("[ArrayOfTexturesDPBManager] ID3D12Resource Pool capacity (%ld) exceeded - extending capacity and appending new allocation at the end", m_ResourcesPool.size());
        ReusableResource newPoolEntry = { };
        newPoolEntry.isFree = false;
        CreateReconstructedPicAllocation(newPoolEntry.pResource.GetAddressOf());
        m_ResourcesPool.push_back(newPoolEntry);

        // Assign it to current ask
        freshAllocation.pReconstructedPicture = newPoolEntry.pResource.Get();
    }

    return freshAllocation;
}

UINT ArrayOfTexturesDPBManager::GetNumberOfPicsInDPB()
{
    assert(m_D3D12DPB.pResources.size() == m_D3D12DPB.pSubresources.size());

    assert(m_D3D12DPB.pResources.size() < UINT_MAX);
    return static_cast<UINT>(m_D3D12DPB.pResources.size());
}

D3D12_VIDEO_ENCODE_REFERENCE_FRAMES ArrayOfTexturesDPBManager::GetCurrentFrameReferenceFrames()
{
    // If all subresources are 0, the DPB is loaded with an array of individual textures, the D3D Encode API expects pSubresources to be null in this case
    // The D3D Decode API expects it to be non-null even with all zeroes.
    UINT* pSubresources = m_D3D12DPB.pSubresources.data();
    if((std::all_of(m_D3D12DPB.pSubresources.cbegin(), m_D3D12DPB.pSubresources.cend(), [](int i){ return i == 0; })) && m_NullSubresourcesOnAllZero) 
    {
        pSubresources = nullptr;
    }

    D3D12_VIDEO_ENCODE_REFERENCE_FRAMES retVal =
    {
        GetNumberOfPicsInDPB(),
        m_D3D12DPB.pResources.data(),
        pSubresources,
    };

    return retVal;    
}

// number of resources in the pool that are marked as in use
UINT ArrayOfTexturesDPBManager::GetNumberOfInUseAllocations()
{
    UINT countOfInUseResourcesInPool = 0;
    for(auto& reusableRes : m_ResourcesPool)
    {
        if(!reusableRes.isFree)
        {
            countOfInUseResourcesInPool++;
        }
    }
    return countOfInUseResourcesInPool;
}

// Returns the number of pictures currently stored in the DPB
UINT ArrayOfTexturesDPBManager::GetNumberOfTrackedAllocations()
{
    assert(m_ResourcesPool.size() < UINT_MAX);
    return static_cast<UINT>(m_ResourcesPool.size());
}