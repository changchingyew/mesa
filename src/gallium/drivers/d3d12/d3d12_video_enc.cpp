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

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_encoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);

   if (!pD3D12Enc->m_needsGPUFlush) {
      D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_flush started. Nothing to flush, all up to date.\n");
   } else {
      D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_flush started. Will flush video queue work and CPU wait "
                    "on fenceValue: %d\n",
                    pD3D12Enc->m_fenceValue);

      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_flush - D3D12Device was removed BEFORE commandlist "
                         "execution with HR %x.\n",
                         hr);
      }

      // Close and execute command list and wait for idle on CPU blocking
      // this method before resetting list and allocator for next submission.

      if (pD3D12Enc->m_transitionsBeforeCloseCmdList.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(pD3D12Enc->m_transitionsBeforeCloseCmdList.size(),
                                                           pD3D12Enc->m_transitionsBeforeCloseCmdList.data());
         pD3D12Enc->m_transitionsBeforeCloseCmdList.clear();
      }

      hr = pD3D12Enc->m_spEncodeCommandList->Close();
      if (FAILED(hr)) {
         D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_flush - Can't close command list with HR %x\n", hr);
      }

      ID3D12CommandList *ppCommandLists[1] = { pD3D12Enc->m_spEncodeCommandList.Get() };
      pD3D12Enc->m_spEncodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
      pD3D12Enc->m_spEncodeCommandQueue->Signal(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue);
      pD3D12Enc->m_spFence->SetEventOnCompletion(pD3D12Enc->m_fenceValue, nullptr);
      D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_flush - ExecuteCommandLists finished on signal with "
                    "fenceValue: %d\n",
                    pD3D12Enc->m_fenceValue);

      hr = pD3D12Enc->m_spCommandAllocator->Reset();
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12CommandAllocator failed with HR %x\n",
            hr);
      }

      hr = pD3D12Enc->m_spEncodeCommandList->Reset(pD3D12Enc->m_spCommandAllocator.Get());
      if (FAILED(hr)) {
         D3D12_LOG_ERROR(
            "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
            hr);
      }

      // Validate device was not removed
      hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_flush - D3D12Device was removed AFTER commandlist "
                         "execution with HR %x, but wasn't before.\n",
                         hr);
      }

      D3D12_LOG_INFO(
         "[d3d12_video_encoder] d3d12_video_encoder_flush - GPU signaled execution finalized for fenceValue: %d\n",
         pD3D12Enc->m_fenceValue);

      pD3D12Enc->m_fenceValue++;
      pD3D12Enc->m_needsGPUFlush = false;
   }
}

/**
 * Destroys a d3d12_video_encoder
 * Call destroy_XX for applicable XX nested member types before deallocating
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of
 * cleanup from failure on the creation method
 */
void
d3d12_video_encoder_destroy(struct pipe_video_codec *codec)
{
   if (codec == nullptr) {
      return;
   }

   d3d12_video_encoder_flush(codec);   // Flush pending work before destroying.

   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;

   // Call d3d12_video_encoder dtor to make ComPtr and other member's destructors work
   delete pD3D12Enc;
}

void
d3d12_video_encoder_update_picparams_tracking(struct d3d12_video_encoder *pD3D12Enc,
                                              struct pipe_video_buffer *  srcTexture,
                                              struct pipe_picture_desc *  picture)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_h264(pD3D12Enc, srcTexture, picture, currentPicParams);
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_update_picparams_tracking",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }

   pD3D12Enc->m_upDPBManager->BeginFrame(currentPicParams);
}

