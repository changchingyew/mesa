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


#ifndef D3D12_VIDEO_DPB_STORAGE_MANAGER_INTERFACE_H
#define D3D12_VIDEO_DPB_STORAGE_MANAGER_INTERFACE_H

#include "d3d12_video_types.h"

struct D3D12_VIDEO_RECONSTRUCTED_PICTURE
{
   ID3D12Resource *pReconstructedPicture;
   UINT            ReconstructedPictureSubresource;
   IUnknown *pVideoHeap;
};

struct D3D12_VIDEO_REFERENCE_FRAMES
{
   UINT             NumTexture2Ds;
   ID3D12Resource **ppTexture2Ds;
   UINT *           pSubresources;
   IUnknown **ppHeaps;
};

// Defines interface for storing and retrieving the decoded picture buffer ID3D12Resources with
// the reconstructed pictures
// Implementors of this interface can decide how to do this, let Class1 and Class2 be implementors...
// for example Class1 can use a texture array and Class2 or an array of textures
class ID3D12VideoDPBStorageManager
{
   // ID3D12VideoDPBStorageManager
 public:
   // Adds a new reference frame at a given position
   virtual void InsertReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition) = 0;

   // Gets a reference frame at a given position
   virtual D3D12_VIDEO_RECONSTRUCTED_PICTURE GetReferenceFrame(UINT dpbPosition) = 0;

   // Assigns a reference frame at a given position
   virtual void AssignReferenceFrame(D3D12_VIDEO_RECONSTRUCTED_PICTURE pReconPicture, UINT dpbPosition) = 0;

   // Removes a new reference frame at a given position and returns operation success
   // pResourceUntracked is an optional output indicating if the removed resource was being tracked by the pool
   virtual bool RemoveReferenceFrame(UINT dpbPosition, bool *pResourceUntracked) = 0;

   // Returns the resource allocation for a NEW reconstructed picture
   virtual D3D12_VIDEO_RECONSTRUCTED_PICTURE GetNewTrackedPictureAllocation() = 0;

   // Returns whether it found the tracked resource on this instance pool tracking and was able to free it
   virtual bool UntrackReconstructedPictureAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem) = 0;

   // Returns true if the trackedItem was allocated (and is being tracked) by this class
   virtual bool IsTrackedAllocation(D3D12_VIDEO_RECONSTRUCTED_PICTURE trackedItem) = 0;

   // resource pool size
   virtual UINT GetNumberOfTrackedAllocations() = 0;

   // number of resources in the pool that are marked as in use
   virtual UINT GetNumberOfInUseAllocations() = 0;

   // Returns the number of pictures currently stored in the DPB
   virtual UINT GetNumberOfPicsInDPB() = 0;

   // Returns all the current reference frames stored in the storage manager
   virtual D3D12_VIDEO_REFERENCE_FRAMES GetCurrentFrameReferenceFrames() = 0;

   // Remove all pictures from DPB
   // returns the number of resources marked as reusable
   virtual UINT ClearDecodePictureBuffer() = 0;

   virtual ~ID3D12VideoDPBStorageManager()
   { }
};

#endif
