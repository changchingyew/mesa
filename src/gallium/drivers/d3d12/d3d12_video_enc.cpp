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
#include "d3d12_video_enc.h"
#include "d3d12_video_enc_h264.h"
#include "d3d12_state_transition_helper.h"
#include "d3d12_video_buffer.h"
#include "d3d12_video_texture_array_dpb_manager.h"
#include "d3d12_video_array_of_textures_dpb_manager.h"
#include "d3d12_video_encoder_references_manager_h264.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "util/vl_vlc.h"

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void d3d12_video_encoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_encoder* pD3D12Enc = (struct d3d12_video_encoder*) codec;
   assert(pD3D12Enc);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);   
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_flush started. Will flush video queue work and CPU wait on fenceValue: %d\n", pD3D12Enc->m_fenceValue);

   HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
   if(hr != S_OK)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_flush - D3D12Device was removed BEFORE commandlist execution.\n");
   }

   // Close and execute command list and wait for idle on CPU blocking 
   // this method before resetting list and allocator for next submission.

   d3d12_record_state_transitions
   (
      pD3D12Enc->m_spEncodeCommandList,
      pD3D12Enc->m_transitionsBeforeCloseCmdList
   );
   pD3D12Enc->m_transitionsBeforeCloseCmdList.clear();

   hr = pD3D12Enc->m_spEncodeCommandList->Close();
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_flush - Can't close command list with HR %x\n", hr);
   }

   ID3D12CommandList *ppCommandLists[1] = { pD3D12Enc->m_spEncodeCommandList.Get() };
   pD3D12Enc->m_spEncodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
   pD3D12Enc->m_spEncodeCommandQueue->Signal(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue);
   pD3D12Enc->m_spFence->SetEventOnCompletion(pD3D12Enc->m_fenceValue, nullptr);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_flush - ExecuteCommandLists finished on signal with fenceValue: %d\n", pD3D12Enc->m_fenceValue);

   hr = pD3D12Enc->m_spCommandAllocator->Reset();
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_flush - resetting ID3D12CommandAllocator failed with HR %x\n", hr);
   }

   hr = pD3D12Enc->m_spEncodeCommandList->Reset(pD3D12Enc->m_spCommandAllocator.Get());
   if (FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n", hr);
   }

   // Validate device was not removed
   hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
   if(hr != S_OK)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_flush - D3D12Device was removed AFTER commandlist execution, but wasn't before.\n");
   }
   
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_flush - GPU signaled execution finalized for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
   
   pD3D12Enc->m_fenceValue++;
}

/**
 * Destroys a d3d12_video_encoder
 * Call destroy_XX for applicable XX nested member types before deallocating 
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of cleanup from failure on the creation method
*/
void d3d12_video_encoder_destroy(struct pipe_video_codec *codec)
{
   if(codec == nullptr)
   {
      return;
   }

   d3d12_video_encoder_flush(codec); // Flush pending work before destroying.

   struct d3d12_video_encoder* pD3D12Enc = (struct d3d12_video_encoder*) codec;

   // Call d3d12_video_encoder dtor to make ComPtr and other member's destructors work
   delete pD3D12Enc;
}