void
d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder *pD3D12Enc,
                                                struct pipe_video_buffer *  srcTexture,
                                                struct pipe_picture_desc *  picture)
{
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);

   bool codecChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_CODEC) != 0);
   bool profileChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_PROFILE) != 0);
   bool levelChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_LEVEL) != 0);
   bool codecConfigChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_CODEC_CONFIG) != 0);
   bool inputFormatChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_INPUT_FORMAT) != 0);
   bool resolutionChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_RESOLUTION) != 0);
   bool rateControlChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_RATE_CONTROL) != 0);
   bool slicesChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_SLICES) != 0);
   bool gopChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_GOP) != 0);
   bool motionPrecisionLimitChanged = ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags &
                                        D3D12_VIDEO_ENCODER_CONFIG_DIRTY_FLAG_MOTION_PRECISION_LIMIT) != 0);

   // Events that that trigger a re-creation of the reference picture manager
   // Stores codec agnostic textures so only input format, resolution and gop (num dpb references) affects this
   if (!pD3D12Enc->m_upDPBManager
       // || codecChanged
       // || profileChanged
       // || levelChanged
       // || codecConfigChanged
       || inputFormatChanged ||
       resolutionChanged
       // || rateControlChanged
       // || slicesChanged
       || gopChanged
       // || motionPrecisionLimitChanged
   ) {
      if (!pD3D12Enc->m_upDPBManager) {
         D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating Reference "
                       "Pictures Manager for the first time\n");
      } else {
         D3D12_LOG_INFO("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating Reference Pictures Manager\n");
      }

      D3D12_RESOURCE_FLAGS resourceAllocFlags =
         D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      bool   fArrayOfTextures = ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) == 0);
      UINT16 texturePoolSize  = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc) +
                               1u;   // adding an extra slot as we also need to count the current frame output recon
                                     // allocation along max reference frame allocations
      if (fArrayOfTextures) {
         pD3D12Enc->m_upDPBStorageManager = std::make_unique<D3D12ArrayOfTexturesDPBManager>(
            texturePoolSize,
            pD3D12Enc->m_pD3D12Screen->dev,
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            (D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
            true,   // setNullSubresourcesOnAllZero - D3D12 Video Encode expects nullptr pSubresources if AoT,
            pD3D12Enc->m_NodeMask);
      } else {
         pD3D12Enc->m_upDPBStorageManager =
            std::make_unique<D3D12TexturesArrayDPBManager>(texturePoolSize,
                                                           pD3D12Enc->m_pD3D12Screen->dev,
                                                           pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                                           pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
                                                           resourceAllocFlags,
                                                           pD3D12Enc->m_NodeMask);
      }
      VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
      d3d12_video_encoder_create_reference_picture_manager(pD3D12Enc);
      VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   }

   bool reCreatedEncoder = false;
   // Events that that trigger a re-creation of the encoder
   if (!pD3D12Enc->m_spVideoEncoder || codecChanged ||
       profileChanged
       // || levelChanged // Only affects encoder heap
       || codecConfigChanged ||
       inputFormatChanged
       // || resolutionChanged // Only affects encoder heap
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/)) ||
       motionPrecisionLimitChanged) {
      if (!pD3D12Enc->m_spVideoEncoder) {
         D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoder for the first time\n");
      } else {
         D3D12_LOG_INFO("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoder\n");
         reCreatedEncoder = true;
      }

      D3D12_VIDEO_ENCODER_DESC encoderDesc = { pD3D12Enc->m_NodeMask,
                                               D3D12_VIDEO_ENCODER_FLAG_NONE,
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                               d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                               d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit };

      // Create encoder
      VERIFY_SUCCEEDED(
         pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoder(&encoderDesc,
                                                             IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoder.GetAddressOf())));
      VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   }

   bool reCreatedEncoderHeap = false;
   // Events that that trigger a re-creation of the encoder heap
   if (!pD3D12Enc->m_spVideoEncoderHeap || codecChanged || profileChanged ||
       levelChanged
       // || codecConfigChanged // Only affects encoder
       || inputFormatChanged   // Might affect internal textures in the heap
       || resolutionChanged
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/))
       // || motionPrecisionLimitChanged // Only affects encoder
   ) {
      if (!pD3D12Enc->m_spVideoEncoderHeap) {
         D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoderHeap for the first time\n");
      } else {
         D3D12_LOG_INFO("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoderHeap\n");
         reCreatedEncoderHeap = true;
      }

      D3D12_VIDEO_ENCODER_HEAP_DESC heapDesc = { pD3D12Enc->m_NodeMask,
                                                 D3D12_VIDEO_ENCODER_HEAP_FLAG_NONE,
                                                 pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                                 d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                                 d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
                                                 // resolution list count
                                                 1,
                                                 // resolution list
                                                 &pD3D12Enc->m_currentEncodeConfig.m_currentResolution };

      // Create encoder heap
      VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoderHeap(
         &heapDesc,
         IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap.GetAddressOf())));
      VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   }

   // If on-the-fly reconfiguration happened without object recreation, set
   // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_*_CHANGED reconfiguration flags in EncodeFrame
   if (rateControlChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
   }

   if (slicesChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_SUBREGION_LAYOUT_CHANGE;
   }

   if (gopChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_GOP_SEQUENCE_CHANGE;
   }
}

