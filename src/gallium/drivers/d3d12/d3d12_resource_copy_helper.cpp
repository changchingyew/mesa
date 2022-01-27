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

#include "d3d12_resource_copy_helper.h"

d3d12_resource_copy_helper::d3d12_resource_copy_helper(ID3D12CommandQueue *pCommandQueue)
   : m_pCommandQueue(pCommandQueue), m_NodeMask(pCommandQueue->GetDesc().NodeMask)
{
   D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = pCommandQueue->GetDesc();

   VERIFY_SUCCEEDED(m_pCommandQueue->GetDevice(IID_PPV_ARGS(&m_pDevice)));

   VERIFY_SUCCEEDED(
      m_pDevice->CreateCommandAllocator(CommandQueueDesc.Type, IID_PPV_ARGS(m_pCommandAllocator.GetAddressOf())));

   VERIFY_SUCCEEDED(m_pDevice->CreateCommandList(m_NodeMask,
                                                 CommandQueueDesc.Type,
                                                 m_pCommandAllocator.Get(),
                                                 nullptr,
                                                 IID_PPV_ARGS(&m_pCommandList)));
}

void
d3d12_resource_copy_helper::upload_data(ID3D12Resource *      pResource,
                                    UINT                  Subresource,
                                    D3D12_RESOURCE_STATES ResourceState,
                                    const void *          pData,
                                    UINT                  RowPitch,
                                    UINT                  SlicePitch)
{
   // Determine the layout necessary for copying
   D3D12_RESOURCE_DESC ResourceDesc = pResource->GetDesc();

   D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout;
   UINT                               NumRows;
   UINT64                             RowSize;
   UINT64                             BufferSize;

   m_pDevice->GetCopyableFootprints(&ResourceDesc, Subresource, 1, 0, &Layout, &NumRows, &RowSize, &BufferSize);

   // Create a CPU-visible buffer matching pResource
   ComPtr<ID3D12Resource> pTempResource;
   auto                   descHeap     = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD, m_NodeMask, m_NodeMask);
   auto                   descResource = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
   VERIFY_SUCCEEDED(m_pDevice->CreateCommittedResource(&descHeap,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &descResource,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                                       nullptr,
                                                       IID_PPV_ARGS(&pTempResource)));

   // memcpy data into pTempResource
   {
      void *pMappedData = nullptr;
      VERIFY_SUCCEEDED(pTempResource->Map(0, nullptr, &pMappedData));

      D3D12_MEMCPY_DEST DestRecord = { pMappedData, Layout.Footprint.RowPitch, Layout.Footprint.RowPitch * NumRows };

      D3D12_SUBRESOURCE_DATA SrcRecord = { pData, RowPitch, SlicePitch };

      size_t copySize = static_cast<size_t>(RowSize);
      if (ResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         // Allow suballocation of dst buffers, if src is smaller than dst, just copy the src size into dst.
         assert(NumRows == 1);
         assert(Layout.Footprint.Depth == 1);
         assert(RowPitch == SlicePitch);

         copySize = std::min(RowSize, static_cast<UINT64>(RowPitch));
      }

      MemcpySubresource(&DestRecord, &SrcRecord, copySize, NumRows, Layout.Footprint.Depth);

      pTempResource->Unmap(0, nullptr);
   }

   // Record command to copy data from pTempResource to pResource
   {
      D3D12ScopedStateTransition<ID3D12GraphicsCommandList> ResourceTransition(m_pCommandList.Get(),
                                                                               pResource,
                                                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                                                               ResourceState,
                                                                               Subresource);

      if (ResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         m_pCommandList->CopyBufferRegion(pResource, Layout.Offset, pTempResource.Get(), 0, ResourceDesc.Width);
      } else {
         D3D12_TEXTURE_COPY_LOCATION ResourceCopyLocation = CD3DX12_TEXTURE_COPY_LOCATION(pResource, Subresource);

         D3D12_PLACED_SUBRESOURCE_FOOTPRINT ZeroPlacement = Layout;
         ZeroPlacement.Offset                             = 0;

         D3D12_TEXTURE_COPY_LOCATION TempResourceCopyLocation =
            CD3DX12_TEXTURE_COPY_LOCATION(pTempResource.Get(), ZeroPlacement);

         m_pCommandList->CopyTextureRegion(&ResourceCopyLocation, 0, 0, 0, &TempResourceCopyLocation, nullptr);
      }
   }

   Epilog();
}

