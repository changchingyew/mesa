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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_resource.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"
#include "d3d12_video_dec.h"
#include "d3d12_state_transition_helper.h"
#include "d3d12_video_buffer.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "util/vl_vlc.h"

struct pipe_video_codec *d3d12_video_create_decoder(struct pipe_context *context,
                                               const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_decoder
   ///

   struct d3d12_video_decoder* pD3D12Dec = new d3d12_video_decoder; // Not using new doesn't call ctor and the initializations in the class declaration are lost
   if (!pD3D12Dec)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - Could not allocate memory for d3d12_video_decoder\n");
      return nullptr;
   }

	pD3D12Dec->base = *codec;
	pD3D12Dec->m_screen = context->screen;

	pD3D12Dec->base.context = context;
	pD3D12Dec->base.width = codec->width;
	pD3D12Dec->base.height = codec->height;
   // Only fill methods that are supported by the d3d12 decoder, leaving null the rest (ie. encode_* / decode_macroblock / get_feedback for encode)
	pD3D12Dec->base.destroy = d3d12_video_destroy;
	pD3D12Dec->base.begin_frame = d3d12_video_begin_frame;
	pD3D12Dec->base.decode_bitstream = d3d12_video_decode_bitstream;
	pD3D12Dec->base.end_frame = d3d12_video_end_frame;
	pD3D12Dec->base.flush = d3d12_video_flush;   
   pD3D12Dec->m_InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE; // Assume progressive for now, can adjust in d3d12_video_begin_frame with argument target.interlaced;
   pD3D12Dec->m_MaxReferencePicsWithCurrentPic = codec->max_references + 1u; // Add an extra one for the current decoded picture recon picture.
   
   pD3D12Dec->m_decodeFormat = d3d12_convert_pipe_video_profile_to_dxgi_format(codec->profile);
   pD3D12Dec->m_d3d12DecProfileType = d3d12_convert_pipe_video_profile_to_profile_type(codec->profile);
   pD3D12Dec->m_d3d12DecProfile = d3d12_convert_pipe_video_profile_to_d3d12_video_decode_profile(codec->profile);

   ///
   /// Try initializing D3D12 Video device and check for device caps
   ///

   struct d3d12_context* pD3D12Ctx = (struct d3d12_context*) context;
   pD3D12Dec->m_pD3D12Screen = d3d12_screen(pD3D12Ctx->base.screen);

   ///
   /// Create decode objects
   ///

   if(FAILED(pD3D12Dec->m_pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(pD3D12Dec->m_spD3D12VideoDevice.GetAddressOf()))))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - D3D12 Device has no Video support\n");
      goto failed;
   }

   if(!d3d12_check_caps_and_create_video_decoder_and_heap(pD3D12Dec->m_pD3D12Screen, pD3D12Dec))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - Failure on d3d12_check_caps_and_create_video_decoder_and_heap\n");
      goto failed;
   }

   if(!d3d12_create_video_command_objects(pD3D12Dec->m_pD3D12Screen, pD3D12Dec))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - Failure on d3d12_create_video_command_objects\n");
      goto failed;
   }

   if(!d3d12_create_video_dpbmanagers(pD3D12Dec->m_pD3D12Screen, pD3D12Dec))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - Failure on d3d12_create_video_dpbmanagers\n");
      goto failed;
   }

   if(!d3d12_create_video_state_buffers(pD3D12Dec->m_pD3D12Screen, pD3D12Dec))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_create_decoder - Failure on d3d12_create_video_state_buffers\n");
      goto failed;
   }   

   return &pD3D12Dec->base;

failed:
   if (pD3D12Dec != nullptr)
   {
      d3d12_video_destroy((struct pipe_video_codec*) pD3D12Dec);
   }

   return nullptr;
}

/**
 * Destroys a d3d12_video_decoder
 * Call destroy_XX for applicable XX nested member types before deallocating 
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of cleanup from failure on the creation method
*/
void d3d12_video_destroy(struct pipe_video_codec *codec)
{
   if(codec == nullptr)
   {
      return;
   }

   d3d12_video_flush(codec); // Flush pending work before destroying.

   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;

   //
   // Destroys a decoder
   // Call destroy_XX for applicable XX nested member types before deallocating 
   // Destroy methods should check != nullptr on their input target argument as this method can be called as part of cleanup from failure on the creation method
   //

   // No need for d3d12_destroy_video_objects
   //    All the objects created here are smart pointer members of d3d12_video_decoder
   // No need for d3d12_destroy_video_decoder_and_heap
   //    All the objects created here are smart pointer members of d3d12_video_decoder
   // No need for d3d12_destroy_video_dpbmanagers
   //    All the objects created here are smart pointer members of d3d12_video_decoder

   // No need for m_pD3D12Screen as it is not managed by d3d12_video_decoder

   // Call dtor to make ComPtr work
   delete pD3D12Dec;
}

/**
 * start decoding of a new frame
 */
void d3d12_video_begin_frame(struct pipe_video_codec *codec,
                     struct pipe_video_buffer *target,
                     struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on decoder creation, re-config (if any) happens in d3d12_video_decode_bitstream
   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;
   assert(pD3D12Dec);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_begin_frame started for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_begin_frame finalized for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
}

/**
 * decode a bitstream
 */