void
d3d12_video_encoder_create_reference_picture_manager(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         bool gopHasPFrames =
            (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.PPicturePeriod > 0) &&
            ((pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.GOPLength == 0) ||
             (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.PPicturePeriod <
              pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.GOPLength));

         pD3D12Enc->m_upDPBManager = std::make_unique<D3D12VideoEncoderReferencesManagerH264>(
            gopHasPFrames,
            *pD3D12Enc->m_upDPBStorageManager,
            pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl.MaxL0ReferencesForP,
            pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl.MaxL0ReferencesForB,
            pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl.MaxL1ReferencesForB,
            pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl
               .MaxDPBCapacity   // Max number of frames to be used as a reference, without counting the current picture
                                 // recon picture
         );

         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<D3D12VideoBitstreamBuilderH264>();
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_create_reference_picture_manager",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA
d3d12_video_encoder_get_current_slice_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA subregionData = {};
         if (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode !=
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME) {
            subregionData.pSlicesPartition_H264 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264;
            subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
         }
         return subregionData;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_slice_param_settings",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
d3d12_video_encoder_get_current_picture_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curPicParamsData = {};
         curPicParamsData.pH264PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData);
         return curPicParamsData;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_picture_param_settings",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_RATE_CONTROL
d3d12_video_encoder_get_current_rate_control_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_RATE_CONTROL curRateControlDesc = {};
   curRateControlDesc.Mode            = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode;
   curRateControlDesc.Flags           = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags;
   curRateControlDesc.TargetFrameRate = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate;

   switch (pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_ABSOLUTE_QP_MAP:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CQP = nullptr;
         curRateControlDesc.ConfigParams.DataSize           = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CQP =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_VBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_QVBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_QVBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_QVBR);
      } break;
      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_rate_control_settings",
                                                  "Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE",
                                                  pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode);
      } break;
   }

   return curRateControlDesc;
}

D3D12_VIDEO_ENCODER_LEVEL_SETTING
d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pH264LevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting);
         return curLevelDesc;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_level_desc",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

UINT
d3d12_video_encoder_build_codec_headers(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return d3d12_video_encoder_build_codec_headers_h264(pD3D12Enc);

      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_build_codec_headers", "Unsupported codec", codec);
      } break;
   }
   return 0u;
}

D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE
d3d12_video_encoder_get_current_gop_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pH264GroupOfPictures =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures);
         return curGOPDesc;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_gop_desc",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION
d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pH264Config = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config);
         return codecConfigDesc;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_codec_config_desc",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_PROFILE_DESC
d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pH264Profile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile);
         return curProfDesc;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_profile_desc",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT
d3d12_video_encoder_get_current_picture_control_capabilities_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT curPicCtrlCaps = {};
         curPicCtrlCaps.pH264Support =
            &pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl;
         curPicCtrlCaps.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl);
         return curPicCtrlCaps;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_picture_control_capabilities_desc",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

UINT
d3d12_video_encoder_get_current_max_dpb_capacity(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT curPicCtrlCaps = {};
         curPicCtrlCaps.pH264Support =
            &pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl;
         curPicCtrlCaps.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl);
         return curPicCtrlCaps.pH264Support->MaxDPBCapacity;
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_get_current_max_dpb_capacity",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

void
d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder *pD3D12Enc,
                                                        struct pipe_video_buffer *  srcTexture,
                                                        struct pipe_picture_desc *  picture)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_current_encoder_config_state_h264(pD3D12Enc, srcTexture, picture);
      } break;

      default:
      {
         D3D12_VIDEO_UNSUPPORTED_SWITCH_CASE_FAIL("d3d12_video_encoder_update_current_encoder_config_state",
                                                  "Unsupported codec",
                                                  codec);
      } break;
   }
}