void
d3d12_resource_copy_helper::readback_data(void *                pData,
                                      UINT                  RowPitch,
                                      UINT                  SlicePitch,
                                      ID3D12Resource *      pResource,
                                      UINT                  Subresource,
                                      D3D12_RESOURCE_STATES ResourceState)
{
   // Determine the layout necessary for copying
   D3D12_RESOURCE_DESC ResourceDesc = pResource->GetDesc();

   D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout;
   UINT                               NumRows;
   UINT64                             RowSize;
   UINT64                             BufferSize;

   m_pDevice->GetCopyableFootprints(&ResourceDesc, Subresource, 1, 0, &Layout, &NumRows, &RowSize, &BufferSize);

   // Create a CPU-visible buffer matching pResource
   ComPtr<ID3D12Resource> pTempResource;
   auto                   descHeap     = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK, m_NodeMask, m_NodeMask);
   auto                   descResource = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
   VERIFY_SUCCEEDED(m_pDevice->CreateCommittedResource(&descHeap,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &descResource,
                                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                                       nullptr,
                                                       IID_PPV_ARGS(&pTempResource)));


   {
      // Record command to copy data from pResource to pTempResource
      D3D12ScopedStateTransition<ID3D12GraphicsCommandList> ResourceTransition(m_pCommandList.Get(),
                                                                               pResource,
                                                                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                                               ResourceState,
                                                                               Subresource);

      if (ResourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         m_pCommandList->CopyBufferRegion(pTempResource.Get(), 0, pResource, Layout.Offset, ResourceDesc.Width);
      } else {
         D3D12_TEXTURE_COPY_LOCATION ResourceCopyLocation = CD3DX12_TEXTURE_COPY_LOCATION(pResource, Subresource);

         D3D12_PLACED_SUBRESOURCE_FOOTPRINT ZeroPlacement = Layout;
         ZeroPlacement.Offset                             = 0;

         D3D12_TEXTURE_COPY_LOCATION TempResourceCopyLocation =
            CD3DX12_TEXTURE_COPY_LOCATION(pTempResource.Get(), ZeroPlacement);

         m_pCommandList->CopyTextureRegion(&TempResourceCopyLocation, 0, 0, 0, &ResourceCopyLocation, nullptr);
      }
   }

   // Execute the copy
   Epilog();

   // memcpy to the output pointer
   {
      void *pMappedData = nullptr;
      VERIFY_SUCCEEDED(pTempResource->Map(0, nullptr, &pMappedData));

      D3D12_MEMCPY_DEST DestRecord = { pData, RowPitch, SlicePitch };

      D3D12_SUBRESOURCE_DATA SrcRecord = { pMappedData, Layout.Footprint.RowPitch, Layout.Footprint.RowPitch * NumRows };

      MemcpySubresource(&DestRecord, &SrcRecord, static_cast<size_t>(RowSize), NumRows, Layout.Footprint.Depth);

      pTempResource->Unmap(0, nullptr);
   }
}

void
d3d12_resource_copy_helper::Epilog()
{
   // Execute the command list, wait for it to finish
   VERIFY_SUCCEEDED(m_pCommandList->Close());

   constexpr UINT64    fenceValue = 1u;
   ComPtr<ID3D12Fence> spFence;
   VERIFY_SUCCEEDED(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(spFence.GetAddressOf())));

   m_pCommandQueue->ExecuteCommandLists(1, CommandListCast(m_pCommandList.GetAddressOf()));
   m_pCommandQueue->Signal(spFence.Get(), fenceValue);
   spFence->SetEventOnCompletion(fenceValue, nullptr);

   // Put the command list back into a recording state
   VERIFY_SUCCEEDED(m_pCommandAllocator->Reset());

   VERIFY_SUCCEEDED(m_pCommandList->Reset(m_pCommandAllocator.Get(), nullptr));
}
