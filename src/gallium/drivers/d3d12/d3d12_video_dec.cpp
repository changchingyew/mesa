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
#include "d3d12_video_dec_h264.h"
#include "d3d12_state_transition_helper.h"
#include "d3d12_video_buffer.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "util/vl_vlc.h"

struct pipe_video_codec *
d3d12_video_create_decoder(struct pipe_context *context, const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_decoder
   ///


   // Not using new doesn't call ctor and the initializations in the class declaration are lost
   struct d3d12_video_decoder *pD3D12Dec = new d3d12_video_decoder;

   pD3D12Dec->base     = *codec;
   pD3D12Dec->m_screen = context->screen;

   pD3D12Dec->base.context = context;
   pD3D12Dec->base.width   = codec->width;
   pD3D12Dec->base.height  = codec->height;
   // Only fill methods that are supported by the d3d12 decoder, leaving null the rest (ie. encode_* / decode_macroblock
   // / get_feedback for encode)
   pD3D12Dec->base.destroy          = d3d12_video_decoder_destroy;
   pD3D12Dec->base.begin_frame      = d3d12_video_decoder_begin_frame;
   pD3D12Dec->base.decode_bitstream = d3d12_video_decoder_decode_bitstream;
   pD3D12Dec->base.end_frame        = d3d12_video_decoder_end_frame;
   pD3D12Dec->base.flush            = d3d12_video_decoder_flush;

   pD3D12Dec->m_decodeFormat = d3d12_convert_pipe_video_profile_to_dxgi_format(codec->profile);
   pD3D12Dec->m_d3d12DecProfileType = d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(codec->profile);
   pD3D12Dec->m_d3d12DecProfile     = d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(codec->profile);

   ///
   /// Try initializing D3D12 Video device and check for device caps
   ///

   struct d3d12_context *pD3D12Ctx = (struct d3d12_context *) context;
   pD3D12Dec->m_pD3D12Screen       = d3d12_screen(pD3D12Ctx->base.screen);

   ///
   /// Create decode objects
   ///

   if (FAILED(pD3D12Dec->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(pD3D12Dec->m_spD3D12VideoDevice.GetAddressOf())))) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_create_decoder - D3D12 Device has no Video support\n");
      goto failed;
   }

   if (!d3d12_video_decoder_check_caps_and_create_decoder(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_create_decoder - Failure on "
                      "d3d12_video_decoder_check_caps_and_create_decoder\n");
      goto failed;
   }

   if (!d3d12_video_decoder_create_command_objects(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      D3D12_LOG_ERROR(
         "[d3d12_video_decoder] d3d12_video_create_decoder - Failure on d3d12_video_decoder_create_command_objects\n");
      goto failed;
   }

   if (!d3d12_video_decoder_create_video_state_buffers(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_create_decoder - Failure on "
                      "d3d12_video_decoder_create_video_state_buffers\n");
      goto failed;
   }

   pD3D12Dec->m_decodeFormatInfo = { pD3D12Dec->m_decodeFormat };
   VERIFY_SUCCEEDED(pD3D12Dec->m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                                                                        &pD3D12Dec->m_decodeFormatInfo,
                                                                        sizeof(pD3D12Dec->m_decodeFormatInfo)));

   return &pD3D12Dec->base;

failed:
   if (pD3D12Dec != nullptr) {
      d3d12_video_decoder_destroy((struct pipe_video_codec *) pD3D12Dec);
   }

   return nullptr;
}

/**
 * Destroys a d3d12_video_decoder
 * Call destroy_XX for applicable XX nested member types before deallocating
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of
 * cleanup from failure on the creation method
 */
void
d3d12_video_decoder_destroy(struct pipe_video_codec *codec)
{
   if (codec == nullptr) {
      return;
   }

   d3d12_video_decoder_flush(codec);   // Flush pending work before destroying.

   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;

   //
   // Destroys a decoder
   // Call destroy_XX for applicable XX nested member types before deallocating
   // Destroy methods should check != nullptr on their input target argument as this method can be called as part of
   // cleanup from failure on the creation method
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
void
d3d12_video_decoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on decoder creation, re-config (if any) happens in
   // d3d12_video_decoder_decode_bitstream
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_begin_frame started for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);

   if (pD3D12Dec->m_numNestedBeginFrame > 0) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] Nested d3d12_video_decoder_begin_frame calls are not supported. Call "
                      "d3d12_video_decoder_end_frame to finalize current frame before calling "
                      "d3d12_video_decoder_begin_frame again.\n");
   }

   pD3D12Dec->m_numNestedBeginFrame++;

   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_begin_frame finalized for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
}

/**
 * decode a bitstream
 */
