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

#ifndef D3D12_RESOURCE_COPY_HELPER_H
#define D3D12_RESOURCE_COPY_HELPER_H

#include "d3d12_video_dec_types.h"
#include "d3d12_state_transition_helper.h"

// Copies data into/out of D3D12 resources
// Note that all copy operations are synchronous (they imply a wait for idle)
class D3D12ResourceCopyHelper
{
public:
    D3D12ResourceCopyHelper(ID3D12CommandQueue* pCommandQueue);

    void CopySubresource(
        ID3D12Resource* pSrcResource,
        UINT srcSubresource,
        D3D12_RESOURCE_STATES srcResourceState,
        ID3D12Resource* pDstResource,
        UINT dstSubresource,
        D3D12_RESOURCE_STATES dstResourceState
        );

    void UploadData(
        ID3D12Resource* pResource,
        UINT Subresource,
        D3D12_RESOURCE_STATES ResourceState,
        const void* pData,
        UINT RowPitch,
        UINT SlicePitch
        );

    void ReadbackData(
        void* pData,
        UINT RowPitch,
        UINT SlicePitch,
        ID3D12Resource* pResource,
        UINT Subresource,
        D3D12_RESOURCE_STATES ResourceState
        );

private:
    void Epilog();

    ComPtr<ID3D12Device> m_pDevice;
    ComPtr<ID3D12CommandQueue> m_pCommandQueue;
    ComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_pCommandList;

    UINT m_NodeMask;
};

#endif