void d3d12_video_decode_bitstream(struct pipe_video_codec *codec,
                           struct pipe_video_buffer *target,
                           struct pipe_picture_desc *picture,
                           unsigned num_buffers,
                           const void * const *buffers,
                           const unsigned *sizes)
{
   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;
   assert(pD3D12Dec);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_decode_bitstream started for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   assert(pD3D12Dec->m_pD3D12Screen);
   struct d3d12_screen* pD3D12Screen = (struct d3d12_screen*) pD3D12Dec->m_pD3D12Screen;

   ///
   /// Caps check
   ///

   // Let's quickly make sure we can decode what's being asked for.

   int capsResult = d3d12_screen_get_video_param(&pD3D12Screen->base,
                               codec->profile,
                               PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
                               PIPE_VIDEO_CAP_SUPPORTED);
   assert(capsResult != 0);

   ///
   /// Compressed bitstream buffers
   ///

   /// Mesa VA frontend Video buffer passing semantics for H264, HEVC, MPEG4, VC1 and PIPE_VIDEO_PROFILE_VC1_ADVANCED are:
      /// If num_buffers == 1 -> buf[0] has the compressed bitstream WITH the starting code
      /// If num_buffers == 2 -> buf[0] has the NALU starting code and buf[1] has the compressed bitstream WITHOUT any starting code.
      /// If num_buffers = 3 -> It's JPEG, not supported in D3D12.
      /// num_buffers is at most 3.
   /// Mesa VDPAU frontend passes the buffers as they get passed in VdpDecoderRender without fixing any start codes except for PIPE_VIDEO_PROFILE_VC1_ADVANCED
      // In https://http.download.nvidia.com/XFree86/vdpau/doxygen/html/index.html#video_mixer_usage it's mentioned that:
      // It is recommended that applications pass solely the slice data to VDPAU; specifically that any header data structures be excluded from the portion of the bitstream passed to VDPAU. VDPAU implementations must operate correctly if non-slice data is included, at least for formats employing start codes to delimit slice data.
      // For all codecs/profiles it's highly recommended (when the codec/profile has such codes...) that the start codes are passed to VDPAU, even when not included in the bitstream the VDPAU client is parsing.
      // Let's assume we get all the start codes for VDPAU. The doc also says "VDPAU implementations must operate correctly if non-slice data is included, at least for formats employing start codes to delimit slice data"
      // if we ever get an issue with VDPAU start codes we should consider adding the code that handles this in the VDPAU layer above the gallium driver like mesa VA does.

   // To handle the multi-slice case where multiple slices mean multiple decode_bitstream calls, end_frame already takes care of this assuming numSlices = pD3D12Dec->m_numConsecutiveDecodeFrame; and parsing the start codes
   // from the combined bitstream of all decode_bitstream calls.

   // VAAPI seems to send one decode_bitstream command per slice, but we should also support the VDPAU case where the buffers have multiple buffer array entry per slice {startCode (optional), slice1, slice2, ..., startCode (optional) , sliceN}

   if(num_buffers > 2) // Assume this means multiple slices at once in a decode_bitstream call
   {
      // Based on VA frontend codebase, this never happens for video (no JPEG)
      // Based on VDPAU frontends codebase, this only happens when sending more than one slice at once in decode bitstream

      // To handle the case where VDPAU send all the slices at once in a single decode_bitstream call, let's pretend it was a series of different calls

      // group by start codes and buffers and perform calls for the number of slices so m_numConsecutiveDecodeFrame matches that number.
      D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_decode_bitstream multiple slices on same call detected for fenceValue: %d, breaking down the calls into one per slice\n", pD3D12Dec->m_fenceValue);

      size_t curBufferIdx = 0;

      // Vars to be used for the delegation calls to decode_bitstream
      unsigned call_num_buffers = 0;
      const void * const * call_buffers = nullptr;
      const unsigned *call_sizes = nullptr;

      while(curBufferIdx < num_buffers)
      {
         // Store the current buffer as the base array pointer for the delegated call, later decide if it'll be a startcode+slicedata or just slicedata call
         call_buffers = &buffers[curBufferIdx];
         call_sizes = &sizes[curBufferIdx];

         // Usually start codes are less or equal than 4 bytes
         // If the current buffer is a start code buffer, send it along with the next buffer. Otherwise, just send the current buffer.
         call_num_buffers = (sizes[curBufferIdx] <= 4) ? 2 : 1;

         // Delegate call with one or two buffers only
         d3d12_video_decode_bitstream(codec,
                           target,
                           picture,
                           call_num_buffers,
                           call_buffers,
                           call_sizes);

         curBufferIdx += call_num_buffers; // Consume from the loop the buffers sent in the last call
      }
   }
   else
   {
///
/// Handle single slice buffer path, maybe with an extra start code buffer at buffers[0].
///

      // Both the start codes being present at buffers[0] case and the multi-slice case can be handled by flattening all the buffers into a single one and passing that to HW.

      size_t totalReceivedBuffersSize = 0u; // Combined size of all sizes[]
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++)
      {
         totalReceivedBuffersSize += sizes[bufferIdx];
      }

      // Bytes of data pre-staged before this decode_frame call
      size_t preStagedDataSize = pD3D12Dec->m_stagingDecodeBitstream.size();
      
      // Extend the staging buffer size, as decode_frame can be called several times before end_frame
      pD3D12Dec->m_stagingDecodeBitstream.resize(preStagedDataSize + totalReceivedBuffersSize);
      
      // Point newSliceDataPositionDstBase to the end of the pre-staged data in m_stagingDecodeBitstream, where the new buffers will be appended
      BYTE* newSliceDataPositionDstBase = pD3D12Dec->m_stagingDecodeBitstream.data() + preStagedDataSize;

      // Append new data at the end.
      size_t dstOffset = 0u;
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++)
      {
         memcpy(newSliceDataPositionDstBase + dstOffset, buffers[bufferIdx], sizes[bufferIdx]);
         dstOffset += sizes[bufferIdx];
      }

      ///
      /// Codec header picture parameters buffers
      ///

      d3d12_store_converted_dxva_picparams_from_pipe_input (
         pD3D12Dec,
         picture
      );
      assert(pD3D12Dec->m_picParamsBuffer.size() > 0);

      // Gather information about interlace from the texture. end_frame will re-create d3d12 decoder/heap as necessary on reconfiguration
      pD3D12Dec->m_InterlaceType = target->interlaced ? D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED : D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE; 

      pD3D12Dec->m_numConsecutiveDecodeFrame++;

      D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_decode_bitstream finalized for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   }   
}

/**
 * end decoding of the current frame
 */
void d3d12_video_end_frame(struct pipe_video_codec *codec,
                  struct pipe_video_buffer *target,
                  struct pipe_picture_desc *picture)
{
   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;
   assert(pD3D12Dec);
   struct d3d12_screen* pD3D12Screen = (struct d3d12_screen*) pD3D12Dec->m_pD3D12Screen;
   assert(pD3D12Screen);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_end_frame started for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);

///
/// Prepare Slice control buffers before clearing staging buffer
///
   assert(pD3D12Dec->m_stagingDecodeBitstream.size()>0); // Make sure the staging wasn't cleared yet in end_frame
   
   size_t numSlices = pD3D12Dec->m_numConsecutiveDecodeFrame;

   std::vector<DXVA_Slice_H264_Short> pSliceControlBuffers(numSlices);
   pSliceControlBuffers.resize(numSlices);
   size_t processedBitstreamBytes = 0u;
   for (size_t sliceIdx = 0; sliceIdx < numSlices; sliceIdx++)
   {
      // From DXVA spec: All bits for the slice are located within the corresponding bitstream data buffer.
      pSliceControlBuffers[sliceIdx].wBadSliceChopping = 0u;
      bool sliceFound = GetSliceSizeAndOffset(sliceIdx, numSlices, pD3D12Dec->m_stagingDecodeBitstream, processedBitstreamBytes, pSliceControlBuffers[sliceIdx].SliceBytesInBuffer, pSliceControlBuffers[sliceIdx].BSNALunitDataLocation);
      assert(sliceFound);
      D3D12_LOG_DBG("[D3D12 Video Driver] Detected slice index %ld with size %d and offset %d for frame with fenceValue: %d\n", sliceIdx, pSliceControlBuffers[sliceIdx].SliceBytesInBuffer, pSliceControlBuffers[sliceIdx].BSNALunitDataLocation, pD3D12Dec->m_fenceValue);
      
      processedBitstreamBytes += pSliceControlBuffers[sliceIdx].SliceBytesInBuffer;
   }

   assert( sizeof(pSliceControlBuffers.data()[0]) == sizeof(DXVA_Slice_H264_Short) );
   d3d12_store_dxva_slicecontrol_in_slicecontrol_buffer(pD3D12Dec, pSliceControlBuffers.data(), pSliceControlBuffers.size() * sizeof((pSliceControlBuffers.data()[0])));
   assert(pD3D12Dec->m_SliceControlBuffer.size() > 0);

///
/// Upload m_stagingDecodeBitstream to GPU memory now that end_frame is called and clear staging buffer
///

   uint64_t sliceDataStagingBufferSize = pD3D12Dec->m_stagingDecodeBitstream.size();
   BYTE* sliceDataStagingBufferPtr = pD3D12Dec->m_stagingDecodeBitstream.data();

   // TODO: If doesn't work with >= size, fix m_D3D12ResourceCopyHelper to support that (don't try to read the whole resource size from the pData src buffer that might be smaller).

   // Reallocate if necessary to accomodate the current frame bitstream buffer in GPU memory
   // if(pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize < sliceDataStagingBufferSize)
   if(pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize != sliceDataStagingBufferSize)
   {
      if(!d3d12_create_video_staging_bitstream_buffer(pD3D12Screen, pD3D12Dec, sliceDataStagingBufferSize))
      {
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_end_frame - Failure on d3d12_create_video_staging_bitstream_buffer\n");
      }
   }   

   // Upload frame bitstream CPU data to ID3D12Resource buffer
   pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize = sliceDataStagingBufferSize; // This can be less than m_curFrameCompressedBitstreamBufferAllocatedSize. 
   assert(pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize <= pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize);
   pD3D12Dec->m_D3D12ResourceCopyHelper->UploadData(
      pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get(),
      0,
      D3D12_RESOURCE_STATE_COMMON,
      sliceDataStagingBufferPtr,
      sizeof(*sliceDataStagingBufferPtr) * sliceDataStagingBufferSize,
      sizeof(*sliceDataStagingBufferPtr) * sliceDataStagingBufferSize
   );

   // Clear CPU staging buffer now that end_frame is called and was uploaded to GPU for DecodeFrame call.
   pD3D12Dec->m_stagingDecodeBitstream.resize(0);
   
   // Reset decode_frame counter at end_frame call
   pD3D12Dec->m_numConsecutiveDecodeFrame = 0;