void
d3d12_video_decoder_decode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *target,
                                     struct pipe_picture_desc *picture,
                                     unsigned                  num_buffers,
                                     const void *const *       buffers,
                                     const unsigned *          sizes)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream started for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   assert(pD3D12Dec->m_pD3D12Screen);
   struct d3d12_screen *      pD3D12Screen      = (struct d3d12_screen *) pD3D12Dec->m_pD3D12Screen;
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) target;
   assert(pD3D12VideoBuffer);

   ///
   /// Caps check
   ///

   // Let's quickly make sure we can decode what's being asked for.

   int capsResult = d3d12_screen_get_video_param(&pD3D12Screen->base,
                                                 codec->profile,
                                                 PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
                                                 PIPE_VIDEO_CAP_SUPPORTED);
   VERIFY_IS_TRUE(capsResult != 0);

   ///
   /// Compressed bitstream buffers
   ///

   /// Mesa VA frontend Video buffer passing semantics for H264, HEVC, MPEG4, VC1 and PIPE_VIDEO_PROFILE_VC1_ADVANCED
   /// are: If num_buffers == 1 -> buf[0] has the compressed bitstream WITH the starting code If num_buffers == 2 ->
   /// buf[0] has the NALU starting code and buf[1] has the compressed bitstream WITHOUT any starting code. If
   /// num_buffers = 3 -> It's JPEG, not supported in D3D12. num_buffers is at most 3.
   /// Mesa VDPAU frontend passes the buffers as they get passed in VdpDecoderRender without fixing any start codes
   /// except for PIPE_VIDEO_PROFILE_VC1_ADVANCED
   // In https://http.download.nvidia.com/XFree86/vdpau/doxygen/html/index.html#video_mixer_usage it's mentioned that:
   // It is recommended that applications pass solely the slice data to VDPAU; specifically that any header data
   // structures be excluded from the portion of the bitstream passed to VDPAU. VDPAU implementations must operate
   // correctly if non-slice data is included, at least for formats employing start codes to delimit slice data. For all
   // codecs/profiles it's highly recommended (when the codec/profile has such codes...) that the start codes are passed
   // to VDPAU, even when not included in the bitstream the VDPAU client is parsing. Let's assume we get all the start
   // codes for VDPAU. The doc also says "VDPAU implementations must operate correctly if non-slice data is included, at
   // least for formats employing start codes to delimit slice data" if we ever get an issue with VDPAU start codes we
   // should consider adding the code that handles this in the VDPAU layer above the gallium driver like mesa VA does.

   // To handle the multi-slice case where multiple slices mean multiple decode_bitstream calls, end_frame already takes
   // care of this assuming numSlices = pD3D12Dec->m_numConsecutiveDecodeFrame; and parsing the start codes from the
   // combined bitstream of all decode_bitstream calls.

   // VAAPI seems to send one decode_bitstream command per slice, but we should also support the VDPAU case where the
   // buffers have multiple buffer array entry per slice {startCode (optional), slice1, slice2, ..., startCode
   // (optional) , sliceN}

   if (num_buffers > 2)   // Assume this means multiple slices at once in a decode_bitstream call
   {
      // Based on VA frontend codebase, this never happens for video (no JPEG)
      // Based on VDPAU frontends codebase, this only happens when sending more than one slice at once in decode bitstream

      // To handle the case where VDPAU send all the slices at once in a single decode_bitstream call, let's pretend it
      // was a series of different calls

      // group by start codes and buffers and perform calls for the number of slices so m_numConsecutiveDecodeFrame
      // matches that number.
      D3D12_LOG_INFO("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream multiple slices on same call detected "
                     "for fenceValue: %d, breaking down the calls into one per slice\n",
                     pD3D12Dec->m_fenceValue);

      size_t curBufferIdx = 0;

      // Vars to be used for the delegation calls to decode_bitstream
      unsigned           call_num_buffers = 0;
      const void *const *call_buffers     = nullptr;
      const unsigned *   call_sizes       = nullptr;

      while (curBufferIdx < num_buffers) {
         // Store the current buffer as the base array pointer for the delegated call, later decide if it'll be a
         // startcode+slicedata or just slicedata call
         call_buffers = &buffers[curBufferIdx];
         call_sizes   = &sizes[curBufferIdx];

         // Usually start codes are less or equal than 4 bytes
         // If the current buffer is a start code buffer, send it along with the next buffer. Otherwise, just send the
         // current buffer.
         call_num_buffers = (sizes[curBufferIdx] <= 4) ? 2 : 1;

         // Delegate call with one or two buffers only
         d3d12_video_decoder_decode_bitstream(codec, target, picture, call_num_buffers, call_buffers, call_sizes);

         curBufferIdx += call_num_buffers;   // Consume from the loop the buffers sent in the last call
      }
   } else {
      ///
      /// Handle single slice buffer path, maybe with an extra start code buffer at buffers[0].
      ///

      // Both the start codes being present at buffers[0] and the rest in buffers [1] or full buffer at [0] cases can be
      // handled by flattening all the buffers into a single one and passing that to HW.

      size_t totalReceivedBuffersSize = 0u;   // Combined size of all sizes[]
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++) {
         totalReceivedBuffersSize += sizes[bufferIdx];
      }

      // Bytes of data pre-staged before this decode_frame call
      size_t preStagedDataSize = pD3D12Dec->m_stagingDecodeBitstream.size();

      // Extend the staging buffer size, as decode_frame can be called several times before end_frame
      pD3D12Dec->m_stagingDecodeBitstream.resize(preStagedDataSize + totalReceivedBuffersSize);

      // Point newSliceDataPositionDstBase to the end of the pre-staged data in m_stagingDecodeBitstream, where the new
      // buffers will be appended
      BYTE *newSliceDataPositionDstBase = pD3D12Dec->m_stagingDecodeBitstream.data() + preStagedDataSize;

      // Append new data at the end.
      size_t dstOffset = 0u;
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++) {
         memcpy(newSliceDataPositionDstBase + dstOffset, buffers[bufferIdx], sizes[bufferIdx]);
         dstOffset += sizes[bufferIdx];
      }

      ///
      /// Codec header picture parameters buffers
      ///

      // Only load the picture params on the first call to decode_bitstream for this frame, the subsequent calls should
      // have the same pic params/qmatrix.
      if (pD3D12Dec->m_numConsecutiveDecodeFrame == 0) {
         d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input(pD3D12Dec, picture, pD3D12VideoBuffer);
         assert(pD3D12Dec->m_picParamsBuffer.size() > 0);
      }

      pD3D12Dec->m_numConsecutiveDecodeFrame++;

      D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream finalized for fenceValue: %d\n",
                    pD3D12Dec->m_fenceValue);
   }
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
}

/**
 * end decoding of the current frame
 */