void d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   // TODO: Calculate uints below based on caps
   UINT16 MaxDPBCapacity = 8u; 
   UINT MaxL0ReferencesForP = 8;
   UINT MaxL0ReferencesForB = 8;
   UINT MaxL1ReferencesForB = 8;

   bool needsObjCreation = 
      (!pD3D12Enc->m_upDPBManager)
      || (!pD3D12Enc->m_spVideoEncoder)
      || (!pD3D12Enc->m_spVideoEncoderHeap);

   bool needsReconfiguration = false;
   if(!needsObjCreation)
   {
      UINT numRes = pD3D12Enc->m_spVideoEncoderHeap->GetResolutionListCount();
      std::vector<D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC> heapRes(numRes);
      VERIFY_SUCCEEDED(pD3D12Enc->m_spVideoEncoderHeap->GetResolutionList(numRes, heapRes.data()));
      assert(numRes == 1); // Need to extend validation below otherwise

      needsReconfiguration = 
         (pD3D12Enc->m_spVideoEncoder->GetInputFormat() != pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format)
         || (heapRes[0].Width != pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width)
         || (heapRes[0].Height != pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height);
      // TODO: Add other reconfig triggers like slices, rate control, codec config struct, etc
      // TODO: set reconfig (ie. slices, rate control, resolution) flags in encodeframe structs without re-creating dpb/heap/encoder sometimes
   }

   if (needsObjCreation || needsReconfiguration)
   {
      bool fArrayOfTexture = true; // TODO: Check D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS
      D3D12_RESOURCE_FLAGS resourceAllocFlags = D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      if(fArrayOfTexture)
      {
         pD3D12Enc->m_upDPBStorageManager = std::make_unique< ArrayOfTexturesDPBManager<ID3D12VideoEncoderHeap> >(
            MaxDPBCapacity,
            pD3D12Enc->m_pD3D12Screen->dev,
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            (D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
            true, // setNullSubresourcesOnAllZero - D3D12 Video Encode expects nullptr pSubresources if AoT,
            pD3D12Enc->m_NodeMask);
      }      
      else
      {
         pD3D12Enc->m_upDPBStorageManager = std::make_unique< TexturesArrayDPBManager<ID3D12VideoEncoderHeap> > (
            MaxDPBCapacity, 
            pD3D12Enc->m_pD3D12Screen->dev,
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format, 
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            resourceAllocFlags,
            pD3D12Enc->m_NodeMask);
      }

      // TODO: Initialization of m_upDPBManager is codec specific, delegate in _H264 function
      pD3D12Enc->m_upDPBManager = std::make_unique<D3D12VideoEncoderH264FIFOReferenceManager>      
      (
         //   UINT GOPLength,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.GOPLength,
         //   UINT PPicturePeriod,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.PPicturePeriod,
         // Component to handle the DPB ID3D12Resource allocations
          *pD3D12Enc->m_upDPBStorageManager,
         // Cap based limitations from Encode12 API
         //   UINT MaxL0ReferencesForP,
         MaxL0ReferencesForP,
         //   UINT MaxL0ReferencesForB,
         MaxL0ReferencesForB,
         //   UINT MaxL1ReferencesForB,
         MaxL1ReferencesForB,
         //   UINT MaxDPBCapacity,
         MaxDPBCapacity,
         // Codec config
         //   UCHAR pic_order_cnt_type,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.pic_order_cnt_type,
         //   UCHAR log2_max_frame_num_minus4,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.log2_max_frame_num_minus4,
         //   UCHAR log2_max_pic_order_cnt_lsb_minus4,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.log2_max_pic_order_cnt_lsb_minus4,
         //   bool forceGOPIDRs = true
         true
      );

      // Create encoder and encoder heap descriptors based off m_currentEncodeConfig

      D3D12_VIDEO_ENCODER_DESC encoderDesc = 
      {
         pD3D12Enc->m_NodeMask,
         D3D12_VIDEO_ENCODER_FLAG_NONE, 
         pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
         d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
         pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
         d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
         pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit
      };

      D3D12_VIDEO_ENCODER_HEAP_DESC heapDesc =
      {
         pD3D12Enc->m_NodeMask,
         D3D12_VIDEO_ENCODER_HEAP_FLAG_NONE,
         pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
         d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
         d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
         // resolution list count
         1, 
         // resolution list
         &pD3D12Enc->m_currentEncodeConfig.m_currentResolution
      };

      // Create encoder
      VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoder(&encoderDesc, IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoder.GetAddressOf())));

      // Create encoder heap
      VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoderHeap(&heapDesc, IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap.GetAddressOf())));
   }

   // Update current frame pic params state after reconfiguring above.
   // TODO: codec specific, delegate in _H264 function
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA codecPicData = { };
   codecPicData.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData);
   codecPicData.pH264PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData;
   pD3D12Enc->m_upDPBManager->GetCurrentFramePictureControlData(codecPicData);
}