///
/// Proceed to record the GPU Decode commands
///

   struct d3d12_video_buffer* pD3D12VideoBuffer = (struct d3d12_video_buffer*) target;
   assert(pD3D12VideoBuffer);
   const UINT outputD3D12TextureSubresource = 0u;

#if !D3D12_DECODER_USE_STAGING_OUTPUT_TEXTURE
   ComPtr<ID3D12Resource> spOutputD3D12Texture = pD3D12VideoBuffer->m_pD3D12Resource->bo->res;
#else
   auto targetUnderlyingResDesc = d3d12_resource_resource(pD3D12VideoBuffer->m_pD3D12Resource)->GetDesc();
   bool bCreateStagingTexture = false;
   if(pD3D12Dec->m_spDecodeOutputStagingTexture == nullptr)
   {
      bCreateStagingTexture = true;
   }
   else
   {
      D3D12_RESOURCE_DESC currStagingDesc = pD3D12Dec->m_spDecodeOutputStagingTexture->GetDesc();
      bCreateStagingTexture = (memcmp(&targetUnderlyingResDesc, &currStagingDesc, sizeof(D3D12_RESOURCE_DESC)) != 0);
   }

   if(bCreateStagingTexture)
   {
      CD3DX12_RESOURCE_DESC textureCreationDesc = CD3DX12_RESOURCE_DESC::Tex2D(targetUnderlyingResDesc.Format, targetUnderlyingResDesc.Width, targetUnderlyingResDesc.Height, 1, 1);
      D3D12_HEAP_PROPERTIES textureCreationProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, pD3D12Dec->m_NodeMask, pD3D12Dec->m_NodeMask);
      VERIFY_SUCCEEDED(pD3D12Dec->m_pD3D12Screen->dev->CreateCommittedResource(
         &textureCreationProps,
         D3D12_HEAP_FLAG_NONE,
         &textureCreationDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Dec->m_spDecodeOutputStagingTexture.GetAddressOf())));   
   }
   
   ComPtr<ID3D12Resource> spOutputD3D12Texture = pD3D12Dec->m_spDecodeOutputStagingTexture;
#endif

   D3D12_RESOURCE_DESC resDesc = spOutputD3D12Texture->GetDesc();
   assert(resDesc.Format == DXGI_FORMAT_NV12);
   assert(resDesc.Format == pD3D12Dec->m_decoderHeapDesc.Format);
   assert(resDesc.Width == pD3D12Dec->m_decoderHeapDesc.DecodeWidth);
   assert(resDesc.Height == pD3D12Dec->m_decoderHeapDesc.DecodeHeight);

   // Requested conversions by caller upper layer (none for now)
   D3D12DecVideoDecodeOutputConversionArguments requestedConversionArguments = { };

   ///
   /// Record DecodeFrame operation and resource state transitions.
   ///

   // Translate input D3D12 structure
   D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS d3d12InputArguments = {};

   d3d12InputArguments.CompressedBitstream.pBuffer = pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get();
   d3d12InputArguments.CompressedBitstream.Offset = 0u;
   const uint64_t d3d12BitstreamOffsetAlignment = 128u; // specified in https://docs.microsoft.com/en-us/windows/win32/api/d3d12video/ne-d3d12video-d3d12_video_decode_tier
   assert((d3d12InputArguments.CompressedBitstream.Offset == 0) || (d3d12InputArguments.CompressedBitstream.Offset % d3d12BitstreamOffsetAlignment));
   d3d12InputArguments.CompressedBitstream.Size = pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize;

   d3d12_record_state_transition(
      pD3D12Dec->m_spDecodeCommandList,
      d3d12InputArguments.CompressedBitstream.pBuffer,
      D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_VIDEO_DECODE_READ
   );

   // Schedule reverse (back to common) transitions before command list closes for current frame
   pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
      CD3DX12_RESOURCE_BARRIER::Transition(
         d3d12InputArguments.CompressedBitstream.pBuffer,
         D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
         D3D12_RESOURCE_STATE_COMMON
      )
   );

   d3d12_decoder_prepare_for_decode_frame(
      pD3D12Dec,
      spOutputD3D12Texture.Get(),
      outputD3D12TextureSubresource,
      requestedConversionArguments
   );

   ///
   /// Set codec picture parameters CPU buffer
   ///

   d3d12InputArguments.NumFrameArguments = 1u; // Only the codec data received from the above layer with picture params
   d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = 
   {
      D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS,
      static_cast<UINT>(pD3D12Dec->m_picParamsBuffer.size()),
      pD3D12Dec->m_picParamsBuffer.data(),
   };
   
   if(pD3D12Dec->m_SliceControlBuffer.size() > 0)
   {
      d3d12InputArguments.NumFrameArguments++;
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = 
      {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL,
         static_cast<UINT>(pD3D12Dec->m_SliceControlBuffer.size()),
         pD3D12Dec->m_SliceControlBuffer.data(),
      };
   }

   if(pD3D12Dec->m_InverseQuantMatrixBuffer.size() > 0)
   {
      d3d12InputArguments.NumFrameArguments++; // Only the codec data received from the above layer with InverseQuantMatrixBuffer params
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments-1] = 
      {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX,
         static_cast<UINT>(pD3D12Dec->m_InverseQuantMatrixBuffer.size()),
         pD3D12Dec->m_InverseQuantMatrixBuffer.data(),
      };
   }

   d3d12InputArguments.ReferenceFrames.ppTexture2Ds = pD3D12Dec->m_spDPBManager->textures.data();
   d3d12InputArguments.ReferenceFrames.pSubresources = pD3D12Dec->m_spDPBManager->texturesSubresources.data();
   d3d12InputArguments.ReferenceFrames.NumTexture2Ds = static_cast<UINT>(pD3D12Dec->m_spDPBManager->Size());
   d3d12InputArguments.pHeap = pD3D12Dec->m_spVideoDecoderHeap.Get();

   // translate output D3D12 structure
   D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1 d3d12OutputArguments = {};
   d3d12OutputArguments.pOutputTexture2D = spOutputD3D12Texture.Get();
   d3d12OutputArguments.OutputSubresource = outputD3D12TextureSubresource;

   if (pD3D12Dec->m_spDPBManager->IsReferenceOnly())
   {
      d3d12OutputArguments.ConversionArguments.Enable = TRUE;

      bool needsTransitionToDecodeWrite = false;
      pD3D12Dec->m_spDPBManager->GetReferenceOnlyOutput(d3d12OutputArguments.ConversionArguments.pReferenceTexture2D, d3d12OutputArguments.ConversionArguments.ReferenceSubresource, needsTransitionToDecodeWrite);
      assert(needsTransitionToDecodeWrite);
      
      // TODO: State transitions might be trickier, with D3D12DecomposeSubresource. See conf decode tests and translation layer refmgr transition method code
      
      d3d12_record_state_transition(
         pD3D12Dec->m_spDecodeCommandList,
         d3d12OutputArguments.ConversionArguments.pReferenceTexture2D,
         D3D12_RESOURCE_STATE_COMMON,
         D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
         d3d12OutputArguments.ConversionArguments.ReferenceSubresource
      );

      // Schedule reverse (back to common) transitions before command list closes for current frame
      pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
         CD3DX12_RESOURCE_BARRIER::Transition(
            d3d12OutputArguments.ConversionArguments.pReferenceTexture2D,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON,
            d3d12OutputArguments.ConversionArguments.ReferenceSubresource
         )
      );

      const D3D12_RESOURCE_DESC &descReference = d3d12OutputArguments.ConversionArguments.pReferenceTexture2D->GetDesc();
      d3d12OutputArguments.ConversionArguments.DecodeColorSpace = 
         CDXGIColorSpaceHelper::ConvertFromLegacyColorSpace(
            !D3D12VideoFormatHelper::YUV(descReference.Format),
            D3D12VideoFormatHelper::GetBitsPerUnit(descReference.Format),
            /* StudioRGB= */ false,
            /* P709= */ true,
            /* StudioYUV= */ true
      );

      const D3D12_RESOURCE_DESC &descOutput = d3d12OutputArguments.pOutputTexture2D->GetDesc();
      d3d12OutputArguments.ConversionArguments.OutputColorSpace = 
         CDXGIColorSpaceHelper::ConvertFromLegacyColorSpace(
            !D3D12VideoFormatHelper::YUV(descOutput.Format),
            D3D12VideoFormatHelper::GetBitsPerUnit(descOutput.Format),
            /* StudioRGB= */ false,
            /* P709= */ true,
            /* StudioYUV= */ true
      );

      const D3D12_VIDEO_DECODER_HEAP_DESC& HeapDesc = pD3D12Dec->m_spVideoDecoderHeap->GetDesc();
      d3d12OutputArguments.ConversionArguments.OutputWidth = HeapDesc.DecodeWidth;
      d3d12OutputArguments.ConversionArguments.OutputHeight = HeapDesc.DecodeHeight;
   }
   else
   {
      d3d12OutputArguments.ConversionArguments.Enable = FALSE;
   }

   d3d12_record_state_transition(
      pD3D12Dec->m_spDecodeCommandList,
      spOutputD3D12Texture.Get(),
      D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE
   );

   // Schedule reverse (back to common) transitions before command list closes for current frame
   pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
      CD3DX12_RESOURCE_BARRIER::Transition(
         spOutputD3D12Texture.Get(),
         D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
         D3D12_RESOURCE_STATE_COMMON
      )
   );
   
   // Record DecodeFrame

   pD3D12Dec->m_spDecodeCommandList->DecodeFrame1(
      pD3D12Dec->m_spVideoDecoder.Get(),
      &d3d12OutputArguments,
      &d3d12InputArguments
   );