void
d3d12_video_decoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pD3D12Dec->m_pD3D12Screen;
   assert(pD3D12Screen);
   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_end_frame started for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) target;
   assert(pD3D12VideoBuffer);

   ///
   /// Prepare Slice control buffers before clearing staging buffer
   ///
   assert(pD3D12Dec->m_stagingDecodeBitstream.size() > 0);   // Make sure the staging wasn't cleared yet in end_frame
   d3d12_video_decoder_prepare_dxva_slices_control(pD3D12Dec);
   assert(pD3D12Dec->m_SliceControlBuffer.size() > 0);

   ///
   /// Upload m_stagingDecodeBitstream to GPU memory now that end_frame is called and clear staging buffer
   ///

   uint64_t sliceDataStagingBufferSize = pD3D12Dec->m_stagingDecodeBitstream.size();
   BYTE *   sliceDataStagingBufferPtr  = pD3D12Dec->m_stagingDecodeBitstream.data();

   // Reallocate if necessary to accomodate the current frame bitstream buffer in GPU memory
   if (pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize < sliceDataStagingBufferSize) {
      if (!d3d12_video_decoder_create_staging_bitstream_buffer(pD3D12Screen, pD3D12Dec, sliceDataStagingBufferSize)) {
         D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_end_frame - Failure on "
                         "d3d12_video_decoder_create_staging_bitstream_buffer\n");
      }
   }

   // Upload frame bitstream CPU data to ID3D12Resource buffer
   pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize =
      sliceDataStagingBufferSize;   // This can be less than m_curFrameCompressedBitstreamBufferAllocatedSize.
   assert(pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize <=
          pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize);
   pD3D12Dec->m_D3D12ResourceCopyHelper->UploadData(pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get(),
                                                    0,
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    sliceDataStagingBufferPtr,
                                                    sizeof(*sliceDataStagingBufferPtr) * sliceDataStagingBufferSize,
                                                    sizeof(*sliceDataStagingBufferPtr) * sliceDataStagingBufferSize);

   // Clear CPU staging buffer now that end_frame is called and was uploaded to GPU for DecodeFrame call.
   pD3D12Dec->m_stagingDecodeBitstream.resize(0);

   // Reset decode_frame counter at end_frame call
   pD3D12Dec->m_numConsecutiveDecodeFrame = 0;

   // Decrement begin_frame counter at end_frame call
   pD3D12Dec->m_numNestedBeginFrame--;

   ///
   /// Proceed to record the GPU Decode commands
   ///

   // Requested conversions by caller upper layer (none for now)
   D3D12VideoDecodeOutputConversionArguments requestedConversionArguments = {};

   ///
   /// Record DecodeFrame operation and resource state transitions.
   ///

   // Translate input D3D12 structure
   D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS d3d12InputArguments = {};

   d3d12InputArguments.CompressedBitstream.pBuffer = pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get();
   d3d12InputArguments.CompressedBitstream.Offset  = 0u;
   const uint64_t d3d12BitstreamOffsetAlignment =
      128u;   // specified in
              // https://docs.microsoft.com/en-us/windows/win32/api/d3d12video/ne-d3d12video-d3d12_video_decode_tier
   VERIFY_IS_TRUE((d3d12InputArguments.CompressedBitstream.Offset == 0) ||
                  ((d3d12InputArguments.CompressedBitstream.Offset % d3d12BitstreamOffsetAlignment) == 0));
   d3d12InputArguments.CompressedBitstream.Size = pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize;

   d3d12_record_state_transition(pD3D12Dec->m_spDecodeCommandList,
                                 d3d12InputArguments.CompressedBitstream.pBuffer,
                                 D3D12_RESOURCE_STATE_COMMON,
                                 D3D12_RESOURCE_STATE_VIDEO_DECODE_READ);

   // Schedule reverse (back to common) transitions before command list closes for current frame
   pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
      CD3DX12_RESOURCE_BARRIER::Transition(d3d12InputArguments.CompressedBitstream.pBuffer,
                                           D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON));

   ///
   /// Clear texture (no reference only flags in resource allocation) to use as decode output to send downstream for
   /// display/consumption
   ///
   ID3D12Resource *pOutputD3D12Texture;
   uint            outputD3D12Subresource = 0;

   ///
   /// Ref Only texture (with reference only flags in resource allocation) to use as reconstructed picture decode output
   /// and to store as future reference in DPB
   ///
   ID3D12Resource *pRefOnlyOutputD3D12Texture;
   uint            refOnlyOutputD3D12Subresource = 0;

   d3d12_video_decoder_prepare_for_decode_frame(pD3D12Dec,
                                                pD3D12VideoBuffer,
                                                &pOutputD3D12Texture,             // output
                                                &outputD3D12Subresource,          // output
                                                &pRefOnlyOutputD3D12Texture,      // output
                                                &refOnlyOutputD3D12Subresource,   // output
                                                requestedConversionArguments);

   ///
   /// Set codec picture parameters CPU buffer
   ///

   d3d12InputArguments.NumFrameArguments =
      1u;   // Only the codec data received from the above layer with picture params
   d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
      D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS,
      static_cast<UINT>(pD3D12Dec->m_picParamsBuffer.size()),
      pD3D12Dec->m_picParamsBuffer.data(),
   };

   if (pD3D12Dec->m_SliceControlBuffer.size() > 0) {
      d3d12InputArguments.NumFrameArguments++;
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL,
         static_cast<UINT>(pD3D12Dec->m_SliceControlBuffer.size()),
         pD3D12Dec->m_SliceControlBuffer.data(),
      };
   }

   if (pD3D12Dec->m_InverseQuantMatrixBuffer.size() > 0) {
      d3d12InputArguments.NumFrameArguments++;   // Only the codec data received from the above layer with
                                                 // InverseQuantMatrixBuffer params
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX,
         static_cast<UINT>(pD3D12Dec->m_InverseQuantMatrixBuffer.size()),
         pD3D12Dec->m_InverseQuantMatrixBuffer.data(),
      };
   }

   d3d12InputArguments.ReferenceFrames = pD3D12Dec->m_spDPBManager->GetCurrentFrameReferenceFrames();

   d3d12InputArguments.pHeap = pD3D12Dec->m_spVideoDecoderHeap.Get();

   // translate output D3D12 structure
   D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1 d3d12OutputArguments = {};
   d3d12OutputArguments.pOutputTexture2D                            = pOutputD3D12Texture;
   d3d12OutputArguments.OutputSubresource                           = outputD3D12Subresource;

   bool fReferenceOnly = (pD3D12Dec->m_ConfigDecoderSpecificFlags &
                          D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED) != 0;
   if (fReferenceOnly) {
      d3d12OutputArguments.ConversionArguments.Enable = TRUE;

      assert(pRefOnlyOutputD3D12Texture);
      d3d12OutputArguments.ConversionArguments.pReferenceTexture2D  = pRefOnlyOutputD3D12Texture;
      d3d12OutputArguments.ConversionArguments.ReferenceSubresource = refOnlyOutputD3D12Subresource;

      const D3D12_RESOURCE_DESC &descReference =
         d3d12OutputArguments.ConversionArguments.pReferenceTexture2D->GetDesc();
      d3d12OutputArguments.ConversionArguments.DecodeColorSpace = d3d12_convert_from_legacy_color_space(
         !util_format_is_yuv(d3d12_get_pipe_format(descReference.Format)),
         util_format_get_blocksize(d3d12_get_pipe_format(descReference.Format)) * 8 /*bytes to bits conversion*/,
         /* StudioRGB= */ false,
         /* P709= */ true,
         /* StudioYUV= */ true);

      const D3D12_RESOURCE_DESC &descOutput = d3d12OutputArguments.pOutputTexture2D->GetDesc();
      d3d12OutputArguments.ConversionArguments.OutputColorSpace =
         d3d12_convert_from_legacy_color_space(!util_format_is_yuv(d3d12_get_pipe_format(descOutput.Format)),                                                            
                                                            util_format_get_blocksize(d3d12_get_pipe_format(descOutput.Format)) * 8 /*bytes to bits conversion*/,
                                                            /* StudioRGB= */ false,
                                                            /* P709= */ true,
                                                            /* StudioYUV= */ true);

      const D3D12_VIDEO_DECODER_HEAP_DESC &HeapDesc         = pD3D12Dec->m_spVideoDecoderHeap->GetDesc();
      d3d12OutputArguments.ConversionArguments.OutputWidth  = HeapDesc.DecodeWidth;
      d3d12OutputArguments.ConversionArguments.OutputHeight = HeapDesc.DecodeHeight;
   } else {
      d3d12OutputArguments.ConversionArguments.Enable = FALSE;
   }

   CD3DX12_RESOURCE_DESC outputDesc(d3d12OutputArguments.pOutputTexture2D->GetDesc());
   UINT                  MipLevel, PlaneSlice, ArraySlice;
   D3D12DecomposeSubresource(d3d12OutputArguments.OutputSubresource,
                             outputDesc.MipLevels,
                             outputDesc.ArraySize(),
                             MipLevel,
                             ArraySlice,
                             PlaneSlice);

   for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
      uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
      d3d12_record_state_transition(pD3D12Dec->m_spDecodeCommandList,
                                    d3d12OutputArguments.pOutputTexture2D,
                                    D3D12_RESOURCE_STATE_COMMON,
                                    D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                    planeOutputSubresource);
   }

   // Schedule reverse (back to common) transitions before command list closes for current frame
   for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
      uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
      d3d12_record_state_transition(pD3D12Dec->m_spDecodeCommandList,
                                    d3d12OutputArguments.pOutputTexture2D,
                                    D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                    D3D12_RESOURCE_STATE_COMMON,
                                    planeOutputSubresource);
   }

   // Record DecodeFrame

   pD3D12Dec->m_spDecodeCommandList->DecodeFrame1(pD3D12Dec->m_spVideoDecoder.Get(),
                                                  &d3d12OutputArguments,
                                                  &d3d12InputArguments);

   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_end_frame finalized for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);

   ///
   /// Flush work to the GPU and blocking wait until decode finishes
   ///
   pD3D12Dec->m_needsGPUFlush = true;
   d3d12_video_decoder_flush(codec);

   ///
   /// To keep the decoded frame allocation lifetime available as a reference in the DPB
   /// Do GPU->GPU texture copy from decode output to pipe target decode texture sampler view planes
   ///

   // Get destination resource
   struct pipe_sampler_view **pPipeDstViews = target->get_sampler_view_planes(target);

   // Get source pipe_resource
   pipe_resource *pPipeSrc = d3d12_resource_from_resource(&pD3D12Screen->base, d3d12OutputArguments.pOutputTexture2D);
   assert(pPipeSrc);

   UINT outputMipLevel, outputPlaneSlice, outputArraySlice;
   D3D12DecomposeSubresource(d3d12OutputArguments.OutputSubresource,
                             outputDesc.MipLevels,
                             outputDesc.ArraySize(),
                             outputMipLevel,
                             outputArraySlice,
                             outputPlaneSlice);

   // Copy all format subresources/texture planes

   for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
      uint planeOutputSubresource = outputDesc.CalcSubresource(outputMipLevel, outputArraySlice, PlaneSlice);
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout          = {};
      UINT64                             totalPlaneBytes = 0;
      pD3D12Screen->dev
         ->GetCopyableFootprints(&outputDesc, planeOutputSubresource, 1, 0, &layout, nullptr, nullptr, &totalPlaneBytes);
      struct pipe_box box = { 0,
                              0,
                              0,
                              static_cast<int>(pPipeDstViews[PlaneSlice]->texture->width0),
                              static_cast<int16_t>(pPipeDstViews[PlaneSlice]->texture->height0),
                              1 };

      pD3D12Dec->base.context->resource_copy_region(pD3D12Dec->base.context,
                                                    pPipeDstViews[PlaneSlice]->texture,              // dst
                                                    PlaneSlice,                                      // dst subres
                                                    0,                                               // dstX
                                                    0,                                               // dstY
                                                    0,                                               // dstZ
                                                    (PlaneSlice == 0) ? pPipeSrc : pPipeSrc->next,   // src
                                                    planeOutputSubresource,                          // src subresource
                                                    &box);
   }
   // Flush resource_copy_region batch
   pD3D12Dec->base.context->flush(pD3D12Dec->base.context, NULL, 0);

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
}

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_decoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_flush started. Will flush video queue work and CPU wait on "
                 "fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);

   if (!pD3D12Dec->m_needsGPUFlush) {
      D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_flush started. Nothing to flush, all up to date.\n");
   } else {
      HRESULT hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_flush - D3D12Device was removed BEFORE commandlist "
                         "execution with HR %x.\n",
                         hr);
      }

      // Close and execute command list and wait for idle on CPU blocking
      // this method before resetting list and allocator for next submission.

      if (pD3D12Dec->m_transitionsBeforeCloseCmdList.size() > 0) {
         pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(pD3D12Dec->m_transitionsBeforeCloseCmdList.size(),
                                                           pD3D12Dec->m_transitionsBeforeCloseCmdList.data());
         pD3D12Dec->m_transitionsBeforeCloseCmdList.clear();
      }

      hr = pD3D12Dec->m_spDecodeCommandList->Close();
      if (FAILED(hr)) {
         D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_flush - Can't close command list with HR %x\n", hr);
      }

      ID3D12CommandList *ppCommandLists[1] = { pD3D12Dec->m_spDecodeCommandList.Get() };
      pD3D12Dec->m_spDecodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
      pD3D12Dec->m_spDecodeCommandQueue->Signal(pD3D12Dec->m_spFence.Get(), pD3D12Dec->m_fenceValue);
      pD3D12Dec->m_spFence->SetEventOnCompletion(pD3D12Dec->m_fenceValue, nullptr);
      D3D12_LOG_DBG("[d3d12_video_decoder] d3d12_video_decoder_flush - ExecuteCommandLists finished on signal with "
                    "fenceValue: %d\n",
                    pD3D12Dec->m_fenceValue);

      hr = pD3D12Dec->m_spCommandAllocator->Reset();
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_decoder] d3d12_video_decoder_flush - resetting ID3D12CommandAllocator failed with HR %x\n",
            hr);
      }

      hr = pD3D12Dec->m_spDecodeCommandList->Reset(pD3D12Dec->m_spCommandAllocator.Get());
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_decoder] d3d12_video_decoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
            hr);
      }

      // Validate device was not removed
      hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_flush - D3D12Device was removed AFTER commandlist "
                         "execution with HR %x, but wasn't before.\n",
                         hr);
      }

      D3D12_LOG_INFO(
         "[d3d12_video_decoder] d3d12_video_decoder_flush - GPU signaled execution finalized for fenceValue: %d\n",
         pD3D12Dec->m_fenceValue);

      pD3D12Dec->m_fenceValue++;
      pD3D12Dec->m_needsGPUFlush = false;
   }
}