bool
d3d12_video_encoder_create_command_objects(struct d3d12_video_encoder *pD3D12Enc)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE };
   HRESULT                  hr               = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(
      &commandQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Enc->m_spFence));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR(
         "[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
      IID_PPV_ARGS(pD3D12Enc->m_spCommandAllocator.GetAddressOf()));
   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to "
                      "CreateCommandAllocator failed with HR %x\n",
                      hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   hr =
      pD3D12Enc->m_pD3D12Screen->dev->CreateCommandList(0,
                                                        D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                                                        pD3D12Enc->m_spCommandAllocator.Get(),
                                                        nullptr,
                                                        IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   D3D12_COMMAND_QUEUE_DESC copyQueueDesc = { D3D12_COMMAND_LIST_TYPE_COPY };
   hr                                     = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(&copyQueueDesc,
                                                           IID_PPV_ARGS(pD3D12Enc->m_spCopyQueue.GetAddressOf()));

   if (FAILED(hr)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   pD3D12Enc->m_D3D12ResourceCopyHelper.reset(new D3D12ResourceCopyHelper(pD3D12Enc->m_spCopyQueue.Get()));

   return true;
}

struct pipe_video_codec *
d3d12_video_encoder_create_encoder(struct pipe_context *context, const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_encoder
   ///

   struct d3d12_video_encoder *pD3D12Enc =
      new d3d12_video_encoder;   // Not using new doesn't call ctor and the initializations in the class declaration are
                                 // lost
   if (!pD3D12Enc) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Could not allocate memory for "
                      "d3d12_video_encoder\n");
      return nullptr;
   }

   pD3D12Enc->base         = *codec;
   pD3D12Enc->m_screen     = context->screen;
   pD3D12Enc->base.context = context;
   pD3D12Enc->base.width   = codec->width;
   pD3D12Enc->base.height  = codec->height;
   // Only fill methods that are supported by the d3d12 encoder, leaving null the rest (ie. encode_* / encode_macroblock)
   pD3D12Enc->base.destroy          = d3d12_video_encoder_destroy;
   pD3D12Enc->base.begin_frame      = d3d12_video_encoder_begin_frame;
   pD3D12Enc->base.encode_bitstream = d3d12_video_encoder_encode_bitstream;
   pD3D12Enc->base.end_frame        = d3d12_video_encoder_end_frame;
   pD3D12Enc->base.flush            = d3d12_video_encoder_flush;
   pD3D12Enc->base.get_feedback     = d3d12_video_encoder_get_feedback;

   struct d3d12_context *pD3D12Ctx = (struct d3d12_context *) context;
   pD3D12Enc->m_pD3D12Screen       = d3d12_screen(pD3D12Ctx->base.screen);

   if (FAILED(pD3D12Enc->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(pD3D12Enc->m_spD3D12VideoDevice.GetAddressOf())))) {
      D3D12_LOG_ERROR(
         "[d3d12_video_encoder] d3d12_video_encoder_create_encoder - D3D12 Device has no Video encode support\n");
      goto failed;
   }

   if (!d3d12_video_encoder_create_command_objects(pD3D12Enc)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Failure on "
                      "d3d12_video_encoder_create_command_objects\n");
      goto failed;
   }

   return &pD3D12Enc->base;

failed:
   if (pD3D12Enc != nullptr) {
      d3d12_video_encoder_destroy((struct pipe_video_codec *) pD3D12Enc);
   }

   return nullptr;
}

void
d3d12_video_encoder_prepare_output_buffers(struct d3d12_video_encoder *pD3D12Enc,
                                           struct pipe_video_buffer *  srcTexture,
                                           struct pipe_picture_desc *  picture)
{
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.NodeIndex = pD3D12Enc->m_NodeIndex;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Codec =
      pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Profile =
      d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.InputFormat =
      pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.PictureTargetResolution =
      pD3D12Enc->m_currentEncodeConfig.m_currentResolution;

   VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(
      D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS,
      &pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps,
      sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps)));
   if (!pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.IsSupported) {
      D3D12_LOG_ERROR(
         "[d3d12_video_encoder] D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS arguments are not supported.\n");
   }

   d3d12_video_encoder_calculate_metadata_resolved_buffer_size(
      pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput,
      pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize);

   D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
   if ((pD3D12Enc->m_spResolvedMetadataBuffer == nullptr) ||
       (pD3D12Enc->m_spResolvedMetadataBuffer->GetDesc().Width <
        pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize)) {
      CD3DX12_RESOURCE_DESC resolvedMetadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize);
      VERIFY_SUCCEEDED(pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &resolvedMetadataBufferDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spResolvedMetadataBuffer.GetAddressOf())));
   }

   if ((pD3D12Enc->m_spMetadataOutputBuffer == nullptr) ||
       (pD3D12Enc->m_spMetadataOutputBuffer->GetDesc().Width <
        pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize)) {
      CD3DX12_RESOURCE_DESC metadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize);
      VERIFY_SUCCEEDED(pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &metadataBufferDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spMetadataOutputBuffer.GetAddressOf())));
   }
}

