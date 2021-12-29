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

#ifndef D3D12_STATE_TRANSITION_HELPER_H
#define D3D12_STATE_TRANSITION_HELPER_H

#include "d3d12_format_utils.h"

template <class TCommandList>
class D3D12ScopedStateTransition
{
public:
    D3D12ScopedStateTransition() :
        m_List(NULL),
        m_Resource(NULL),
        m_OriginalUsage(D3D12_RESOURCE_STATE_COMMON)
    {

    }

    D3D12ScopedStateTransition(
        TCommandList *List,
        ID3D12Resource *Resource,
        D3D12_RESOURCE_STATES DestinationUsage,
        D3D12_RESOURCE_STATES OriginalUsage,
        UINT Subresource = 0
        ) :
        m_List(List),
        m_Resource(Resource),
        m_OriginalUsage(OriginalUsage),
        m_DestUsage(DestinationUsage),
        m_Subresource(Subresource)
    {
        if (m_OriginalUsage != m_DestUsage)
        {
            ResourceBarrierHelper(m_List, m_Resource, m_OriginalUsage, m_DestUsage, m_Subresource);
        }
    }

    ~D3D12ScopedStateTransition()
    {
        if (m_List && (m_OriginalUsage != m_DestUsage))
        {
            ResourceBarrierHelper(m_List, m_Resource, m_DestUsage, m_OriginalUsage, m_Subresource);
        }
    }

private:
    TCommandList *m_List;
    ID3D12Resource* m_Resource;
    D3D12_RESOURCE_STATES m_OriginalUsage;
    D3D12_RESOURCE_STATES m_DestUsage;
    UINT m_Subresource;

    void
    ResourceBarrierHelper (
        TCommandList *List,
        ID3D12Resource *Resource,
        UINT StateBefore,
        UINT StateAfter,
        UINT Subresource
        )
    {
        D3D12_RESOURCE_BARRIER Barrier = { };
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Resource;
        Barrier.Transition.Subresource = Subresource;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATES( StateBefore );
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATES( StateAfter );
        List->ResourceBarrier(1, &Barrier);
    }
};

template <class TCommandList>
void d3d12_record_state_transition(
    ComPtr<TCommandList> & spCommandList,
    _In_ ID3D12Resource* pResource,
    D3D12_RESOURCE_STATES stateBefore,
    D3D12_RESOURCE_STATES stateAfter,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE )
{
    D3D12_RESOURCE_BARRIER rgBarrierDescs[1] = 
   {
      CD3DX12_RESOURCE_BARRIER::Transition(pResource, stateBefore, stateAfter, subresource, flags),
   };
   spCommandList->ResourceBarrier(1u, rgBarrierDescs);
}

#endif