#if D3D12_DECODER_MOCK_DECODED_TEXTURE
   // Different color per Y, U, V component.
   // uint8_t YUVPixelValues[3] = { 76, 84, 255 }; // (255, 0, 0) in RGB
   uint8_t YUVPixelValues[3] = { 125, 91, 167 }; // (180, 110, 60) in RGB - brown/gold
   
   struct pipe_sampler_view **views = target->get_sampler_view_planes(target);
   const uint numPlanes = 2;// Y and UV planes
   for (size_t planeIdx = 0; planeIdx < numPlanes; planeIdx++)
   {
      unsigned box_w = align(resDesc.Width, 2);
      unsigned box_h = align(resDesc.Height, 2);
      unsigned box_x = 0 & ~1;
      unsigned box_y = 0 & ~1;
      vl_video_buffer_adjust_size(&box_w, &box_h, planeIdx,
                                  pipe_format_to_chroma_format(target->buffer_format),
                                  target->interlaced);
      vl_video_buffer_adjust_size(&box_x, &box_y, planeIdx,
                                  pipe_format_to_chroma_format(target->buffer_format),
                                  target->interlaced);
      struct pipe_box box = {box_x, box_y, 0, box_w, box_h, 1};
      struct pipe_transfer *transfer;
      void *pMappedTexture = pD3D12Dec->base.context->texture_map(
            pD3D12Dec->base.context,
            views[planeIdx]->texture,
            0,
            PIPE_MAP_WRITE,
            &box,
            &transfer);
      
      assert(pMappedTexture);

      size_t bTextureBytes = box.height * transfer->stride;

      if(planeIdx == 0)
      {
         // Just fill the Y pixels
         D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_end_frame - Uploading mock decoded pixel data %d to view plane %d - fenceValue: %d\n", YUVPixelValues[0], (uint) planeIdx, pD3D12Dec->m_fenceValue);
         memset(pMappedTexture, YUVPixelValues[0], bTextureBytes);
      }
      else if(planeIdx == 1)
      {
         D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_end_frame - Uploading mock decoded pixel data U: %d V: %d to view plane %d - fenceValue: %d\n", YUVPixelValues[1], YUVPixelValues[2], (uint) planeIdx, pD3D12Dec->m_fenceValue);
         uint8_t* pDst = (uint8_t*) pMappedTexture;
         for (size_t pixRow = 0; pixRow < box.height; pixRow++)
         {
            // Interleave U, V values. From MSDN: When the combined U-V array is addressed as an array of little-endian WORD values, the LSBs contain the U values, and the MSBs contain the V values.
            // box.width counts the number of combined UV components in WORDs.
            uint16_t* rowPtr = (uint16_t*) pDst;
            for (size_t pixInRow = 0; pixInRow < box.width; pixInRow++)
            {
               *rowPtr = (static_cast<UINT16>(YUVPixelValues[2]) << 8) | static_cast<UINT16>(YUVPixelValues[1]);
               
               assert(sizeof(*rowPtr) == sizeof(uint16_t)); // to make sure the stride increment is in WORDs as UV packed values.
               rowPtr++;
            }
            
            assert(sizeof(*pDst) == sizeof(uint8_t)); // to make sure the stride increment is in bytes as transfer->stride.
            pDst += transfer->stride;
         }         
      }

      pipe_texture_unmap(pD3D12Dec->base.context, transfer);
   }
#endif

   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_end_frame finalized for fenceValue: %d\n", pD3D12Dec->m_fenceValue);

   // Flush work to the GPU
   d3d12_video_flush(codec);