bool
d3d12_video_decoder_create_command_objects(const struct d3d12_screen * pD3D12Screen,
                                           struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE };
   HRESULT                  hr               = pD3D12Screen->dev->CreateCommandQueue(&commandQueueDesc,
                                                      IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   hr = pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Dec->m_spFence));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR(
         "[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   hr = pD3D12Screen->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                                                  IID_PPV_ARGS(pD3D12Dec->m_spCommandAllocator.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to "
                      "CreateCommandAllocator failed with HR %x\n",
                      hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   hr = pD3D12Screen->dev->CreateCommandList(0,
                                             D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                                             pD3D12Dec->m_spCommandAllocator.Get(),
                                             nullptr,
                                             IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   D3D12_COMMAND_QUEUE_DESC copyQueueDesc = { D3D12_COMMAND_LIST_TYPE_COPY };
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Dec);
   hr = pD3D12Screen->dev->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(pD3D12Dec->m_spCopyQueue.GetAddressOf()));

   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   pD3D12Dec->m_D3D12ResourceCopyHelper.reset(new D3D12ResourceCopyHelper(pD3D12Dec->m_spCopyQueue.Get()));

   return true;
}

bool
d3d12_video_decoder_check_caps_and_create_decoder(const struct d3d12_screen * pD3D12Screen,
                                                  struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   pD3D12Dec->m_decoderDesc = {};

   D3D12_VIDEO_DECODE_CONFIGURATION decodeConfiguration = { pD3D12Dec->m_d3d12DecProfile,
                                                            D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE,
                                                            D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE };

   D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport = {};
   decodeSupport.NodeIndex                               = pD3D12Dec->m_NodeIndex;
   decodeSupport.Configuration                           = decodeConfiguration;
   decodeSupport.Width                                   = pD3D12Dec->base.width;
   decodeSupport.Height                                  = pD3D12Dec->base.height;
   decodeSupport.DecodeFormat                            = pD3D12Dec->m_decodeFormat;
   // no info from above layer on framerate/bitrate
   decodeSupport.FrameRate.Numerator   = 0;
   decodeSupport.FrameRate.Denominator = 0;
   decodeSupport.BitRate               = 0;

   HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
                                                                     &decodeSupport,
                                                                     sizeof(decodeSupport));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - CheckFeatureSupport "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   if (!(decodeSupport.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - "
                      "D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED was false when checking caps \n");
      return false;
   }

   pD3D12Dec->m_configurationFlags = decodeSupport.ConfigurationFlags;
   pD3D12Dec->m_tier               = decodeSupport.DecodeTier;

   if (d3d12_video_decoder_supports_aot_dpb(decodeSupport, pD3D12Dec->m_d3d12DecProfileType)) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ARRAY_OF_TEXTURES;
   }

   if (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ALIGNMENT_HEIGHT;
   }

   if (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED;
   }

   pD3D12Dec->m_decoderDesc.NodeMask      = pD3D12Dec->m_NodeMask;
   pD3D12Dec->m_decoderDesc.Configuration = decodeConfiguration;

   hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&pD3D12Dec->m_decoderDesc,
                                                            IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - CreateVideoDecoder "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   return true;
}

