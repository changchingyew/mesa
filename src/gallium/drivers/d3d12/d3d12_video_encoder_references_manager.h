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


#ifndef D3D12_VIDEO_ENCODE_REFERENCES_MANAGER_INTERFACE_H
#define D3D12_VIDEO_ENCODE_REFERENCES_MANAGER_INTERFACE_H

#include "d3d12_video_types.h"

typedef struct D3D12VideoEncoderH264FrameDesc
{
    UINT64 idr_pic_id;
    D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 CurrentFrameType;
    UINT m_curPictureOrderCountNumber;
    UINT m_curFrameDecodingOrderNumber;
} D3D12VideoEncoderH264FrameDesc;

class ID3D12AutomaticVideoEncodeReferencePicManager
{
public:

    virtual void BeginFrame(D3D12VideoEncoderH264FrameDesc) = 0; // TODO: Templatize method BeginFrame<TCodecPictureData> and then use with D3D12VideoEncoderH264FrameDesc and other codecs
    virtual void EndFrame() = 0;
    virtual D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE GetCurrentFrameReconPicOutputAllocation() = 0;
    virtual void GetCurrentFramePictureControlData(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA& codecAllocation) = 0;
    virtual bool IsCurrentFrameUsedAsReference() = 0;
    virtual D3D12_VIDEO_ENCODE_REFERENCE_FRAMES GetCurrentFrameReferenceFrames() = 0;
    virtual ~ID3D12AutomaticVideoEncodeReferencePicManager() { }
};

#endif