#if !D3D12_DECODER_MOCK_DECODED_TEXTURE && D3D12_DECODER_USE_STAGING_OUTPUT_TEXTURE
   struct pipe_sampler_view **views = target->get_sampler_view_planes(target);
   const uint numPlanes = 2;// Y and UV planes
   for (size_t planeIdx = 0; planeIdx < numPlanes; planeIdx++)
   {
      const D3D12_RESOURCE_DESC stagingDesc = spOutputD3D12Texture->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
      UINT64 srcTextureTotalBytes = 0;
      pD3D12Dec->m_pD3D12Screen->dev->GetCopyableFootprints(&stagingDesc, planeIdx, 1, 0, &layout, nullptr, nullptr, &srcTextureTotalBytes);
      std::vector<uint8_t> pSrc(srcTextureTotalBytes);

      // Uncomment below if desired to mock an all violet decoded texture
      // std::vector<uint8_t> pTmp(srcTextureTotalBytes);
      // memset(pTmp.data(), 255u/*if on all YUV is RGB violet*/, pTmp.size());
      // pD3D12Dec->m_D3D12ResourceCopyHelper->UploadData(
      //    spOutputD3D12Texture.Get(),
      //    planeIdx,
      //    D3D12_RESOURCE_STATE_COMMON,
      //    pTmp.data(),
      //    layout.Footprint.RowPitch,
      //    layout.Footprint.RowPitch
      // );

      pD3D12Dec->m_D3D12ResourceCopyHelper->ReadbackData(
         pSrc.data(),
         layout.Footprint.RowPitch,
         layout.Footprint.RowPitch * stagingDesc.Height,
         spOutputD3D12Texture.Get(),
         planeIdx,
         D3D12_RESOURCE_STATE_COMMON
      );

      if(planeIdx == 0)
      {
         pD3D12Dec->m_DecodedTexturePixelsY.resize(srcTextureTotalBytes);
         memcpy(pD3D12Dec->m_DecodedTexturePixelsY.data(), pSrc.data(), pSrc.size());
         
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_pDecodedTexturePixelsY = pSrc.data();
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_decodedTexturePixelsYSize = pSrc.size();
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_YStride = layout.Footprint.RowPitch;

      }
      else if(planeIdx==1)
      {
         pD3D12Dec->m_DecodedTexturePixelsUV.resize(srcTextureTotalBytes);
         memcpy(pD3D12Dec->m_DecodedTexturePixelsUV.data(), pSrc.data(), pSrc.size());
         
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_pDecodedTexturePixelsUV = pSrc.data();
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_decodedTexturePixelsUVSize = pSrc.size();
         pD3D12Dec->m_DecodedPlanesBufferDesc.m_UVStride = layout.Footprint.RowPitch;

      }
      target->associated_data = &pD3D12Dec->m_DecodedPlanesBufferDesc;

      // At this point pSrc has the srcTextureTotalBytes of raw pixel data of spOutputD3D12Texture/subresource/planeIdx

      // Upload pSrc into target using texture_map
      unsigned box_w = align(stagingDesc.Width, 2);
      unsigned box_h = align(stagingDesc.Height, 2);
      unsigned box_x = 0 & ~1;
      unsigned box_y = 0 & ~1;
      vl_video_buffer_adjust_size(&box_w, &box_h, planeIdx,
                                  pipe_format_to_chroma_format(target->buffer_format),
                                  target->interlaced);
      vl_video_buffer_adjust_size(&box_x, &box_y, planeIdx,
                                  pipe_format_to_chroma_format(target->buffer_format),
                                  target->interlaced);
      struct pipe_box box = {box_x, box_y, 0, box_w, box_h, 1};
      struct pipe_transfer *transfer;
      void *pMappedTexture = pD3D12Dec->base.context->texture_map(
            pD3D12Dec->base.context,
            views[planeIdx]->texture,
            0,
            PIPE_MAP_WRITE,
            &box,
            &transfer);
      
      assert(pMappedTexture);

      size_t bTextureBytes = box.height * transfer->stride;
      uint8_t* pDstData = (uint8_t*) pMappedTexture;
      for (size_t pixRow = 0; pixRow < box.height; pixRow++)
      {
         if(planeIdx == 0)
         {
            // Copy box.width Y pixels from src but increment pDstData stride
            memcpy(pDstData, pSrc.data(), box.width);
         }
         else if(planeIdx == 1)
         {
            // box.width counts the number of combined UV components in WORDs.So we gotta copy 2*box.width 8-bit components but increment pDstData stride
            memcpy(pDstData, pSrc.data(), 2*box.width);
         }
         assert(sizeof(*pDstData) == sizeof(uint8_t)); // to make sure the stride increment is in bytes as transfer->stride.
         pDstData += transfer->stride;
      }

      pipe_texture_unmap(pD3D12Dec->base.context, transfer);
   }
#endif
}

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void d3d12_video_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;
   assert(pD3D12Dec);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);   
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_flush started. Will flush video queue work and CPU wait on fenceValue: %d\n", pD3D12Dec->m_fenceValue);

   HRESULT hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
   if(hr != S_OK)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_flush - D3D12Device was removed BEFORE commandlist execution.\n");
   }

   // Close and execute command list and wait for idle on CPU blocking 
   // this method before resetting list and allocator for next submission.

   d3d12_record_state_transitions
   (
      pD3D12Dec->m_spDecodeCommandList,
      pD3D12Dec->m_transitionsBeforeCloseCmdList
   );
   pD3D12Dec->m_transitionsBeforeCloseCmdList.clear();

   hr = pD3D12Dec->m_spDecodeCommandList->Close();
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_flush - Can't close command list with HR %x\n", hr);
   }

#if D3D12_DECODER_MOCK_DECODED_TEXTURE
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_flush - Mocking decoded texture for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
#else
   ID3D12CommandList *ppCommandLists[1] = { pD3D12Dec->m_spDecodeCommandList.Get() };
   pD3D12Dec->m_spDecodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
   pD3D12Dec->m_spDecodeCommandQueue->Signal(pD3D12Dec->m_spFence.Get(), pD3D12Dec->m_fenceValue);
   pD3D12Dec->m_spFence->SetEventOnCompletion(pD3D12Dec->m_fenceValue, nullptr);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_flush - ExecuteCommandLists finished on signal with fenceValue: %d\n", pD3D12Dec->m_fenceValue);
#endif

   hr = pD3D12Dec->m_spCommandAllocator->Reset();
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_flush - resetting ID3D12CommandAllocator failed with HR %x\n", hr);
   }

   hr = pD3D12Dec->m_spDecodeCommandList->Reset(pD3D12Dec->m_spCommandAllocator.Get());
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n", hr);
   }

   // Validate device was not removed
   hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
   if(hr != S_OK)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_flush - D3D12Device was removed AFTER commandlist execution, but wasn't before.\n");
   }
   
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_flush - GPU signaled execution finalized for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   
   pD3D12Dec->m_fenceValue++;
}

bool d3d12_create_video_command_objects(const struct d3d12_screen* pD3D12Screen, struct d3d12_video_decoder* pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE };
   HRESULT hr = pD3D12Screen->dev->CreateCommandQueue(
      &commandQueueDesc,
      IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandQueue.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_command_objects - Call to CreateCommandQueue failed with HR %x\n", hr);
      return false;
   }

   hr = pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Dec->m_spFence));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_command_objects - Call to CreateFence failed with HR %x\n", hr);
      return false;
   }
   
   hr = pD3D12Screen->dev->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
      IID_PPV_ARGS(pD3D12Dec->m_spCommandAllocator.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_command_objects - Call to CreateCommandAllocator failed with HR %x\n", hr);
      return false;
   }

   hr = pD3D12Screen->dev->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
      pD3D12Dec->m_spCommandAllocator.Get(),
      nullptr,
      IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandList.GetAddressOf()));

   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_command_objects - Call to CreateCommandList failed with HR %x\n", hr);
      return false;
   }

   D3D12_COMMAND_QUEUE_DESC copyQueueDesc = { D3D12_COMMAND_LIST_TYPE_COPY };
   hr = pD3D12Screen->dev->CreateCommandQueue(
      &copyQueueDesc,
      IID_PPV_ARGS(pD3D12Dec->m_spCopyQueue.GetAddressOf()));

   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_command_objects - Call to CreateCommandQueue failed with HR %x\n", hr);
      return false;
   }

   pD3D12Dec->m_D3D12ResourceCopyHelper.reset(new D3D12ResourceCopyHelper(pD3D12Dec->m_spCopyQueue.Get()));

   return true;
}