bool
d3d12_video_decoder_create_video_state_buffers(const struct d3d12_screen * pD3D12Screen,
                                               struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   if (!d3d12_video_decoder_create_staging_bitstream_buffer(pD3D12Screen,
                                                            pD3D12Dec,
                                                            pD3D12Dec->m_InitialCompBitstreamGPUBufferSize)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_video_state_buffers - Failure on "
                      "d3d12_video_decoder_create_staging_bitstream_buffer\n");
      return false;
   }

   return true;
}

bool
d3d12_video_decoder_create_staging_bitstream_buffer(const struct d3d12_screen * pD3D12Screen,
                                                    struct d3d12_video_decoder *pD3D12Dec,
                                                    UINT64                      bufSize)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   if (pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get() != nullptr) {
      pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Reset();
   }

   auto    descHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, pD3D12Dec->m_NodeMask, pD3D12Dec->m_NodeMask);
   auto    descResource = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
   HRESULT hr           = pD3D12Screen->dev->CreateCommittedResource(
      &descHeap,
      D3D12_HEAP_FLAG_NONE,
      &descResource,
      D3D12_RESOURCE_STATE_COMMON,
      nullptr,
      IID_PPV_ARGS(pD3D12Dec->m_curFrameCompressedBitstreamBuffer.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_decoder] d3d12_video_decoder_create_staging_bitstream_buffer - "
                      "CreateCommittedResource failed with HR %x\n",
                      hr);
      return false;
   }

   pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize = bufSize;
   return true;
}