bool
d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder *pD3D12Enc,
                                        struct pipe_video_buffer *  srcTexture,
                                        struct pipe_picture_desc *  picture)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   d3d12_video_encoder_update_current_encoder_config_state(pD3D12Enc, srcTexture, picture);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   d3d12_video_encoder_reconfigure_encoder_objects(pD3D12Enc, srcTexture, picture);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   d3d12_video_encoder_update_picparams_tracking(pD3D12Enc, srcTexture, picture);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   d3d12_video_encoder_prepare_output_buffers(pD3D12Enc, srcTexture, picture);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   return true;
}

/**
 * start encoding of a new frame
 */
void
d3d12_video_encoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on encoder creation, re-config (if any) happens in
   // d3d12_video_encoder_encode_bitstream
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_begin_frame started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);

   if (pD3D12Enc->m_numNestedBeginFrame > 0) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] Nested d3d12_video_encoder_begin_frame calls are not supported. Call "
                      "d3d12_video_encoder_end_frame to finalize current frame before calling "
                      "d3d12_video_encoder_begin_frame again.\n");
   }

   pD3D12Enc->m_numNestedBeginFrame++;

   ///
   /// Caps check
   ///
   int capsResult = d3d12_screen_get_video_param(&pD3D12Enc->m_pD3D12Screen->base,
                                                 codec->profile,
                                                 PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                 PIPE_VIDEO_CAP_SUPPORTED);
   if (capsResult == 0) {
      D3D12_LOG_ERROR(
         "[d3d12_video_encoder] d3d12_video_encoder_begin_frame - d3d12_screen_get_video_param returned no support.\n");
   }

   if (!d3d12_video_encoder_reconfigure_session(pD3D12Enc, target, picture)) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_begin_frame - Failure on "
                      "d3d12_video_encoder_reconfigure_session\n");
   }

   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_begin_frame finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
}

void
d3d12_video_encoder_calculate_metadata_resolved_buffer_size(UINT maxSliceNumber, size_t &bufferSize)
{
   bufferSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA) +
                (maxSliceNumber * sizeof(D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA));
}