D3D12_VIDEO_ENCODER_LEVEL_SETTING d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder* pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec)
   {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = { };
         curLevelDesc.pH264LevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting);
         return curLevelDesc;
      } break;
      
      default:
         assert(0);
   }
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder* pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec)
   {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = { };
         codecConfigDesc.pH264Config = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config;
         codecConfigDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config);
         return codecConfigDesc;
      } break;
      
      default:
         assert(0);
   }
}

D3D12_VIDEO_ENCODER_PROFILE_DESC d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder* pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec)
   {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = { };
         curProfDesc.pH264Profile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile;
         curProfDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile);
         return curProfDesc;
      } break;
      
      default:
         assert(0);
   }
}

void d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec)
   {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_current_encoder_config_state_h264(pD3D12Enc, srcTexture, picture);
      } break;
      
      default:
         assert(0);
   }
}

bool d3d12_create_video_encode_command_objects(struct d3d12_video_encoder* pD3D12Enc)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE };
   HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(
      &commandQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandQueue.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_encode_command_objects - Call to CreateCommandQueue failed with HR %x\n", hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Enc->m_spFence));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_encode_command_objects - Call to CreateFence failed with HR %x\n", hr);
      return false;
   }
   
   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
      IID_PPV_ARGS(pD3D12Enc->m_spCommandAllocator.GetAddressOf()));
   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_encode_command_objects - Call to CreateCommandAllocator failed with HR %x\n", hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandList(
      0,
      D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
      pD3D12Enc->m_spCommandAllocator.Get(),
      nullptr,
      IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandList.GetAddressOf()));

   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_encode_command_objects - Call to CreateCommandList failed with HR %x\n", hr);
      return false;
   }

   D3D12_COMMAND_QUEUE_DESC copyQueueDesc = { D3D12_COMMAND_LIST_TYPE_COPY };
   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(
      &copyQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spCopyQueue.GetAddressOf()));

   if(FAILED(hr))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_create_video_encode_command_objects - Call to CreateCommandQueue failed with HR %x\n", hr);
      return false;
   }

   pD3D12Enc->m_D3D12ResourceCopyHelper.reset(new D3D12ResourceCopyHelper(pD3D12Enc->m_spCopyQueue.Get()));

   return true;
}

struct pipe_video_codec *d3d12_video_encoder_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_encoder
   ///

   struct d3d12_video_encoder* pD3D12Enc = new d3d12_video_encoder; // Not using new doesn't call ctor and the initializations in the class declaration are lost
   if (!pD3D12Enc)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_create_encoder - Could not allocate memory for d3d12_video_encoder\n");
      return nullptr;
   }

	pD3D12Enc->base = *codec;
	pD3D12Enc->m_screen = context->screen;
	pD3D12Enc->base.context = context;
	pD3D12Enc->base.width = codec->width;
	pD3D12Enc->base.height = codec->height;   
   // Only fill methods that are supported by the d3d12 encoder, leaving null the rest (ie. encode_* / encode_macroblock)
	pD3D12Enc->base.destroy = d3d12_video_encoder_destroy;
	pD3D12Enc->base.begin_frame = d3d12_video_encoder_begin_frame;
	pD3D12Enc->base.encode_bitstream = d3d12_video_encoder_encode_bitstream;
	pD3D12Enc->base.end_frame = d3d12_video_encoder_end_frame;
	pD3D12Enc->base.flush = d3d12_video_encoder_flush;   
   // TODO: pipe interface function get_feeback?

   struct d3d12_context* pD3D12Ctx = (struct d3d12_context*) context;
   pD3D12Enc->m_pD3D12Screen = d3d12_screen(pD3D12Ctx->base.screen);

   if(FAILED(pD3D12Enc->m_pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(pD3D12Enc->m_spD3D12VideoDevice.GetAddressOf()))))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_create_encoder - D3D12 Device has no Video encode support\n");
      goto failed;
   }   

   if(!d3d12_create_video_encode_command_objects(pD3D12Enc))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_create_encoder - Failure on d3d12_create_video_encode_command_objects\n");
      goto failed;
   }

   return &pD3D12Enc->base;