void
d3d12_video_decoder_prepare_for_decode_frame(struct d3d12_video_decoder *pD3D12Dec,
                                             struct d3d12_video_buffer * pD3D12VideoBuffer,
                                             ID3D12Resource **           ppOutTexture2D,
                                             UINT *                      pOutSubresourceIndex,
                                             ID3D12Resource **           ppRefOnlyOutTexture2D,
                                             UINT *                      pRefOnlyOutSubresourceIndex,
                                             const D3D12VideoDecodeOutputConversionArguments &conversionArgs)
{
   d3d12_video_decoder_reconfigure_dpb(pD3D12Dec, pD3D12VideoBuffer, conversionArgs);

   // Refresh DPB active references for current frame, release memory for unused references.
   d3d12_video_decoder_refresh_dpb_active_references(pD3D12Dec);

   // Get the output texture for the current frame to be decoded
   pD3D12Dec->m_spDPBManager->GetCurrentFrameDecodeOutputTexture(ppOutTexture2D, pOutSubresourceIndex);

   // Get the reference only texture for the current frame to be decoded (if applicable)
   bool fReferenceOnly = (pD3D12Dec->m_ConfigDecoderSpecificFlags &
                          D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED) != 0;
   if (fReferenceOnly) {
      bool needsTransitionToDecodeWrite = false;
      pD3D12Dec->m_spDPBManager->GetReferenceOnlyOutput(ppRefOnlyOutTexture2D,
                                                        pRefOnlyOutSubresourceIndex,
                                                        needsTransitionToDecodeWrite);
      VERIFY_IS_TRUE(needsTransitionToDecodeWrite);

      CD3DX12_RESOURCE_DESC outputDesc((*ppRefOnlyOutTexture2D)->GetDesc());
      UINT                  MipLevel, PlaneSlice, ArraySlice;
      D3D12DecomposeSubresource(*pRefOnlyOutSubresourceIndex,
                                outputDesc.MipLevels,
                                outputDesc.ArraySize(),
                                MipLevel,
                                ArraySlice,
                                PlaneSlice);

      for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
         uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
         d3d12_record_state_transition(pD3D12Dec->m_spDecodeCommandList,
                                       *ppRefOnlyOutTexture2D,
                                       D3D12_RESOURCE_STATE_COMMON,
                                       D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                       planeOutputSubresource);
      }

      // Schedule reverse (back to common) transitions before command list closes for current frame
      for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
         uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
         d3d12_record_state_transition(pD3D12Dec->m_spDecodeCommandList,
                                       *ppRefOnlyOutTexture2D,
                                       D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                       D3D12_RESOURCE_STATE_COMMON,
                                       planeOutputSubresource);
      }
   }

   // If decoded needs reference_only entries in the dpb, use the reference_only allocation for current frame
   // otherwise, use the standard output resource
   ID3D12Resource *pCurrentFrameDPBEntry = fReferenceOnly ? *ppRefOnlyOutTexture2D : *ppOutTexture2D;
   UINT currentFrameDPBEntrySubresource  = fReferenceOnly ? *pRefOnlyOutSubresourceIndex : *pOutSubresourceIndex;

   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         d3d12_video_decoder_prepare_current_frame_references_h264(pD3D12Dec,
                                                                   pCurrentFrameDPBEntry,
                                                                   currentFrameDPBEntrySubresource);
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_prepare_for_decode_frame",
                                                  "Unsupported profile",
                                                  pD3D12Dec->m_d3d12DecProfileType);
      } break;
   }
}

void
d3d12_video_decoder_reconfigure_dpb(struct d3d12_video_decoder *                     pD3D12Dec,
                                    struct d3d12_video_buffer *                      pD3D12VideoBuffer,
                                    const D3D12VideoDecodeOutputConversionArguments &conversionArguments)
{
   UINT   width;
   UINT   height;
   UINT16 maxDPB;
   bool   isInterlaced;
   d3d12_video_decoder_get_frame_info(pD3D12Dec, &width, &height, &maxDPB, isInterlaced);

   ID3D12Resource *               pPipeD3D12DstResource = d3d12_resource_resource(pD3D12VideoBuffer->m_pD3D12Resource);
   D3D12_RESOURCE_DESC            outputResourceDesc    = pPipeD3D12DstResource->GetDesc();
   VIDEO_DECODE_PROFILE_BIT_DEPTH resourceBitDepth = d3d12_video_decoder_get_format_bitdepth(outputResourceDesc.Format);

   pD3D12VideoBuffer->base.interlaced = isInterlaced;
   D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE interlaceTypeRequested =
      isInterlaced ? D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED : D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
   if ((pD3D12Dec->m_decodeFormat != outputResourceDesc.Format) ||
       (pD3D12Dec->m_decoderDesc.Configuration.InterlaceType != interlaceTypeRequested)) {
      // Copy current pD3D12Dec->m_decoderDesc, modify decodeprofile and re-create decoder.
      D3D12_VIDEO_DECODER_DESC decoderDesc    = pD3D12Dec->m_decoderDesc;
      decoderDesc.Configuration.InterlaceType = interlaceTypeRequested;
      decoderDesc.Configuration.DecodeProfile =
         d3d12_video_decoder_resolve_profile(pD3D12Dec->m_d3d12DecProfileType, resourceBitDepth);
      pD3D12Dec->m_spVideoDecoder.Reset();
      HRESULT hr =
         pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&decoderDesc,
                                                             IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_decoder] d3d12_video_decoder_reconfigure_dpb - CreateVideoDecoder failed with HR %x\n",
            hr);
      }
      // Update state after CreateVideoDecoder succeeds only.
      pD3D12Dec->m_decoderDesc = decoderDesc;
   }

   if (!pD3D12Dec->m_spDPBManager || !pD3D12Dec->m_spVideoDecoderHeap ||
       pD3D12Dec->m_decodeFormat != outputResourceDesc.Format || pD3D12Dec->m_decoderHeapDesc.DecodeWidth != width ||
       pD3D12Dec->m_decoderHeapDesc.DecodeHeight != height ||
       pD3D12Dec->m_decoderHeapDesc.MaxDecodePictureBufferCount < maxDPB) {
      // Detect the combination of AOT/ReferenceOnly to configure the DPB manager
      UINT16 referenceCount = (conversionArguments.Enable) ? (UINT16) conversionArguments.ReferenceFrameCount +
                                                                1 /*extra slot for current picture*/ :
                                                             maxDPB;
      d3d12_video_decode_dpb_descriptor dpbDesc = {};
      dpbDesc.Width              = (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Width : width;
      dpbDesc.Height             = (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Height : height;
      dpbDesc.Format =
         (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Format.Format : outputResourceDesc.Format;
      dpbDesc.fArrayOfTexture =
         ((pD3D12Dec->m_ConfigDecoderSpecificFlags & D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ARRAY_OF_TEXTURES) != 0);
      dpbDesc.dpbSize        = referenceCount;
      dpbDesc.m_NodeMask     = pD3D12Dec->m_NodeMask;
      dpbDesc.fReferenceOnly = ((pD3D12Dec->m_ConfigDecoderSpecificFlags &
                                 D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_REFERENCE_ONLY_TEXTURES_REQUIRED) != 0);

      // Create DPB manager
      if (pD3D12Dec->m_spDPBManager == nullptr) {
         pD3D12Dec->m_spDPBManager.reset(new D3D12VideoDecoderReferencesManager(pD3D12Dec->m_pD3D12Screen,
                                                                                pD3D12Dec->m_NodeMask,
                                                                                pD3D12Dec->m_d3d12DecProfileType,
                                                                                dpbDesc));
      }

      //
      // (Re)-create decoder heap
      //
      D3D12_VIDEO_DECODER_HEAP_DESC decoderHeapDesc = {};
      decoderHeapDesc.NodeMask                      = pD3D12Dec->m_NodeMask;
      decoderHeapDesc.Configuration                 = pD3D12Dec->m_decoderDesc.Configuration;
      decoderHeapDesc.DecodeWidth                   = dpbDesc.Width;
      decoderHeapDesc.DecodeHeight                  = dpbDesc.Height;
      decoderHeapDesc.Format                        = dpbDesc.Format;
      decoderHeapDesc.MaxDecodePictureBufferCount   = maxDPB;
      pD3D12Dec->m_spVideoDecoderHeap.Reset();
      HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoderHeap(
         &decoderHeapDesc,
         IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoderHeap.GetAddressOf()));
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_decoder] d3d12_video_decoder_reconfigure_dpb - CreateVideoDecoderHeap failed with HR %x\n",
            hr);
      }
      // Update pD3D12Dec after CreateVideoDecoderHeap succeeds only.
      pD3D12Dec->m_decoderHeapDesc = decoderHeapDesc;
   }

   pD3D12Dec->m_decodeFormat = outputResourceDesc.Format;
}

