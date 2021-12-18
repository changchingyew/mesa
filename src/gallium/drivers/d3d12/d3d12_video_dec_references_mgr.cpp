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

//----------------------------------------------------------------------------------------------------------------------------------
static UINT16 GetInvalidReferenceIndex(D3D12_VIDEO_DECODE_PROFILE_TYPE DecodeProfileType)
{
    assert(DecodeProfileType <= D3D12_VIDEO_DECODE_PROFILE_TYPE_MAX_VALID);

    switch (DecodeProfileType)
    {
        case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
            return DXVA_H264_INVALID_PICTURE_INDEX;
        default:
            return 0;
    };
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::GetCurrentFrameDecodeOutputTexture(ID3D12Resource** ppOutTexture2D, UINT* pOutSubresourceIndex)
{
    ///
    /// Create decode output texture
    ///

    // Returns a fresh texture from the pool.
    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE pFreshAllocation = m_upD3D12TexturesStorageManager->GetNewTrackedReconstructedPictureAllocation();

    *ppOutTexture2D = pFreshAllocation.pReconstructedPicture;
    *pOutSubresourceIndex = pFreshAllocation.ReconstructedPictureSubresource;
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
D3D12VidDecReferenceDataManager::D3D12VidDecReferenceDataManager(
    const struct d3d12_screen* pD3D12Screen,
    UINT NodeMask,
    D3D12_VIDEO_DECODE_PROFILE_TYPE DecodeProfileType,
    D3D12DPBDescriptor m_dpbDescriptor
)
    : m_pD3D12Screen(pD3D12Screen)
    , m_NodeMask(NodeMask)
    , m_invalidIndex(GetInvalidReferenceIndex(DecodeProfileType))
    , m_dpbDescriptor(m_dpbDescriptor)
    {
        this->Init();

        D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC targetFrameResolution = {m_dpbDescriptor.Width, m_dpbDescriptor.Height};
        D3D12_RESOURCE_FLAGS resourceAllocFlags = m_dpbDescriptor.fReferenceOnly ? (D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) : D3D12_RESOURCE_FLAG_NONE;
        if(m_dpbDescriptor.fArrayOfTexture)
        {
            m_upD3D12TexturesStorageManager = std::make_unique<ArrayOfTexturesDPBManager>(m_dpbDescriptor.dpbSize, m_pD3D12Screen->dev, m_dpbDescriptor.Format, targetFrameResolution, resourceAllocFlags);
        }
        else
        {
            m_upD3D12TexturesStorageManager = std::make_unique<TexturesArrayDPBManager>(m_dpbDescriptor.dpbSize, m_pD3D12Screen->dev, m_dpbDescriptor.Format, targetFrameResolution, resourceAllocFlags);
        }
    }

//----------------------------------------------------------------------------------------------------------------------------------
UINT16 D3D12VidDecReferenceDataManager::FindRemappedIndex(UINT16 originalIndex)
{
    // Check if the index is already mapped.
    for (UINT16 remappedIndex = 0; remappedIndex < referenceDatas.size(); remappedIndex++)
    {
        if (referenceDatas[remappedIndex].originalIndex == originalIndex)
        {
            return remappedIndex;
        }
    }

    return m_invalidIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
UINT16 D3D12VidDecReferenceDataManager::UpdateEntry(
                                                        UINT16 index, // in
                                                        ID3D12Resource*& pOutputReferenceNoRef, // out -> new reference slot assigned or nullptr
                                                        UINT& OutputSubresource, // out -> new reference slot assigned or 0
                                                        bool& outNeedsTransitionToDecodeRead // out -> indicates if output resource argument has to be transitioned to D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
                                                    )
{
    UINT16 remappedIndex = m_invalidIndex;

    if (index != m_invalidIndex)
    {
        remappedIndex = FindRemappedIndex(index);

        outNeedsTransitionToDecodeRead = true;
        if (   remappedIndex == m_invalidIndex
            || remappedIndex == m_currentOutputIndex)
        {
            // // Caller specified an invalid reference index.  Remap it to the current
            // // picture index to avoid crashing and still attempt to decode.
            // if (g_hTracelogging)
            // {
            //     TraceLoggingWrite(g_hTracelogging,
            //         "Decode - Invalid Reference Index",
            //         TraceLoggingValue(index, "Index"),
            //         TraceLoggingValue(m_currentOutputIndex, "OutputIndex"));
            // }
            fprintf(stderr, "[D3D12VidDecReferenceDataManager] Decode - Invalid Reference Index\n");

            remappedIndex = m_currentOutputIndex;

            // The output resource has already been transitioned to the DECODE_WRITE state when
            // set as the current output.  For use as a reference, the resource should be in a DECODE_READ state, 
            // but we can't express both so leave it in the WRITE state.  This is an error condition, so this is 
            // an attempt to keep the decoder producing output until we start getting correct reference indices again.
            outNeedsTransitionToDecodeRead = false;
        }

        decoderHeapsParameter[remappedIndex] = referenceDatas[remappedIndex].decoderHeap.Get();        
        textures[remappedIndex] = referenceDatas[remappedIndex].referenceTexture;
        texturesSubresources[remappedIndex] = referenceDatas[remappedIndex].subresourceIndex;        

        pOutputReferenceNoRef = outNeedsTransitionToDecodeRead ? referenceDatas[remappedIndex].referenceTexture : nullptr;
        OutputSubresource = outNeedsTransitionToDecodeRead ? referenceDatas[remappedIndex].subresourceIndex : 0u;
    }

    return remappedIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
UINT16 D3D12VidDecReferenceDataManager::GetUpdatedEntry(UINT16 index)
{
    UINT16 remappedIndex = m_invalidIndex;

    if (index != m_invalidIndex)
    {
        remappedIndex = FindRemappedIndex(index);

        if (remappedIndex == m_invalidIndex)
        {
            // Caller specified an invalid reference index.  Remap it to the current
            // picture index to avoid crashing and still attempt to decode.
            remappedIndex = m_currentOutputIndex;
        }
    }

    return remappedIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
UINT16 D3D12VidDecReferenceDataManager::StoreFutureReference(UINT16 index, ComPtr<ID3D12VideoDecoderHeap>& decoderHeap, ID3D12Resource* pTexture2D, UINT subresourceIndex)
{
    // Check if the index was in use.
    UINT16 remappedIndex = FindRemappedIndex(index);

    if (remappedIndex == m_invalidIndex)
    {
        // If not already mapped, see if the same index in the remapped space is available.
        if (   index < referenceDatas.size()
            && referenceDatas[index].originalIndex == m_invalidIndex)
        {
            remappedIndex = index;
        }
    }

    if (remappedIndex == m_invalidIndex)
    {
        // The current output index was not used last frame.  Get an unused entry.
        remappedIndex = FindRemappedIndex(m_invalidIndex);
    }

    if (remappedIndex == m_invalidIndex)
    {
        D3D12_LOG_ERROR("[D3D12 Video Driver Error] D3D12VidDecReferenceDataManager - Decode - No available reference map entry for output.\n");
    }

    ReferenceData& referenceData = referenceDatas[remappedIndex];

    // Set the index as the key in this map entry.
    referenceData.originalIndex = index;

    referenceData.decoderHeap = decoderHeap;

    // When IsReferenceOnly is true, then the translation layer is managing references
    // either becasue the layout is incompatible with other texture usage (REFERENCE_ONLY), or because and/or 
    // decode output conversion is enabled.
    if (!IsReferenceOnly())
    {
        referenceData.referenceTexture = pTexture2D;
        referenceData.subresourceIndex = subresourceIndex;
    }

    // Store the index to use for error handling when caller specifies and invalid reference index.
    m_currentOutputIndex = remappedIndex;

    return remappedIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
void D3D12VidDecReferenceDataManager::GetReferenceOnlyOutput(
    ID3D12Resource*& pOutputReferenceNoRef, // out -> new reference slot assigned or nullptr
    UINT& OutputSubresource, // out -> new reference slot assigned or nullptr
    bool& outNeedsTransitionToDecodeWrite // out -> indicates if output resource argument has to be transitioned to D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
    )
{
    if(!IsReferenceOnly())
    {
        D3D12_LOG_ERROR("[D3D12 Video Driver Error] D3D12VidDecReferenceDataManager::GetReferenceOnlyOutput expected IsReferenceOnly() to be true.\n");
    }

    ReferenceData& referenceData = referenceDatas[m_currentOutputIndex];

    pOutputReferenceNoRef = referenceData.referenceTexture;
    OutputSubresource = referenceData.subresourceIndex;
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::MarkReferenceInUse(UINT16 index)
{
    if (index != m_invalidIndex)
    {
        UINT16 remappedIndex = FindRemappedIndex(index);
        if (remappedIndex != m_invalidIndex)
        {
            referenceDatas[remappedIndex].fUsed = true;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::ReleaseUnusedReferences()
{
    for (ReferenceData& referenceData : referenceDatas)
    {
        if (!referenceData.fUsed)
        {
            referenceData.decoderHeap = nullptr;

            if (!IsReferenceOnly())
            {
                referenceData.referenceTexture = nullptr;
                referenceData.subresourceIndex = 0;
            }

            referenceData.originalIndex = m_invalidIndex;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
void D3D12VidDecReferenceDataManager::Init()
{
    ResizeDataStructures(m_dpbDescriptor.dpbSize);
    ResetInternalTrackingReferenceUsage();
    ResetReferenceFramesInformation();
    ReleaseUnusedReferences();

    if (m_dpbDescriptor.fReferenceOnly)
    {
        D3D12ResourceHeapCombinedDesc requiredResourceArgs = {};

        if (m_dpbDescriptor.fArrayOfTexture)
        {
            requiredResourceArgs.m_desc12 = CD3DX12_RESOURCE_DESC::Tex2D(m_dpbDescriptor.Format, m_dpbDescriptor.Width, m_dpbDescriptor.Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
            UINT64 requiredResourceSize = 0;
            m_pD3D12Screen->dev->GetCopyableFootprints(&requiredResourceArgs.m_desc12, 0, 1, 0, nullptr, nullptr, nullptr, &requiredResourceSize);
            requiredResourceArgs.m_heapDesc = CD3DX12_HEAP_DESC(requiredResourceSize, CD3DX12_HEAP_PROPERTIES((D3D12_HEAP_TYPE_DEFAULT), m_NodeMask, m_NodeMask));

            for (ReferenceData& referenceData : referenceDatas)
            {
                D3D12_HEAP_PROPERTIES currentHeapProps = { };
                D3D12_HEAP_FLAGS currentHeapFlags = { };
                HRESULT hr = referenceData.referenceOnlyTexture->GetHeapProperties(&currentHeapProps, &currentHeapFlags);
                if(FAILED(hr))
                {
                    D3D12_LOG_ERROR("[D3D12 Video Driver Error] D3D12VidDecReferenceDataManager::Resize - GetHeapProperties failed with HR %x\n", hr);
                }
                D3D12_RESOURCE_DESC currentResourceDesc = referenceData.referenceOnlyTexture->GetDesc();
                UINT64 currentResourceSize = 0;
                m_pD3D12Screen->dev->GetCopyableFootprints(&currentResourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &currentResourceSize);

                D3D12ResourceHeapCombinedDesc existingResourceArgs = 
                {
                    currentResourceDesc,
                    CD3DX12_HEAP_DESC(currentResourceSize, currentHeapProps, 0L, currentHeapFlags)
                };

                if (   !referenceData.referenceOnlyTexture
                    || 0 != memcmp(&existingResourceArgs, &requiredResourceArgs, sizeof(D3D12ResourceHeapCombinedDesc)))
                {
                    hr = m_pD3D12Screen->dev->CreateCommittedResource(
                        &requiredResourceArgs.m_heapDesc.Properties,
                        D3D12_HEAP_FLAG_NONE,
                        &requiredResourceArgs.m_desc12,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(referenceData.referenceOnlyTexture.GetAddressOf()));
                    if(FAILED(hr))
                    {
                        D3D12_LOG_ERROR("[D3D12 Video Driver Error] D3D12VidDecReferenceDataManager::Resize - CreateCommittedResource failed with HR %x\n", hr);
                    }
                }

                referenceData.referenceTexture = referenceData.referenceOnlyTexture.Get();
                referenceData.subresourceIndex = 0u;
            }
        }
        else
        {
            requiredResourceArgs.m_desc12 = CD3DX12_RESOURCE_DESC::Tex2D(m_dpbDescriptor.Format, m_dpbDescriptor.Width, m_dpbDescriptor.Height, m_dpbDescriptor.dpbSize, 1, 1, 0, D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
            UINT64 requiredResourceSize = 0;
            m_pD3D12Screen->dev->GetCopyableFootprints(&requiredResourceArgs.m_desc12, 0, 1, 0, nullptr, nullptr, nullptr, &requiredResourceSize);
            requiredResourceArgs.m_heapDesc = CD3DX12_HEAP_DESC(requiredResourceSize, CD3DX12_HEAP_PROPERTIES((D3D12_HEAP_TYPE_DEFAULT), m_NodeMask, m_NodeMask));

            ComPtr<ID3D12Resource> spReferenceOnlyTextureArray;
            
            HRESULT hr = m_pD3D12Screen->dev->CreateCommittedResource(
                        &requiredResourceArgs.m_heapDesc.Properties,
                        D3D12_HEAP_FLAG_NONE,
                        &requiredResourceArgs.m_desc12,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(spReferenceOnlyTextureArray.GetAddressOf()));

            if(FAILED(hr))
            {
                D3D12_LOG_ERROR("[D3D12 Video Driver Error] D3D12VidDecReferenceDataManager::Resize - CreateCommittedResource failed with HR %x\n", hr);
            }

            for (size_t i = 0; i < referenceDatas.size(); i++)
            {
                referenceDatas[i].referenceOnlyTexture = spReferenceOnlyTextureArray.Get();
                referenceDatas[i].referenceTexture = spReferenceOnlyTextureArray.Get();
                referenceDatas[i].subresourceIndex = static_cast<UINT>(i);
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::ResizeDataStructures(UINT size)
{
    textures.resize(size);
    texturesSubresources.resize(size);
    decoderHeapsParameter.resize(size);
    referenceDatas.resize(size);
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::ResetReferenceFramesInformation()
{
    for (UINT index = 0; index < Size(); index++)
    {
        textures[index] = nullptr;
        texturesSubresources[index] = 0;
        decoderHeapsParameter[index] = nullptr;
    }
}

//----------------------------------------------------------------------------------------------------------------------------------
void D3D12VidDecReferenceDataManager::ResetInternalTrackingReferenceUsage()
{
    for (UINT index = 0; index < Size(); index++)
    {
        referenceDatas[index].fUsed = false;
    }
}