bool d3d12_check_caps_and_create_video_decoder_and_heap(const struct d3d12_screen* pD3D12Screen, struct d3d12_video_decoder* pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   pD3D12Dec->m_decoderDesc = {};
   pD3D12Dec->m_decoderHeapDesc = {};
   
   D3D12_VIDEO_DECODE_CONFIGURATION decodeConfiguration = 
   { 
      pD3D12Dec->m_d3d12DecProfile, 
      D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE,
      pD3D12Dec->m_InterlaceType,
   };

   D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport = {};
   decodeSupport.NodeIndex = pD3D12Dec->m_NodeIndex;
   decodeSupport.Configuration = decodeConfiguration;
   decodeSupport.Width = pD3D12Dec->base.width;
   decodeSupport.Height = pD3D12Dec->base.height;
   decodeSupport.DecodeFormat = pD3D12Dec->m_decodeFormat;
   // no info from above layer on framerate/bitrate
   decodeSupport.FrameRate.Numerator = 0;
   decodeSupport.FrameRate.Denominator = 0;
   decodeSupport.BitRate = 0;

   HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &decodeSupport, sizeof(decodeSupport));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_check_caps_and_create_video_decoder_and_heap - CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   }

   if (!(decodeSupport.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_check_caps_and_create_video_decoder_and_heap - D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED was false when checking caps \n");
      return false;
   }

   pD3D12Dec->m_configurationFlags = decodeSupport.ConfigurationFlags;
   pD3D12Dec->m_tier = decodeSupport.DecodeTier;

   if (d3d12_video_dec_supports_aot_dpb(decodeSupport, pD3D12Dec->m_d3d12DecProfileType))
   {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ARRAY_OF_TEXTURES;
   }

   if (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED)
   {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ALIGNMENT_HEIGHT;
   }

   if(decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED)
   {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED;
   }

   pD3D12Dec->m_decoderDesc.NodeMask = pD3D12Dec->m_NodeMask;
   pD3D12Dec->m_decoderDesc.Configuration = decodeConfiguration;
   
   hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&pD3D12Dec->m_decoderDesc, IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_check_caps_and_create_video_decoder_and_heap - CreateVideoDecoder failed with HR %x\n", hr);
      return false;
   }

   pD3D12Dec->m_decoderHeapDesc.NodeMask = pD3D12Dec->m_NodeMask;
   pD3D12Dec->m_decoderHeapDesc.Configuration = decodeConfiguration;
   pD3D12Dec->m_decoderHeapDesc.DecodeWidth = pD3D12Dec->base.width;
   pD3D12Dec->m_decoderHeapDesc.DecodeHeight = pD3D12Dec->base.height;
   pD3D12Dec->m_decoderHeapDesc.Format = pD3D12Dec->m_decodeFormat;
   pD3D12Dec->m_decoderHeapDesc.MaxDecodePictureBufferCount = pD3D12Dec->m_MaxReferencePicsWithCurrentPic;

   hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoderHeap(&pD3D12Dec->m_decoderHeapDesc, IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoderHeap.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_check_caps_and_create_video_decoder_and_heap - CreateVideoDecoderHeap failed with HR %x\n", hr);
      return false;
   }

   return true;
}

bool d3d12_create_video_dpbmanagers(const struct d3d12_screen* pD3D12Screen, struct d3d12_video_decoder* pD3D12Dec)
{   
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   pD3D12Dec->m_spDPBManager.reset(new D3D12VidDecReferenceDataManager(pD3D12Screen, pD3D12Dec->m_d3d12DecProfileType, pD3D12Dec->m_NodeMask));

   return true;
}

bool d3d12_create_video_state_buffers(const struct d3d12_screen* pD3D12Screen, struct d3d12_video_decoder* pD3D12Dec)
{   
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   if(!d3d12_create_video_staging_bitstream_buffer(pD3D12Screen, pD3D12Dec, pD3D12Dec->m_InitialCompBitstreamGPUBufferSize))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_state_buffers - Failure on d3d12_create_video_staging_bitstream_buffer\n");
      return false;
   }

   return true;
}

bool d3d12_create_video_staging_bitstream_buffer(const struct d3d12_screen* pD3D12Screen, struct d3d12_video_decoder* pD3D12Dec, UINT64 bufSize)
{   
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   if(pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get() != nullptr)
   {
      pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Reset();
   }

   auto descHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, pD3D12Dec->m_NodeMask, pD3D12Dec->m_NodeMask);
   auto descResource = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
   HRESULT hr = pD3D12Screen->dev->CreateCommittedResource(
            &descHeap,
            D3D12_HEAP_FLAG_NONE,
            &descResource,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pD3D12Dec->m_curFrameCompressedBitstreamBuffer.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_staging_bitstream_buffer - CreateCommittedResource failed with HR %x\n", hr);
      return false;
   }

   pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize = bufSize;
   return true;
}

void d3d12_decoder_prepare_for_decode_frame(
   struct d3d12_video_decoder *pD3D12Dec,
   ID3D12Resource* pTexture2D,
   UINT subresourceIndex,
   const D3D12DecVideoDecodeOutputConversionArguments& conversionArgs
)
{
   d3d12_decoder_release_unused_references(pD3D12Dec);
   d3d12_decoder_manage_resolution_change(pD3D12Dec, conversionArgs, pTexture2D, subresourceIndex);

   switch (pD3D12Dec->m_d3d12DecProfileType)
   {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         d3d12_decoder_prepare_for_decode_frame_h264(pD3D12Dec, pTexture2D, subresourceIndex);   
      } break;

      default:
         assert(0);
         break;
   }
}