void
d3d12_video_decoder_refresh_dpb_active_references(struct d3d12_video_decoder *pD3D12Dec)
{
   // Method overview
   // 1. Codec specific strategy in switch statement regarding reference frames eviction policy. Should only mark active
   // DPB references, leaving evicted ones as unused
   // 2. Call ReleaseUnusedReferencesTexturesMemory(); at the end of this method. Any references (and texture
   // allocations associated)
   //    that were left not marked as used in m_spDPBManager by step (2) are lost.

   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         d3d12_video_decoder_refresh_dpb_active_references_h264(pD3D12Dec);
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_refresh_dpb_active_references",
                                                  "Unsupported profile",
                                                  pD3D12Dec->m_d3d12DecProfileType);
      } break;
   }

   // Releases the underlying reference picture texture objects of all references that were not marked as used in this
   // method.
   pD3D12Dec->m_spDPBManager->ReleaseUnusedReferencesTexturesMemory();
}

void
d3d12_video_decoder_get_frame_info(
   struct d3d12_video_decoder *pD3D12Dec, UINT *pWidth, UINT *pHeight, UINT16 *pMaxDPB, bool &isInterlaced)
{
   *pWidth      = 0;
   *pHeight     = 0;
   *pMaxDPB     = 0;
   isInterlaced = false;

   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         d3d12_video_decoder_get_frame_info_h264(pD3D12Dec, pWidth, pHeight, pMaxDPB, isInterlaced);
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_get_frame_info",
                                                  "Unsupported profile",
                                                  pD3D12Dec->m_d3d12DecProfileType);
      } break;
   }

   if (pD3D12Dec->m_ConfigDecoderSpecificFlags & D3D12_VIDEO_DECODE_CONFIG_SPECIFIC_ALIGNMENT_HEIGHT) {
      const UINT AlignmentMask = 31;
      *pHeight                 = (*pHeight + AlignmentMask) & ~AlignmentMask;
   }
}

///
/// Returns the number of bytes starting from [buf.data() + buffsetOffset] where the _targetCode_ is found
/// Returns -1 if start code not found
///
int
d3d12_video_decoder_get_next_startcode_offset(std::vector<BYTE> &buf,
                                              unsigned int       bufferOffset,
                                              unsigned int       targetCode,
                                              unsigned int       targetCodeBitSize,
                                              unsigned int       numBitsToSearchIntoBuffer)
{
   struct vl_vlc vlc = { 0 };

   // Shorten the buffer to be [buffetOffset, endOfBuf)
   unsigned int bufSize = buf.size() - bufferOffset;
   uint8_t *    bufPtr  = buf.data();
   bufPtr += bufferOffset;

   /* search the first numBitsToSearchIntoBuffer bytes for a startcode */
   vl_vlc_init(&vlc, 1, (const void *const *) &bufPtr, &bufSize);
   for (uint i = 0; i < numBitsToSearchIntoBuffer && vl_vlc_bits_left(&vlc) >= targetCodeBitSize; ++i) {
      if (vl_vlc_peekbits(&vlc, targetCodeBitSize) == targetCode)
         return i;
      vl_vlc_eatbits(&vlc, 8);   // Stride is 8 bits = 1 byte
      vl_vlc_fillbits(&vlc);
   }

   return -1;
}

