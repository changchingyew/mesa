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

class ID3D12AutomaticVideoEncodeReferencePicManager
{

// ID3D12AutomaticVideoEncodeReferencePicManager
public:

    // Method to configure GOP pattern; call on implementation class construction and allows possibility to reconfigure GOP later
    // Returns true if GOP was successfully (re)-initialized
    // Return false if GOP cannot be reconfigured by implementation
    // Example GOP definition; Let A=GOPLength; B=PPicturePeriod => produces this cyclic GOP
    // A=0; B=1 => IPPPPPPPP...
    // A=0; B=2 => IBPBPBPBP...
    // A=0; B=3 => IBBPBBPBB...
    // A=1; B=0 => IIIIIIIII...
    // A=2; B=1 => IPIPIPIPI...
    // A=3; B=1 => IPPIPPIPP...
    // A=3; B=2 => IBPIBPIBP...
    // A=4; B=3 => IBBPIBBPIBBP...
    virtual bool ConfigureGOP(
        // Indicates the distance between I-frames in the sequence, or the number of pictures on a GOP. If set to 0, only the first frame will be an I frame (infinite GOP).
        UINT GOPLength,
        // Indicates the period for P-frames to be inserted within the GOP. Note that if GOPLength is set to 0 for infinite GOP, this value must be greater than zero.
        UINT PPicturePeriod,
        // Insert IDR frame instead of I frame on GOP cycle restart if set
        bool forceGOPIDRs = true
    ) = 0;

    virtual bool IsGopSupported(
        UINT GOPLength,
        UINT PPicturePeriod
    ) = 0;

    // Advances internal state to next frame in GOP; subsequent calls to GetCurrentFrame* point to the advanced frame status    
    virtual void AdvanceFrame() = 0;

    // Returns the resource allocation for a reconstructed picture output for the current frame
    virtual D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE GetCurrentFrameReconPicOutputAllocation() = 0;

    // Calculates the picture control structure for the current frame
    virtual void GetCurrentFramePictureControlData(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA& codecAllocation) = 0;

    virtual bool IsCurrentFrameUsedAsReference() = 0;
    
    virtual D3D12_VIDEO_ENCODE_REFERENCE_FRAMES GetCurrentFrameReferenceFrames() = 0;

    virtual void GetCurrentGOP(UINT& GOPLength, UINT& PPicturePeriod) = 0;

    virtual ~ID3D12AutomaticVideoEncodeReferencePicManager() { }
};

#endif