void d3d12_decoder_manage_resolution_change(
   struct d3d12_video_decoder *pD3D12Dec,
   const D3D12DecVideoDecodeOutputConversionArguments& conversionArguments,
   ID3D12Resource* pOutputResource,
   uint outputSubesource)
{
   UINT width;
   UINT height;
   UINT16 maxDPB;
   d3d12_decoder_get_frame_info(pD3D12Dec, &width, &height, &maxDPB);

   D3D12_RESOURCE_DESC outputResourceDesc = pOutputResource->GetDesc();
   VIDEO_DECODE_PROFILE_BIT_DEPTH resourceBitDepth = d3d12_dec_get_format_bitdepth(outputResourceDesc.Format);

   if (pD3D12Dec->m_decodeFormat != outputResourceDesc.Format)
   {
      // Copy current pD3D12Dec->m_decoderDesc, modify decodeprofile and re-create decoder.
      D3D12_VIDEO_DECODER_DESC decoderDesc = pD3D12Dec->m_decoderDesc;
      decoderDesc.Configuration.DecodeProfile = d3d12_decoder_resolve_profile(pD3D12Dec->m_d3d12DecProfileType, resourceBitDepth);
      pD3D12Dec->m_spVideoDecoder.Reset();
      HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&decoderDesc, IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
      if(FAILED(hr))
      {
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_decoder_manage_resolution_change - CreateVideoDecoder failed with HR %x\n", hr);
      }
      // Update state after CreateVideoDecoder succeeds only.
      pD3D12Dec->m_decoderDesc = decoderDesc;
   }

   if ( 
         !pD3D12Dec->m_DPBManagerInitialized
      || !pD3D12Dec->m_spVideoDecoderHeap
      || pD3D12Dec->m_decodeFormat != outputResourceDesc.Format
      || pD3D12Dec->m_decoderHeapDesc.DecodeWidth != width
      || pD3D12Dec->m_decoderHeapDesc.DecodeHeight != height
      || pD3D12Dec->m_decoderHeapDesc.MaxDecodePictureBufferCount < maxDPB)
   {
      
      UINT16 referenceCount = maxDPB;
      bool fArrayOfTexture = false;
      bool fReferenceOnly = (pD3D12Dec->m_ConfigDecoderSpecificFlags & D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED) != 0;

      D3D12ReferenceOnlyDesc* pReferenceOnlyDesc = nullptr;
      D3D12ReferenceOnlyDesc referenceOnlyDesc;
      referenceOnlyDesc.Width = outputResourceDesc.Width;
      referenceOnlyDesc.Height = outputResourceDesc.Height;
      referenceOnlyDesc.Format = outputResourceDesc.Format;

      if (conversionArguments.Enable)
      {
            // Decode output conversion is on, create a DPB only array to hold the references. 
            // All indices are re-mapped in host decoder to address just the size of the DPB array (given by ReferenceFrameCount).
            referenceCount = (UINT16)conversionArguments.ReferenceFrameCount;

            referenceOnlyDesc.Width = conversionArguments.ReferenceInfo.Width;
            referenceOnlyDesc.Height = conversionArguments.ReferenceInfo.Height;
            referenceOnlyDesc.Format = conversionArguments.ReferenceInfo.Format.Format;
            pReferenceOnlyDesc = &referenceOnlyDesc;
            
      }
      else if (fReferenceOnly)
      {
            pReferenceOnlyDesc = &referenceOnlyDesc;
      }
      
      if (outputResourceDesc.DepthOrArraySize != 1)
      {
            // When DepthOrArraySize is not 1 Enable Texture Array Mode.  This selection
            // is made regardless of ConfigDecoderSpecific during decode creation.
            // The reference indices are in a range of zero to the ArraySize and refer 
            // directly to array subresources.
            referenceCount = outputResourceDesc.DepthOrArraySize;
      }
      else
      {
            // A DepthOrArraySize of 1 indicates that Array of Texture Mode is enabled.
            // The reference indices are not in the range of 0 to MaxDPB, but instead 
            // are in a range determined by the caller that the driver doesn't appear to have
            // a way of knowing.

            assert(pD3D12Dec->m_tier >= D3D12_VIDEO_DECODE_TIER_2 || fReferenceOnly);
            fArrayOfTexture = pD3D12Dec->m_tier >= D3D12_VIDEO_DECODE_TIER_2;
      }

      pD3D12Dec->m_spDPBManager->Resize(referenceCount, pReferenceOnlyDesc, fArrayOfTexture);
      pD3D12Dec->m_DPBManagerInitialized = true;

      // Copy current pD3D12Dec->m_decoderDesc and update config values acconrdingly
      D3D12_VIDEO_DECODER_HEAP_DESC decoderHeapDesc = pD3D12Dec->m_decoderHeapDesc;
      decoderHeapDesc.Configuration.DecodeProfile = d3d12_decoder_resolve_profile(pD3D12Dec->m_d3d12DecProfileType, resourceBitDepth);
      decoderHeapDesc.DecodeWidth = width;
      decoderHeapDesc.DecodeHeight = height;
      decoderHeapDesc.Format = outputResourceDesc.Format;
      decoderHeapDesc.MaxDecodePictureBufferCount = maxDPB;
      pD3D12Dec->m_spVideoDecoderHeap.Reset();
      HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoderHeap(&decoderHeapDesc, IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoderHeap.GetAddressOf()));
      if(FAILED(hr))
      {
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_decoder_manage_resolution_change - CreateVideoDecoderHeap failed with HR %x\n", hr);
      }
      // Update state after CreateVideoDecoderHeap succeeds only.
      pD3D12Dec->m_decoderHeapDesc = decoderHeapDesc;
   }

   pD3D12Dec->m_decodeFormat = outputResourceDesc.Format;
}

void d3d12_decoder_release_unused_references(struct d3d12_video_decoder *pD3D12Dec)
{
   // Method overview
   // 1. Clear the following pD3D12Dec->m_spDPBManager descriptors: textures, textureSubresources and decoder heap by calling pD3D12Dec->m_spDPBManager->ResetReferenceFramesInformation()        
   // 2. Codec specific strategy in switch statement regarding reference frames eviction policy
   // 3. Call pD3D12Dec->m_spDPBManager->ReleaseUnusedReferences(); at the end of this method. Any references (and texture allocations associated) that were left not marked as used in pD3D12Dec->m_spDPBManager by step (2) are lost.
   
   pD3D12Dec->m_spDPBManager->ResetReferenceFramesInformation();

   switch (pD3D12Dec->m_d3d12DecProfileType)
   {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         d3d12_decoder_release_unused_references_h264(pD3D12Dec);
      }
      break;

      default:
         assert(0);
         break;
   }

   // Releases the underlying reference picture texture objects of all references that were not marked as used in this method.
   pD3D12Dec->m_spDPBManager->ReleaseUnusedReferences();
}

void d3d12_decoder_get_frame_info(struct d3d12_video_decoder *pD3D12Dec, UINT *pWidth, UINT *pHeight, UINT16 *pMaxDPB)
{
   *pWidth = 0;
   *pHeight = 0;
   *pMaxDPB = 0;

   switch (pD3D12Dec->m_d3d12DecProfileType)
   {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {         
         d3d12_decoder_get_frame_info_h264(pD3D12Dec, pWidth, pHeight, pMaxDPB);
      }
      break;

   default:
      assert(0);
      break;
   }

   if (pD3D12Dec->m_ConfigDecoderSpecificFlags & D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ALIGNMENT_HEIGHT)
   {
      const UINT AlignmentMask = 31;
      *pHeight = (*pHeight + AlignmentMask) & ~AlignmentMask;
   }
}

///
/// Returns the number of bytes starting from [buf.data() + buffsetOffset] where the _targetCode_ is found
/// Returns -1 if start code not found
///
int GetNextStartCodeOffset(D3D12DecoderByteBuffer &buf, unsigned int bufferOffset, unsigned int targetCode, unsigned int targetCodeBitSize, unsigned int numBitsToSearchIntoBuffer)
{
   struct vl_vlc vlc = {0};

   // Shorten the buffer to be [buffetOffset, endOfBuf)
   unsigned int bufSize = buf.size() - bufferOffset;
   uint8_t* bufPtr = buf.data();
   bufPtr += bufferOffset;

   /* search the first numBitsToSearchIntoBuffer bytes for a startcode */
   vl_vlc_init(&vlc, 1, (const void * const*) &bufPtr, &bufSize);
   for (uint i = 0; i < numBitsToSearchIntoBuffer && vl_vlc_bits_left(&vlc) >= targetCodeBitSize; ++i) {
      if (vl_vlc_peekbits(&vlc, targetCodeBitSize) == targetCode)
         return i;
      vl_vlc_eatbits(&vlc, 8); // Stride is 8 bits = 1 byte
      vl_vlc_fillbits(&vlc);
   }

   return -1;
}