void
d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input(
   struct d3d12_video_decoder *codec,   // input argument, current decoder
   struct pipe_picture_desc
      *picture,   // input argument, base structure of pipe_XXX_picture_desc where XXX is the codec name
   struct d3d12_video_buffer *pD3D12VideoBuffer   // input argument, target video buffer
)
{
   assert(picture);
   assert(codec);
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;

   D3D12_VIDEO_DECODE_PROFILE_TYPE profileType =
      d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(codec->base.profile);
   switch (profileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         size_t                  dxvaPicParamsBufferSize = sizeof(DXVA_PicParams_H264);
         pipe_h264_picture_desc *pPicControlH264         = (pipe_h264_picture_desc *) picture;
         ID3D12Resource *        pPipeD3D12DstResource   = d3d12_resource_resource(pD3D12VideoBuffer->m_pD3D12Resource);
         D3D12_RESOURCE_DESC     outputResourceDesc      = pPipeD3D12DstResource->GetDesc();
         DXVA_PicParams_H264     dxvaPicParamsH264 =
            d3d12_video_decoder_dxva_picparams_from_pipe_picparams_h264(pD3D12Dec->m_fenceValue,
                                                                        codec->base.profile,
                                                                        outputResourceDesc.Width,
                                                                        outputResourceDesc.Height,
                                                                        pPicControlH264);
         d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(codec,
                                                                      &dxvaPicParamsH264,
                                                                      dxvaPicParamsBufferSize);

         size_t            dxvaQMatrixBufferSize           = sizeof(DXVA_Qmatrix_H264);
         DXVA_Qmatrix_H264 dxvaQmatrixH264                 = {};
         bool              seq_scaling_matrix_present_flag = false;
         d3d12_video_decoder_dxva_qmatrix_from_pipe_picparams_h264((pipe_h264_picture_desc *) picture,
                                                                   dxvaQmatrixH264,
                                                                   seq_scaling_matrix_present_flag);
         if (seq_scaling_matrix_present_flag) {
            d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(codec, &dxvaQmatrixH264, dxvaQMatrixBufferSize);
         } else {
            codec->m_InverseQuantMatrixBuffer.resize(0);   //  m_InverseQuantMatrixBuffer.size() == 0 means no
                                                           //  quantization matrix buffer is set for current frame
         }
      } break;
      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input",
                                                  "Unsupported profile",
                                                  profileType);
      } break;
   }
}

void
d3d12_video_decoder_prepare_dxva_slices_control(
   struct d3d12_video_decoder *pD3D12Dec   // input argument, current decoder
)
{
   D3D12_VIDEO_DECODE_PROFILE_TYPE profileType =
      d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(pD3D12Dec->base.profile);
   switch (profileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
      {
         size_t                             numSlices = pD3D12Dec->m_numConsecutiveDecodeFrame;
         std::vector<DXVA_Slice_H264_Short> pOutSliceControlBuffers(numSlices);

         d3d12_video_decoder_prepare_dxva_slices_control_h264(pD3D12Dec, numSlices, pOutSliceControlBuffers);

         assert(sizeof(pOutSliceControlBuffers.data()[0]) == sizeof(DXVA_Slice_H264_Short));
         UINT64 DXVAStructSize = pOutSliceControlBuffers.size() * sizeof((pOutSliceControlBuffers.data()[0]));
         assert((DXVAStructSize % sizeof(DXVA_Slice_H264_Short)) == 0);
         d3d12_video_decoder_store_dxva_slicecontrol_in_slicecontrol_buffer(pD3D12Dec,
                                                                            pOutSliceControlBuffers.data(),
                                                                            DXVAStructSize);
         assert(pD3D12Dec->m_SliceControlBuffer.size() == DXVAStructSize);
      } break;
      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_prepare_dxva_slices_control",
                                                  "Unsupported profile",
                                                  profileType);
      } break;
   }
}

void
d3d12_video_decoder_store_dxva_slicecontrol_in_slicecontrol_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                                   void *                      pDXVAStruct,
                                                                   UINT64                      DXVAStructSize)
{
   if (pD3D12Dec->m_SliceControlBuffer.capacity() < DXVAStructSize) {
      pD3D12Dec->m_SliceControlBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_SliceControlBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_SliceControlBuffer.data(), pDXVAStruct, DXVAStructSize);
}

void
d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                         void *                      pDXVAStruct,
                                                         UINT64                      DXVAStructSize)
{
   if (pD3D12Dec->m_InverseQuantMatrixBuffer.capacity() < DXVAStructSize) {
      pD3D12Dec->m_InverseQuantMatrixBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_InverseQuantMatrixBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_InverseQuantMatrixBuffer.data(), pDXVAStruct, DXVAStructSize);
}

void
d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                             void *                      pDXVAStruct,
                                                             UINT64                      DXVAStructSize)
{
   if (pD3D12Dec->m_picParamsBuffer.capacity() < DXVAStructSize) {
      pD3D12Dec->m_picParamsBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_picParamsBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_picParamsBuffer.data(), pDXVAStruct, DXVAStructSize);
}

bool
d3d12_video_decoder_supports_aot_dpb(D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport,
                                     D3D12_VIDEO_DECODE_PROFILE_TYPE         profileType)
{
   bool supportedProfile = false;
   switch (profileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
         supportedProfile = true;
         break;
      default:
         supportedProfile = false;
         break;
   }

   return (decodeSupport.DecodeTier >= D3D12_VIDEO_DECODE_TIER_2) && supportedProfile;
}

D3D12_VIDEO_DECODE_PROFILE_TYPE
d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(enum pipe_video_profile profile)
{
   switch (profile) {
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
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_convert_pipe_video_profile_to_profile_type",
                                                  "Unsupported profile",
                                                  profile);
         return D3D12_VIDEO_DECODE_PROFILE_TYPE_NONE;
      } break;
   }
}

GUID
d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(enum pipe_video_profile profile)
{
   switch (profile) {
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
         return {};
   }
}

GUID
d3d12_video_decoder_resolve_profile(D3D12_VIDEO_DECODE_PROFILE_TYPE profileType, UINT resourceBitDepth)
{
   switch (profileType) {
      case D3D12_VIDEO_DECODE_PROFILE_TYPE_H264:
         return D3D12_VIDEO_DECODE_PROFILE_H264;
         break;
      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_resolve_profile",
                                                  "Unsupported profile",
                                                  profileType);
         return {};
      } break;
   }
}

VIDEO_DECODE_PROFILE_BIT_DEPTH
d3d12_video_decoder_get_format_bitdepth(DXGI_FORMAT Format)
{
   switch (Format) {
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
      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_decoder_get_format_bitdepth",
                                                  "Unsupported DXGI_FORMAT",
                                                  Format);
         return VIDEO_DECODE_PROFILE_BIT_DEPTH_NONE;
      } break;
   }
}

#pragma GCC diagnostic pop