// Returns the number of slices that the output will contain for fixed slicing modes
// and the maximum number of slices the output might contain for dynamic slicing modes (eg. max bytes per slice)
UINT
d3d12_video_encoder_calculate_max_slices_count_in_output(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE                          slicesMode,
   const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES *slicesConfig,
   UINT                                                                     MaxSubregionsNumberFromCaps,
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC                              sequenceTargetResolution,
   UINT                                                                     SubregionBlockPixelsSize)
{
   UINT pic_width_in_subregion_units =
      static_cast<UINT>(std::ceil(sequenceTargetResolution.Width / static_cast<double>(SubregionBlockPixelsSize)));
   UINT pic_height_in_subregion_units =
      static_cast<UINT>(std::ceil(sequenceTargetResolution.Height / static_cast<double>(SubregionBlockPixelsSize)));
   UINT total_picture_subregion_units = pic_width_in_subregion_units * pic_height_in_subregion_units;
   UINT maxSlices                     = 0u;
   switch (slicesMode) {
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME:
      {
         maxSlices = 1u;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION:
      {
         maxSlices = MaxSubregionsNumberFromCaps;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED:
      {
         maxSlices = static_cast<UINT>(
            std::ceil(total_picture_subregion_units / static_cast<double>(slicesConfig->NumberOfCodingUnitsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION:
      {
         maxSlices = static_cast<UINT>(
            std::ceil(pic_height_in_subregion_units / static_cast<double>(slicesConfig->NumberOfRowsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME:
      {
         maxSlices = slicesConfig->NumberOfSlicesPerFrame;
      } break;
      default:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_calculate_max_slices_count_in_output - the slice "
                         "pattern mode %d is unknown\n",
                         slicesMode);
      } break;
   }

   return maxSlices;
}

/**
 * encode a bitstream
 */
void
d3d12_video_encoder_encode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *source,
                                     struct pipe_resource *    destination,
                                     void **                   feedback)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);
   assert(pD3D12Enc->m_pD3D12Screen);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);

   struct d3d12_video_buffer *pInputVideoBuffer = (struct d3d12_video_buffer *) source;
   assert(pInputVideoBuffer);
   ID3D12Resource *pInputVideoD3D12Res        = d3d12_resource_resource(pInputVideoBuffer->m_pD3D12Resource);
   UINT            inputVideoD3D12Subresource = 0u;

   struct d3d12_resource *pOutputBitstreamBuffer = (struct d3d12_resource *) destination;
   assert(pOutputBitstreamBuffer);
   ID3D12Resource *pOutputBufferD3D12Res = d3d12_resource_resource(pOutputBitstreamBuffer);

   if (pD3D12Enc->m_numConsecutiveEncodeFrame > 0) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] Nested d3d12_video_encoder_encode_bitstream calls are not supported. Call "
                      "d3d12_video_encoder_end_frame to finalize current frame before calling "
                      "d3d12_video_encoder_encode_bitstream again.\n");
   }

   pD3D12Enc->m_numConsecutiveEncodeFrame++;

   ///
   /// Record Encode operation
   ///

   std::vector<D3D12_RESOURCE_BARRIER> rgCurrentFrameStateTransitions = {
      CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(pOutputBufferD3D12Res,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE)
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(rgCurrentFrameStateTransitions.size(),
                                                     rgCurrentFrameStateTransitions.data());

   D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE reconPicOutputTextureDesc =
      pD3D12Enc->m_upDPBManager->GetCurrentFrameReconPicOutputAllocation();
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES referenceFramesDescriptor =
      pD3D12Enc->m_upDPBManager->GetCurrentFrameReferenceFrames();
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS picCtrlFlags = D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_NONE;

   // Transition DPB reference pictures to read mode
   // TODO: D3D12DecomposeSubresource in all transitions, take TextureArray case into account too. For resources managed
   // by pipe/mesa (ie. not DPB resources or staging frame textures), do we want to use d3d12_transition_resource_state
   // from d3d12_context?
   UINT                                maxReferences = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);
   std::vector<D3D12_RESOURCE_BARRIER> rgReferenceTransitions(maxReferences);
   if ((referenceFramesDescriptor.NumTexture2Ds > 0) || (pD3D12Enc->m_upDPBManager->IsCurrentFrameUsedAsReference())) {
      rgReferenceTransitions.clear();
      rgReferenceTransitions.reserve(maxReferences);
      // Check if array of textures vs texture array
      if (referenceFramesDescriptor.pSubresources == nullptr) {
         // Array of resources mode
         // Transition all subresources of each resource
         for (UINT referenceIdx = 0; referenceIdx < referenceFramesDescriptor.NumTexture2Ds; referenceIdx++) {
            rgReferenceTransitions.push_back(
               CD3DX12_RESOURCE_BARRIER::Transition(referenceFramesDescriptor.ppTexture2Ds[referenceIdx],
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
         }
      } else if (referenceFramesDescriptor.NumTexture2Ds > 0) {
         // texture array mode
         // Transition all subresources of the first resource containing subresources per each ref pic
         rgReferenceTransitions.push_back(
            CD3DX12_RESOURCE_BARRIER::Transition(referenceFramesDescriptor.ppTexture2Ds[0],
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
      }

      if (reconPicOutputTextureDesc.pReconstructedPicture != nullptr) {
         picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE;

         rgReferenceTransitions.push_back(
            CD3DX12_RESOURCE_BARRIER::Transition(reconPicOutputTextureDesc.pReconstructedPicture,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
      }

      if (rgReferenceTransitions.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<UINT>(rgReferenceTransitions.size()),
                                                           rgReferenceTransitions.data());
      }
   }

   // Update current frame pic params state after reconfiguring above.
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);
   pD3D12Enc->m_upDPBManager->GetCurrentFramePictureControlData(currentPicParams);

   UINT prefixGeneratedHeadersByteSize = d3d12_video_encoder_build_codec_headers(pD3D12Enc);

   const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS inputStreamArguments = {
      // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC
      { // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS
        pD3D12Enc->m_currentEncodeConfig.m_seqFlags,
        // D3D12_VIDEO_ENCODER_INTRA_REFRESH
        pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh,
        d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc),
        // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
        pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
        pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
        d3d12_video_encoder_get_current_slice_param_settings(pD3D12Enc),
        d3d12_video_encoder_get_current_gop_desc(pD3D12Enc) },
      // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC
      { // UINT IntraRefreshFrameIndex;
        pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex,
        // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS Flags;
        picCtrlFlags,
        // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA PictureControlCodecData;
        currentPicParams,
        // D3D12_VIDEO_ENCODE_REFERENCE_FRAMES ReferenceFrames;
        referenceFramesDescriptor },
      pInputVideoD3D12Res,
      inputVideoD3D12Subresource,
      prefixGeneratedHeadersByteSize   // hint for driver to know header size in final bitstream for rate control internal
      // budgeting. - User can also calculate headers fixed size beforehand (eg. no VUI,
      // etc) and build them with final values after EncodeFrame is executed
   };

   const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS outputStreamArguments = {
      // D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM
      {
         pOutputBufferD3D12Res,
         prefixGeneratedHeadersByteSize,   // Start writing after the reserved interval [0,
                                           // prefixGeneratedHeadersByteSize) for bitstream headers
      },
      // D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE
      reconPicOutputTextureDesc,
      // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER
      { pD3D12Enc->m_spMetadataOutputBuffer.Get(), 0 }
   };

   // Record EncodeFrame
   pD3D12Enc->m_spEncodeCommandList->EncodeFrame(pD3D12Enc->m_spVideoEncoder.Get(),
                                                 pD3D12Enc->m_spVideoEncoderHeap.Get(),
                                                 &inputStreamArguments,
                                                 &outputStreamArguments);

   // Upload the CPU buffers with the bitstream headers to the compressed bitstream resource in the interval [0,
   // prefixGeneratedHeadersByteSize)
   assert(prefixGeneratedHeadersByteSize == pD3D12Enc->m_BitstreamHeadersBuffer.size());
   pD3D12Enc->m_D3D12ResourceCopyHelper->UploadData(pOutputBufferD3D12Res,
                                                    0,
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    pD3D12Enc->m_BitstreamHeadersBuffer.data(),
                                                    pD3D12Enc->m_BitstreamHeadersBuffer.size(),
                                                    pD3D12Enc->m_BitstreamHeadersBuffer.size());

   D3D12_RESOURCE_BARRIER rgResolveMetadataStateTransitions[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON)
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(_countof(rgResolveMetadataStateTransitions),
                                                     rgResolveMetadataStateTransitions);

   const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS inputMetadataCmd = {
      pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
      d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
      pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
      // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
      pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
      { pD3D12Enc->m_spMetadataOutputBuffer.Get(), 0 }
   };

   const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS outputMetadataCmd = {
      { pD3D12Enc->m_spResolvedMetadataBuffer.Get(), 0 }
   };
   pD3D12Enc->m_spEncodeCommandList->ResolveEncoderOutputMetadata(&inputMetadataCmd, &outputMetadataCmd);

   // Transition DPB reference pictures back to COMMON
   if ((referenceFramesDescriptor.NumTexture2Ds > 0) || (pD3D12Enc->m_upDPBManager->IsCurrentFrameUsedAsReference())) {
      for (auto &BarrierDesc : rgReferenceTransitions) {
         std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      }

      if (rgReferenceTransitions.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<UINT>(rgReferenceTransitions.size()),
                                                           rgReferenceTransitions.data());
      }
   }

   D3D12_RESOURCE_BARRIER rgRevertResolveMetadataStateTransitions[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                           D3D12_RESOURCE_STATE_COMMON),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON),
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(_countof(rgRevertResolveMetadataStateTransitions),
                                                     rgRevertResolveMetadataStateTransitions);

   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
}

void
d3d12_video_encoder_get_feedback(struct pipe_video_codec *codec, void *feedback, unsigned *size)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);

   if (pD3D12Enc->m_needsGPUFlush) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] d3d12_video_encoder_get_feedback - Cannot get feedback with outstanding "
                      "GPU buffers. Call d3d12_video_encoder_flush and try again.\n");
   }

   D3D12_VIDEO_ENCODER_OUTPUT_METADATA                       encoderMetadata;
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> pSubregionsMetadata;
   d3d12_video_encoder_extract_encode_metadata(
      pD3D12Enc,
      pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
      pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize,
      encoderMetadata,
      pSubregionsMetadata);

   // Read metadata from encoderMetadata
   if (encoderMetadata.EncodeErrorFlags != D3D12_VIDEO_ENCODER_ENCODE_ERROR_FLAG_NO_ERROR) {
      D3D12_LOG_ERROR("[d3d12_video_encoder] Encode GPU command failed - EncodeErrorFlags: %ld\n",
                      encoderMetadata.EncodeErrorFlags);
   }

   assert(encoderMetadata.EncodedBitstreamWrittenBytesCount > 0u);
   *size = (pD3D12Enc->m_BitstreamHeadersBuffer.size() + encoderMetadata.EncodedBitstreamWrittenBytesCount);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
}