failed:
   if (pD3D12Enc != nullptr)
   {
      d3d12_video_encoder_destroy((struct pipe_video_codec*) pD3D12Enc);
   }

   return nullptr;
}

bool d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);   
   
   d3d12_video_encoder_update_current_encoder_config_state(pD3D12Enc, srcTexture, picture);
   d3d12_video_encoder_reconfigure_encoder_objects(pD3D12Enc, srcTexture, picture);

   return true;
}

/**
 * start encoding of a new frame
 */
void d3d12_video_encoder_begin_frame(struct pipe_video_codec *codec,
                     struct pipe_video_buffer *target,
                     struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on encoder creation, re-config (if any) happens in d3d12_video_encoder_encode_bitstream
   struct d3d12_video_encoder* pD3D12Enc = (struct d3d12_video_encoder*) codec;
   assert(pD3D12Enc);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_begin_frame started for fenceValue: %d\n", pD3D12Enc->m_fenceValue);

   if(!d3d12_video_encoder_reconfigure_session(pD3D12Enc, target, picture))
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver] d3d12_video_encoder_begin_frame - Failure on d3d12_video_encoder_reconfigure_session\n");
   }

   if(pD3D12Enc->m_numNestedBeginFrame > 0)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver] Nested d3d12_video_encoder_begin_frame calls are not supported. Call d3d12_video_encoder_end_frame to finalize current frame before calling d3d12_video_encoder_begin_frame again.\n");
   }

   pD3D12Enc->m_numNestedBeginFrame++;

   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_begin_frame finalized for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
}

/**
 * encode a bitstream
 */
void d3d12_video_encoder_encode_bitstream(struct pipe_video_codec *codec,
                           struct pipe_video_buffer *source,
                           struct pipe_resource *destination,
                           void **feedback)
{
   struct d3d12_video_encoder* pD3D12Enc = (struct d3d12_video_encoder*) codec;
   assert(pD3D12Enc);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_encode_bitstream started for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);
   assert(pD3D12Enc->m_pD3D12Screen);

   if(pD3D12Enc->m_numConsecutiveEncodeFrame > 0)
   {
      D3D12_LOG_ERROR("[D3D12 Video Driver] Nested d3d12_video_encoder_encode_bitstream calls are not supported. Call d3d12_video_encoder_end_frame to finalize current frame before calling d3d12_video_encoder_encode_bitstream again.\n");
   }

   pD3D12Enc->m_numConsecutiveEncodeFrame++;

   ///
   /// Caps check
   ///

   // Let's quickly make sure we can encode what's being asked for.

   int capsResult = d3d12_screen_get_video_param(&pD3D12Enc->m_pD3D12Screen->base,
                               codec->profile,
                               PIPE_VIDEO_ENTRYPOINT_ENCODE,
                               PIPE_VIDEO_CAP_SUPPORTED);
   assert(capsResult != 0);

   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_encode_bitstream finalized for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
}

/**
 * end encoding of the current frame
 */
void d3d12_video_encoder_end_frame(struct pipe_video_codec *codec,
                  struct pipe_video_buffer *target,
                  struct pipe_picture_desc *picture)
{
   struct d3d12_video_encoder* pD3D12Enc = (struct d3d12_video_encoder*) codec;
   assert(pD3D12Enc);
   struct d3d12_screen* pD3D12Screen = (struct d3d12_screen*) pD3D12Enc->m_pD3D12Screen;
   assert(pD3D12Screen);
   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_end_frame started for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);
   struct d3d12_video_buffer* pD3D12VideoBuffer = (struct d3d12_video_buffer*) target;
   assert(pD3D12VideoBuffer);

   // Reset encode_frame counter at end_frame call
   pD3D12Enc->m_numConsecutiveEncodeFrame = 0;

   // Decrement begin_frame counter at end_frame call
   pD3D12Enc->m_numNestedBeginFrame--;

   D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_end_frame finalized for fenceValue: %d\n", pD3D12Enc->m_fenceValue);

   ///
   /// Flush work to the GPU and blocking wait until encode finishes 
   ///
   d3d12_video_encoder_flush(codec);
}

#pragma GCC diagnostic pop