bool GetSliceSizeAndOffset(size_t sliceIdx, size_t numSlices, D3D12DecoderByteBuffer &buf, unsigned int bufferOffset, UINT& outSliceSize, UINT& outSliceOffset)
{
   if(sliceIdx >= numSlices)
   {
      return false;
   }

   if(sliceIdx == (numSlices - 1)) // If this is the last slice on the bitstream
   {
      uint numBitsToSearchIntoBuffer = buf.size() - bufferOffset; // Search the rest of the full frame buffer after the offset
      int currentSlicePosition = GetNextStartCodeOffset(buf, bufferOffset, DXVA_H264_START_CODE, DXVA_H264_START_CODE_LEN_BITS, numBitsToSearchIntoBuffer);
      assert(currentSlicePosition >= 0);

      // Save the offset until the next slice in the output param
      outSliceOffset = currentSlicePosition + bufferOffset;
      
      // As there is not another slice after this one, the size will be the difference between the bitsteam total size and the offset current slice

      outSliceSize = buf.size() - outSliceOffset;
   }   
   else // If it's not the last slice on the bitstream
   {
      uint numBitsToSearchIntoBuffer = buf.size() - bufferOffset; // Search the rest of the full frame buffer after the offset
      int currentSlicePosition = GetNextStartCodeOffset(buf, bufferOffset, DXVA_H264_START_CODE, DXVA_H264_START_CODE_LEN_BITS, numBitsToSearchIntoBuffer);
      assert(currentSlicePosition >= 0); 

      // Save the offset until the next slice in the output param
      outSliceOffset = currentSlicePosition + bufferOffset;
      
      // As there's another slice after this one, look for it and calculate the size based on the next one's offset.

      // Skip current start code, to get the slice after this, to calculate its size
      bufferOffset += DXVA_H264_START_CODE_LEN_BITS;

      int nextSlicePosition = DXVA_H264_START_CODE_LEN_BITS + GetNextStartCodeOffset(buf, bufferOffset, DXVA_H264_START_CODE, DXVA_H264_START_CODE_LEN_BITS, numBitsToSearchIntoBuffer);
      assert(nextSlicePosition >= 0); // if currentSlicePosition was the last slice, this might fail

      outSliceSize = nextSlicePosition - currentSlicePosition;
   }
   return true;
}

void d3d12_store_converted_dxva_picparams_from_pipe_input (
    struct d3d12_video_decoder *codec, // input argument, current decoder
    struct pipe_picture_desc *picture // input argument, base structure of pipe_XXX_picture_desc where XXX is the codec name
)
{
   assert(picture);
   assert(codec);
   struct d3d12_video_decoder* pD3D12Dec = (struct d3d12_video_decoder*) codec;

   D3D12_VIDEO_DECODE_PROFILE_TYPE profileType = d3d12_convert_pipe_video_profile_to_profile_type(codec->base.profile);
   switch (profileType)
   {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         size_t dxvaPicParamsBufferSize = sizeof(DXVA_PicParams_H264);
         pipe_h264_picture_desc* pPicControlH264 = (pipe_h264_picture_desc*) picture;
         DXVA_PicParams_H264 dxvaPicParamsH264 = d3d12_dec_dxva_picparams_from_pipe_picparams_h264(pD3D12Dec->m_fenceValue, codec->base.profile, codec->m_decoderHeapDesc.DecodeWidth, codec->m_decoderHeapDesc.DecodeHeight, pPicControlH264);
         d3d12_store_dxva_picparams_in_picparams_buffer(codec, &dxvaPicParamsH264, dxvaPicParamsBufferSize);

         size_t dxvaQMatrixBufferSize = sizeof(DXVA_Qmatrix_H264);
         DXVA_Qmatrix_H264 dxvaQmatrixH264 = { };
         bool seq_scaling_matrix_present_flag = false;
         d3d12_dec_dxva_qmatrix_from_pipe_picparams_h264((pipe_h264_picture_desc*) picture, dxvaQmatrixH264, seq_scaling_matrix_present_flag);
         if(seq_scaling_matrix_present_flag)
         {
            d3d12_store_dxva_qmatrix_in_qmatrix_buffer(codec, &dxvaQmatrixH264, dxvaQMatrixBufferSize);
         }
         else
         {
            codec->m_InverseQuantMatrixBuffer.resize(0); //  m_InverseQuantMatrixBuffer.size() == 0 means no quantization matrix buffer is set for current frame
         }
      }
      break;
      default:
         assert(0);
         break;
   }
}

void d3d12_store_dxva_slicecontrol_in_slicecontrol_buffer(struct d3d12_video_decoder *pD3D12Dec, void* pDXVAStruct, UINT64 DXVAStructSize)
{
   assert((DXVAStructSize % sizeof(DXVA_Slice_H264_Short)) == 0);
   if (pD3D12Dec->m_SliceControlBuffer.capacity() < DXVAStructSize)
   {
      pD3D12Dec->m_SliceControlBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_SliceControlBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_SliceControlBuffer.data(), pDXVAStruct, DXVAStructSize);
}

void d3d12_store_dxva_qmatrix_in_qmatrix_buffer(struct d3d12_video_decoder *pD3D12Dec, void* pDXVAStruct, UINT64 DXVAStructSize)
{
   if (pD3D12Dec->m_InverseQuantMatrixBuffer.capacity() < DXVAStructSize)
   {   
      pD3D12Dec->m_InverseQuantMatrixBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_InverseQuantMatrixBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_InverseQuantMatrixBuffer.data(), pDXVAStruct, DXVAStructSize);
}

void d3d12_store_dxva_picparams_in_picparams_buffer(struct d3d12_video_decoder *pD3D12Dec, void* pDXVAStruct, UINT64 DXVAStructSize)
{
   if (pD3D12Dec->m_picParamsBuffer.capacity() < DXVAStructSize)
   {   
      pD3D12Dec->m_picParamsBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_picParamsBuffer.resize(DXVAStructSize);   
   memcpy(pD3D12Dec->m_picParamsBuffer.data(), pDXVAStruct, DXVAStructSize);
}

bool d3d12_video_dec_supports_aot_dpb(D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport, D3D12_VIDEO_DECODE_PROFILE_TYPE profileType)
{
    bool supportedProfile = false;
    switch (profileType)
    {
        case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
            supportedProfile = true;
            break;
        default:
            supportedProfile = false;
            break;
    }

    return (decodeSupport.DecodeTier >= D3D12_VIDEO_DECODE_TIER_2 || (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED) != 0)
        && supportedProfile;
}

DXGI_FORMAT d3d12_convert_pipe_video_profile_to_dxgi_format(enum pipe_video_profile profile)
{
    switch (profile)
    {
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
            return DXGI_FORMAT_NV12;
         case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
            assert(0); // Unusupported for now.
            return DXGI_FORMAT_P010;
        default:
            assert(0);
            return DXGI_FORMAT_UNKNOWN;
    }
}

D3D12_VIDEO_DECODE_PROFILE_TYPE d3d12_convert_pipe_video_profile_to_profile_type(enum pipe_video_profile profile)
{
    switch (profile)
    {
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
            return D3D12_VIDEO_DECODE_PROFILE_TYPE_H264;
        default:
            assert(0);
            return D3D12_VIDEO_DECODE_PROFILE_TYPE_NONE;
   }
}

GUID d3d12_convert_pipe_video_profile_to_d3d12_video_decode_profile(enum pipe_video_profile profile)
{
    switch (profile)
    {
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
        case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
            return D3D12_VIDEO_DECODE_PROFILE_H264;
        default:
            assert(0);
            return {};
   }
}

GUID d3d12_decoder_resolve_profile(D3D12_VIDEO_DECODE_PROFILE_TYPE profileType, UINT resourceBitDepth)
{
    switch (profileType)
    {
        case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
            return D3D12_VIDEO_DECODE_PROFILE_H264;
            break;
        default:
            assert(0);
            return { };
            break;
    }
}

VIDEO_DECODE_PROFILE_BIT_DEPTH d3d12_dec_get_format_bitdepth(DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_YUY2:
        case DXGI_FORMAT_AYUV:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_420_OPAQUE:
            return VIDEO_DECODE_PROFILE_BIT_DEPTH_8_BIT;

        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_Y410:
        case DXGI_FORMAT_Y210:
            return VIDEO_DECODE_PROFILE_BIT_DEPTH_10_BIT;

        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_Y416:
        case DXGI_FORMAT_Y216:
            return VIDEO_DECODE_PROFILE_BIT_DEPTH_16_BIT;
    }

    assert(false);
    return VIDEO_DECODE_PROFILE_BIT_DEPTH_NONE;
}

#pragma GCC diagnostic pop