void
d3d12_video_encoder_extract_encode_metadata(
   struct d3d12_video_encoder *                               pD3D12Enc,
   ID3D12Resource *                                           pResolvedMetadataBuffer,   // input
   size_t                                                     resourceMetadataSize,      // input
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA &                      parsedMetadata,            // output
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> &pSubregionsMetadata        // output
)
{
   std::vector<uint8_t> pTmp(resourceMetadataSize);
   uint8_t *            pMetadataBufferSrc = pTmp.data();
   pD3D12Enc->m_D3D12ResourceCopyHelper->ReadbackData(pMetadataBufferSrc,
                                                      resourceMetadataSize,
                                                      resourceMetadataSize,
                                                      pResolvedMetadataBuffer,
                                                      0,
                                                      D3D12_RESOURCE_STATE_COMMON);

   // Clear output
   memset(&parsedMetadata, 0, sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA));

   // Calculate sizes
   size_t encoderMetadataSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA);

   // Copy buffer to the appropriate D3D12_VIDEO_ENCODER_OUTPUT_METADATA memory layout
   parsedMetadata = *reinterpret_cast<D3D12_VIDEO_ENCODER_OUTPUT_METADATA *>(pMetadataBufferSrc);

   // As specified in D3D12 Encode spec, the array base for metadata for the slices
   // (D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA[]) is placed in memory immediately after the
   // D3D12_VIDEO_ENCODER_OUTPUT_METADATA structure
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *pFrameSubregionMetadata =
      reinterpret_cast<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *>(reinterpret_cast<BYTE *>(pMetadataBufferSrc) +
                                                                       encoderMetadataSize);

   // Copy fields into D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA
   assert(parsedMetadata.WrittenSubregionsCount < SIZE_MAX);
   pSubregionsMetadata.resize(static_cast<size_t>(parsedMetadata.WrittenSubregionsCount));
   for (UINT sliceIdx = 0; sliceIdx < parsedMetadata.WrittenSubregionsCount; sliceIdx++) {
      pSubregionsMetadata[sliceIdx].bHeaderSize  = pFrameSubregionMetadata[sliceIdx].bHeaderSize;
      pSubregionsMetadata[sliceIdx].bSize        = pFrameSubregionMetadata[sliceIdx].bSize;
      pSubregionsMetadata[sliceIdx].bStartOffset = pFrameSubregionMetadata[sliceIdx].bStartOffset;
   }
}

/**
 * end encoding of the current frame
 */
void
d3d12_video_encoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_end_frame started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);

   // Signal finish of current frame encoding to the picture management tracker
   pD3D12Enc->m_upDPBManager->EndFrame();

   // Reset encode_frame counter at end_frame call
   pD3D12Enc->m_numConsecutiveEncodeFrame = 0;

   // Decrement begin_frame counter at end_frame call
   pD3D12Enc->m_numNestedBeginFrame--;

   D3D12_LOG_DBG("[d3d12_video_encoder] d3d12_video_encoder_end_frame finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);

   ///
   /// Flush work to the GPU and blocking wait until encode finishes
   ///
   pD3D12Enc->m_needsGPUFlush = true;
   d3d12_video_encoder_flush(codec);
   VERIFY_DEVICE_NOT_REMOVED(pD3D12Enc);
}

#pragma GCC diagnostic pop
