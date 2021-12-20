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

#ifndef D3D12_VIDEO_DEC_REFMGR_H
#define D3D12_VIDEO_DEC_REFMGR_H

#include "d3d12_video_dec_types.h"
#include "d3d12_video_dpb_storage_manager.h"

typedef struct D3D12ResourceHeapCombinedDesc
{
    D3D12_RESOURCE_DESC m_desc12;
    D3D12_HEAP_DESC m_heapDesc;
} D3D12ResourceHeapCombinedDesc;

struct D3D12VidDecReferenceDataManager
{
    D3D12VidDecReferenceDataManager(
        const struct d3d12_screen* pD3D12Screen,
        UINT NodeMask,
        D3D12_VIDEO_DECODE_PROFILE_TYPE DecodeProfileType,
        D3D12DPBDescriptor dpbDescriptor);

    bool IsReferenceOnly() { return m_dpbDescriptor.fReferenceOnly; }
    bool IsArrayOfTextures() { return m_dpbDescriptor.fArrayOfTexture; }
    
    void MarkAllReferencesAsUnused();
    void ReleaseUnusedReferencesTexturesMemory();

    template<typename T, size_t size> 
    void MarkReferencesInUse(const T (&picEntries)[size]);
    void MarkReferenceInUse(UINT16 index);
    
    UINT16 StoreFutureReference(UINT16 index, _In_ ComPtr<ID3D12VideoDecoderHeap> & decoderHeap, ID3D12Resource* pTexture2D, UINT subresourceIndex);
    
    // Will clear() argument outNeededTransitions and fill it with the necessary transitions to perform by the caller after the method returns
    template<typename T, size_t size> 
    void UpdateEntries(T (&picEntries)[size], std::vector<D3D12_RESOURCE_BARRIER> & outNeededTransitions);

    void GetReferenceOnlyOutput(
        ID3D12Resource*& pOutputReferenceNoRef, // out -> new reference slot assigned or nullptr
        UINT& OutputSubresource, // out -> new reference slot assigned or nullptr
        bool& outNeedsTransitionToDecodeWrite // out -> indicates if output resource argument has to be transitioned to D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
    );

    // Gets the output texture for the current frame to be decoded
    void GetCurrentFrameDecodeOutputTexture(ID3D12Resource** ppOutTexture2D, UINT* pOutSubresourceIndex);

    D3D12_VIDEO_DECODE_REFERENCE_FRAMES GetCurrentFrameReferenceFrames();

private:

    UINT16 UpdateEntry(
                UINT16 index, // in
                ID3D12Resource*& pOutputReferenceNoRef, // out -> new reference slot assigned or nullptr
                UINT& OutputSubresource, // out -> new reference slot assigned or 0
                bool& outNeedsTransitionToDecodeRead // out -> indicates if output resource argument has to be transitioned to D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
            );

    UINT16 FindRemappedIndex(UINT16 originalIndex);
    
    struct ReferenceData
    {
        UINT16 originalIndex;
        bool fUsed;
    };

    std::unique_ptr<ID3D12VideoDPBStorageManager<ID3D12VideoDecoderHeap> > m_upD3D12TexturesStorageManager;
    std::vector<ReferenceData> m_referenceDXVAIndices;
    const struct d3d12_screen* m_pD3D12Screen;
    UINT                       m_NodeMask;
    UINT16                     m_invalidIndex;
    D3D12DPBDescriptor         m_dpbDescriptor = { };
    UINT16                     m_currentOutputIndex = 0;    
};


//----------------------------------------------------------------------------------------------------------------------------------
template<typename T, size_t size>
void D3D12VidDecReferenceDataManager::UpdateEntries(T (&picEntries)[size], std::vector<D3D12_RESOURCE_BARRIER> & outNeededTransitions)
{
    outNeededTransitions.clear();

    for (auto& picEntry : picEntries)
    {
            // UINT16 UpdateEntry(
            //     UINT16 index, // in
            //     ID3D12Resource*& pOutputReferenceNoRef, // out -> new reference slot assigned or nullptr
            //     UINT& OutputSubresource, // out -> new reference slot assigned or 0
            //     bool& outNeedsTransitionToDecodeRead // out -> indicates if output resource argument has to be transitioned to D3D12_RESOURCE_STATE_VIDEO_DECODE_READ by the caller
            // );

        ID3D12Resource* pOutputReferenceNoRef = { };
        UINT OutputSubresource = 0u;
        bool outNeedsTransitionToDecodeRead = false;

        picEntry.Index7Bits = UpdateEntry(picEntry.Index7Bits, pOutputReferenceNoRef, OutputSubresource, outNeedsTransitionToDecodeRead);
        if(outNeedsTransitionToDecodeRead)
        {
            outNeededTransitions.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pOutputReferenceNoRef, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, OutputSubresource));
        }        
    }
}

//----------------------------------------------------------------------------------------------------------------------------------    
template<typename T, size_t size> 
void D3D12VidDecReferenceDataManager::MarkReferencesInUse(const T (&picEntries)[size])
{
    for (auto& picEntry : picEntries)
    {
        MarkReferenceInUse(picEntry.Index7Bits);
    }
}

#endif
