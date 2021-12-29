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

#include "d3d12_video_enc.h"
#include "d3d12_video_enc_h264.h"
#include "util/u_video.h"
#include "d3d12_screen.h"

void d3d12_video_encoder_update_current_rate_control_h264(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc = { };
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Numerator = picture->rate_ctrl[0].frame_rate_num;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Denominator = picture->rate_ctrl[0].frame_rate_den;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags = D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_NONE;

   switch (picture->rate_ctrl[0].rate_ctrl_method) 
   {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.TargetAvgBitRate = picture->rate_ctrl[0].target_bitrate;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.PeakBitRate = picture->rate_ctrl[0].peak_bitrate;

         {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags | D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.VBVCapacity = picture->rate_ctrl[0].vbv_buffer_size;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.InitialVBVFullness = picture->rate_ctrl[0].vbv_buf_lv;
         }

         {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags | D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.MaxFrameBitSize = picture->rate_ctrl[0].peak_bits_picture_integer;
            
            if(picture->rate_ctrl[0].peak_bits_picture_fraction > 0) // Round bit up as we don't have fractional bit parameter
            {
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.MaxFrameBitSize++;
            }
         }
      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.TargetBitRate = picture->rate_ctrl[0].target_bitrate;

         {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags | D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.VBVCapacity = picture->rate_ctrl[0].vbv_buffer_size;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.InitialVBVFullness = picture->rate_ctrl[0].vbv_buf_lv;
         }

         {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags | D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.MaxFrameBitSize = picture->rate_ctrl[0].peak_bits_picture_integer;
            
            if(picture->rate_ctrl[0].peak_bits_picture_fraction > 0) // Round bit up as we don't have fractional bit parameter
            {
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.MaxFrameBitSize++;
            }
         }
      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_FullIntracodedFrame = picture->quant_i_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_PrevRefOnly = picture->quant_p_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_BiDirectionalRef = picture->quant_b_frames;
      } break;
      default:
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] d3d12_video_encoder_update_current_rate_control_h264 invalid RC config, using default RC CQP mode\n");
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_FullIntracodedFrame = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_PrevRefOnly = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_BiDirectionalRef = 30;
      } break;
   }
}

D3D12VideoEncoderH264FrameDesc d3d12_video_encoder_convert_current_frame_gop_info_h264(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   struct pipe_h264_enc_picture_desc *h264Pic = (struct pipe_h264_enc_picture_desc *)picture;

   D3D12VideoEncoderH264FrameDesc ret = {
      h264Pic->idr_pic_id,
      d3d12_video_encoder_convert_frame_type(h264Pic->picture_type),
      h264Pic->pic_order_cnt,
      h264Pic->frame_num_cnt,
   };

   return ret;
}

D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 d3d12_video_encoder_convert_frame_type(enum pipe_h2645_enc_picture_type picType)
{
   switch (picType) 
   {
      case PIPE_H2645_ENC_PICTURE_TYPE_P:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_B:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_I:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264] PIPE_H2645_ENC_PICTURE_TYPE_SKIP not supported.\n");
      } break;
      default:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264] d3d12_video_encoder_convert_frame_type - Invalid pipe_h2645_enc_picture_type %d.\n", picType);
      } break;
   }
}

void d3d12_video_encoder_update_current_h264_slices_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   // There's no config filled for this from above layers, so default for now.
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264 = { }; 
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264.NumberOfSlicesPerFrame = 4;
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
}

D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE d3d12_video_encoder_convert_h264_motion_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   return D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM;
}

D3D12_VIDEO_ENCODER_LEVELS_H264 d3d12_video_encoder_convert_level_h264(UINT h264SpecLevel)
{
   switch(h264SpecLevel)
   {
      case 10:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_1;
      } break;
      case 11:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_11;
      } break;
      case 12:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_12;
      } break;
      case 13:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_13;
      } break;
      case 20:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_2;
      } break;
      case 21:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_21;
      } break;
      case 22:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_22;
      } break;
      case 30:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_3;
      } break;
      case 31:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_31;
      } break;
      case 32:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_32;
      } break;
      case 40:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_4;
      } break;
      case 41:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_41;
      } break;
      case 42:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_42;
      } break;
      case 50:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_5;
      } break;
      case 51:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_51;
      } break;
      case 52:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_52;
      } break;
      case 60:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_6;
      } break;
      case 61:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_61;
      } break;
      case 62:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_62;
      } break;
      default:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264] d3d12_video_encoder_convert_level_h264 - Unsupported level\n");
      } break;            
   }
}

void d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12, UINT &specLevel, UINT &constraint_set3_flag)
{
    specLevel = 0;
    constraint_set3_flag = 0;

    switch(level12)
    {
        case D3D12_VIDEO_ENCODER_LEVELS_H264_1:
        {
            specLevel = 10;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_1b:
        {
            specLevel = 11;
            constraint_set3_flag = 1;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_11:
        {
            specLevel = 11;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_12:
        {
            specLevel = 12;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_13:
        {
            specLevel = 13;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_2:
        {
            specLevel = 20;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_21:
        {
            specLevel = 21;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_22:
        {
            specLevel = 22;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_3:
        {
            specLevel = 30;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_31:
        {
            specLevel = 31;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_32:
        {
            specLevel = 32;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_4:
        {
            specLevel = 40;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_41:
        {
            specLevel = 41;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_42:
        {
            specLevel = 42;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_5:
        {
            specLevel = 50;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_51:
        {
            specLevel = 51;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_52:
        {
            specLevel = 52;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_6:
        {
            specLevel = 60;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_61:
        {
            specLevel = 61;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_62:
        {
            specLevel = 62;
        } break;
        default:
        {
            D3D12_LOG_ERROR("[d3d12_video_encoder_h264] d3d12_video_encoder_h264 level\n"); // Not a supported level
        } break;            
    }
}

bool d3d12_video_encoder_is_gop_supported(UINT GOPLength, UINT PPicturePeriod, UINT MaxDPBCapacity, UINT MaxL0ReferencesForP, UINT MaxL0ReferencesForB, UINT MaxL1ReferencesForB)
{
    bool bSupportedGOP = true;
    bool gopHasPFrames = (PPicturePeriod > 0) && ((GOPLength == 0) || (PPicturePeriod < GOPLength));
    bool gopHasBFrames = (PPicturePeriod > 1);    

    if(gopHasPFrames && (MaxL0ReferencesForP == 0))
    {
        D3D12_LOG_DBG("[d3d12_video_encoder_h264] GOP not supported based on HW capabilities - Reason: no P frames support - GOP Length: %d GOP PPicPeriod: %d\n", GOPLength, PPicturePeriod);
        bSupportedGOP = false;
    }

    if(gopHasBFrames && ((MaxL0ReferencesForB + MaxL1ReferencesForB) == 0))
    {
        D3D12_LOG_DBG("[d3d12_video_encoder_h264] GOP not supported based on HW capabilities - Reason: no B frames support - GOP Length: %d GOP PPicPeriod: %d\n", GOPLength, PPicturePeriod);
        bSupportedGOP = false;
    }

    if(gopHasPFrames && !gopHasBFrames && (MaxL0ReferencesForP < MaxDPBCapacity))
    {
        D3D12_LOG_DBG("[d3d12_video_encoder_h264] MaxL0ReferencesForP must be equal or higher than the reported MaxDPBCapacity -- P frames should be able to address all the DPB unique indices at least once\n");
        bSupportedGOP = false;
    }

    if(gopHasPFrames && gopHasBFrames && ((MaxL0ReferencesForB + MaxL1ReferencesForB) < MaxDPBCapacity))
    {
        D3D12_LOG_DBG("[d3d12_video_encoder_h264] Insufficient L0 and L1 lists size to address all the unique ref pic indices reported by MaxDPBCapacity\n");
        bSupportedGOP = false;
    }
    
    return bSupportedGOP;
}

void d3d12_video_encoder_update_h264_gop_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   // Only update GOP when it begins
   if (picture->gop_cnt == 1)
   {
      UINT GOPCoeff = picture->i_remain;
      UINT GOPLength = picture->gop_size/GOPCoeff;
      UINT PPicturePeriod = std::ceil(GOPLength / (double) (picture->p_remain/GOPCoeff)) - 1;

      bool gopHasPFrames = (PPicturePeriod > 0) && ((GOPLength == 0) || (PPicturePeriod < GOPLength));
      bool gopHasBFrames = (PPicturePeriod > 1);
      if(!gopHasPFrames && !gopHasBFrames)
      {
         assert(picture->pic_order_cnt_type == 2u);
         // I Frame only
      } else if (gopHasPFrames && !gopHasBFrames)
      {
         // I and P only
         // assert(picture->pic_order_cnt_type == 2u);
         // TODO: Some drivers expect pic_order_cnt_type = 2 here but upper layer is sending 0
         picture->pic_order_cnt_type = 2u;
      } else
      {
         // I, P and B frames
         assert(picture->pic_order_cnt_type == 0);
      }

      assert(picture->pic_order_cnt_type != 1); // Not supported by D3D12 Encode.

      const UINT max_pic_order_cnt_lsb = (GOPLength > 0) ? 256u : 16384u;
      const UINT max_max_frame_num = (GOPLength > 0) ? 256u : 16384u;
      double log2_max_frame_num_minus4 = std::max(0.0, std::ceil(std::log2(max_max_frame_num)) - 4);
      double log2_max_pic_order_cnt_lsb_minus4 = std::max(0.0, std::ceil(std::log2(max_pic_order_cnt_lsb)) - 4);
      assert(log2_max_frame_num_minus4 < UCHAR_MAX);
      assert(log2_max_pic_order_cnt_lsb_minus4 < UCHAR_MAX);   
      assert(picture->pic_order_cnt_type < UCHAR_MAX);   

      pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures =
      {
         GOPLength,
         PPicturePeriod,
         static_cast<UCHAR>(picture->pic_order_cnt_type),
         static_cast<UCHAR>(log2_max_frame_num_minus4),
         static_cast<UCHAR>(log2_max_pic_order_cnt_lsb_minus4)
      };;

      ///
      /// Cache caps in pD3D12Enc
      ///

      D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT inOutPicCtrlCodecData = { };
      inOutPicCtrlCodecData.pH264Support = &pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl;
      inOutPicCtrlCodecData.DataSize = sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl);
      D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT capPictureControlData = 
      {
         pD3D12Enc->m_NodeIndex,
         D3D12_VIDEO_ENCODER_CODEC_H264,
         d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
         false,
         inOutPicCtrlCodecData
      };

      VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT, &capPictureControlData, sizeof(capPictureControlData)));
      assert(capPictureControlData.IsSupported);

      // Calculate the DPB size for this session based on
      // 1. Driver reported caps
      // 2. the GOP type and L0/L1 usage based on this
      // h264PicCtrlData will contain the adjusted values to be used later
      // for allocations such as the DPB resource pool
      auto& h264PicCtrlData = pD3D12Enc->m_currentEncodeCapabilities.m_PictureControlCapabilities.m_H264PictureControl;
      if(!gopHasPFrames && !gopHasBFrames)
      {            
         // I Frame only
         // Will store 0 previous frames
         h264PicCtrlData.MaxDPBCapacity = 0u;
      } 
      else if (gopHasPFrames && !gopHasBFrames)
      {
         // I and P only

         // Check if underlying HW supports the GOP
         if(h264PicCtrlData.MaxL0ReferencesForP == 0)
         {
               D3D12_LOG_ERROR("[d3d12_video_encoder_h264] D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT doesn't support P frames and the GOP has P frames.\n");
               return;
         }

         if(GOPLength > 0) // GOP is closed with periodic I/IDR frames (eg. no infinite gop)
         {
               // Will store only GOPLength - 1 inter frames in between IDRs as maximum, as we won't need all the MaxL0ReferencesForB slots even when the driver reports it.
               h264PicCtrlData.MaxDPBCapacity = std::min(h264PicCtrlData.MaxL0ReferencesForP, (GOPLength - 1));
         }
         else
         {
               // Will store only MaxL0ReferencesForP frames as there're no B frames.
               h264PicCtrlData.MaxDPBCapacity = h264PicCtrlData.MaxL0ReferencesForP;
         }
      } 
      else
      {
         // I, P and B frames

         // Check if underlying HW supports the GOP for
         if(gopHasPFrames && (h264PicCtrlData.MaxL0ReferencesForP == 0))
         {
               D3D12_LOG_ERROR("[d3d12_video_encoder_h264] D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT doesn't support P frames and the GOP has P and B frames.\n");
               return;
         }

         if(gopHasBFrames && ((h264PicCtrlData.MaxL0ReferencesForB + h264PicCtrlData.MaxL1ReferencesForB) == 0))
         {
               D3D12_LOG_ERROR("[d3d12_video_encoder_h264] D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT doesn't support B frames and the GOP has P and B frames.\n");
               return;
         }

         if(GOPLength > 0) // GOP is closed with periodic I/IDR frames (eg. no infinite gop)
         {
               // Will store only GOPLength - 1 inter frames in between IDRs as maximum, as we won't need the h264PicCtrlData.MaxDPBCapacity slots even when the driver reports it.
               h264PicCtrlData.MaxDPBCapacity = std::min(h264PicCtrlData.MaxDPBCapacity, (GOPLength - 1));
         }
         // else
         // {
         //     // Will store the full h264PicCtrlData.MaxDPBCapacity capacity and then L0/L1 lists will be created based on MaxL0ReferencesForP/MaxL0ReferencesForB/MaxL1ReferencesForB
         // }
      }

      if(!d3d12_video_encoder_is_gop_supported(GOPLength, PPicturePeriod, h264PicCtrlData.MaxDPBCapacity, h264PicCtrlData.MaxL0ReferencesForP, h264PicCtrlData.MaxL0ReferencesForB, h264PicCtrlData.MaxL1ReferencesForB))
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264] Invalid or unsupported GOP \n");
      }
   }
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 d3d12_video_encoder_convert_h264_codec_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 config = 
   {
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_NONE,
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_DIRECT_MODES_DISABLED,
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_SLICES_DEBLOCKING_MODE_0_ALL_LUMA_CHROMA_SLICE_BLOCK_EDGES_ALWAYS_FILTERED,
   };

   if(picture->pic_ctrl.enc_cabac_enable)
   {
      config.ConfigurationFlags | D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_ENABLE_CABAC_ENCODING;
   }

   return config;
}

void d3d12_video_encoder_update_current_encoder_config_state_h264(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   struct pipe_h264_enc_picture_desc *h264Pic = (struct pipe_h264_enc_picture_desc *)picture;

   // Set requested config
   pD3D12Enc->m_currentEncodeConfig.m_currentRequestedConfig = *h264Pic;

   // Set codec
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc = D3D12_VIDEO_ENCODER_CODEC_H264;

   // Set input format
   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo = { };
   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format = D3D12VideoFormatHelper::d3d12_convert_pipe_video_profile_to_dxgi_format(pD3D12Enc->base.profile);
   VERIFY_SUCCEEDED(pD3D12Enc->m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo, sizeof(pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo)));

   // Set resolution
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width = srcTexture->width;
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height = srcTexture->height;

   // Set profile
   pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile = d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(pD3D12Enc->base.profile);

   // Set level
   pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting = d3d12_video_encoder_convert_level_h264(pD3D12Enc->base.level);

   // Set codec config
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config = d3d12_video_encoder_convert_h264_codec_configuration(pD3D12Enc, h264Pic);

   // Set rate control
   d3d12_video_encoder_update_current_rate_control_h264(pD3D12Enc, h264Pic);

   // Set slices config
   d3d12_video_encoder_update_current_h264_slices_configuration(pD3D12Enc, h264Pic);

   // Set GOP config
   d3d12_video_encoder_update_h264_gop_configuration(pD3D12Enc, h264Pic);
   
   // m_currentEncodeConfig.m_encoderPicParamsDesc pic params are set in d3d12_video_encoder_reconfigure_encoder_objects after re-allocating objects if needed

   // Set motion estimation config
   pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit = d3d12_video_encoder_convert_h264_motion_configuration(pD3D12Enc, h264Pic);   

   ///
   /// Check for video encode support detailed capabilities
   ///

   D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT capEncoderSupportData = { };
   capEncoderSupportData.NodeIndex = pD3D12Enc->m_NodeIndex;
   capEncoderSupportData.Codec = D3D12_VIDEO_ENCODER_CODEC_H264;
   capEncoderSupportData.InputFormat = pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   capEncoderSupportData.RateControl = d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc);
   capEncoderSupportData.IntraRefresh = pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh.Mode;
   capEncoderSupportData.SubregionFrameEncoding = pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode;
   capEncoderSupportData.ResolutionsListCount = 1;
   capEncoderSupportData.pResolutionList = &pD3D12Enc->m_currentEncodeConfig.m_currentResolution;
   capEncoderSupportData.CodecGopSequence = d3d12_video_encoder_get_current_gop_desc(pD3D12Enc);
   capEncoderSupportData.MaxReferenceFramesInDPB = pD3D12Enc->base.max_references; // Max number of frames to be used as a reference, without counting the current picture recon picture
   capEncoderSupportData.CodecConfiguration = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);

   D3D12_VIDEO_ENCODER_PROFILE_H264 suggestedProfileH264 = { };
   D3D12_VIDEO_ENCODER_LEVELS_H264 suggestedLevelH264 = { };
   capEncoderSupportData.SuggestedProfile.pH264Profile = &suggestedProfileH264;
   capEncoderSupportData.SuggestedProfile.DataSize = sizeof(suggestedProfileH264);
   capEncoderSupportData.SuggestedLevel.pH264LevelSetting = &suggestedLevelH264;
   capEncoderSupportData.SuggestedLevel.DataSize = sizeof(suggestedLevelH264);

   // prepare inout storage for the resolution dependent result.
   capEncoderSupportData.pResolutionDependentSupport = &pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps;

   VERIFY_SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT, &capEncoderSupportData, sizeof(capEncoderSupportData)));
   
   pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags = capEncoderSupportData.SupportFlags;
   pD3D12Enc->m_currentEncodeCapabilities.m_ValidationFlags = capEncoderSupportData.ValidationFlags;

   // Check for general support and cache caps results
   if((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) == 0)
   {
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested codec is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RESOLUTION_NOT_SUPPORTED_IN_LIST) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested resolution is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_CONFIGURATION_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested bitrate or rc config is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_CONFIGURATION_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested codec config is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_MODE_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested rate control mode is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INTRA_REFRESH_MODE_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested intra refresh config is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_SUBREGION_LAYOUT_MODE_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested subregion layout mode is not supported\n");
      }
         
      if((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INPUT_FORMAT_NOT_SUPPORTED) != 0)
      {
         D3D12_LOG_DBG("[d3d12_video_encoder_h264] Requested input dxgi format is not supported\n");
      }

      D3D12_LOG_ERROR("[d3d12_video_encoder_h264] D3D12_FEATURE_VIDEO_ENCODER_SUPPORT arguments are not supported - ValidationFlags: 0x%x - SupportFlags: 0x%x\n",
            capEncoderSupportData.ValidationFlags,
            capEncoderSupportData.SupportFlags);
   }

   pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput = d3d12_video_encoder_calculate_max_slices_count_in_output(      
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
   &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264,
   pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxSubregionsNumber,
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
   pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize);
   if(pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput > pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxSubregionsNumber)
   {
      D3D12_LOG_ERROR("[d3d12_video_encoder_h264 Error] Desired number of subregions is not supported (higher than max reported slice number in query caps)\n.");
   }
}

D3D12_VIDEO_ENCODER_PROFILE_H264 d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile)
{
   switch (profile)
   {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN;
         
      } break;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH;
      } break;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10;
      } break;
      // No support for other profiles below
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
      default:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264 Error] d3d12_video_encoder_update_current_encoder_config_state_h264 - Unsupported profile\n");
         return static_cast<D3D12_VIDEO_ENCODER_PROFILE_H264>(0);
      } break;         
   }
}

D3D12_VIDEO_ENCODER_CODEC d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(enum pipe_video_profile profile)
{
   switch (u_reduce_video_profile(profile))
   {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_H264;
      } break;
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_HEVC;
      } break;
      case PIPE_VIDEO_FORMAT_MPEG12:
      case PIPE_VIDEO_FORMAT_MPEG4:
      case PIPE_VIDEO_FORMAT_VC1:
      case PIPE_VIDEO_FORMAT_JPEG:
      case PIPE_VIDEO_FORMAT_VP9:
      case PIPE_VIDEO_FORMAT_UNKNOWN:
      default:
      {
         D3D12_LOG_ERROR("[d3d12_video_encoder_h264 Error] d3d12_video_encoder_convert_codec_to_d3d12_enc_codec - Unsupported codec\n");
         return static_cast<D3D12_VIDEO_ENCODER_CODEC>(0);
      } break;
   }
}

UINT d3d12_video_encoder_build_codec_headers_h264(struct d3d12_video_encoder* pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams = d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   auto profDesc = d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   auto levelDesc = d3d12_video_encoder_get_current_level_desc(pD3D12Enc);
   auto codecConfigDesc = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);
   auto MaxDPBCapacity = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);

   size_t writtenSPSBytesCount = 0;
   bool writeNewSPS = (pD3D12Enc->m_fenceValue == 1); // on first frame
   writeNewSPS |= ((pD3D12Enc->m_currentEncodeConfig.m_seqFlags & D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RESOLUTION_CHANGE) != 0); // also on resolution change   

   UINT active_seq_parameter_set_id = pD3D12Enc->m_upH264BitstreamBuilder->GetSPSCount();
   if(writeNewSPS)
   {
      pD3D12Enc->m_upH264BitstreamBuilder->BuildSPS(*profDesc.pH264Profile,
         *levelDesc.pH264LevelSetting,
         pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
         *codecConfigDesc.pH264Config,
         pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures,
         active_seq_parameter_set_id,
         MaxDPBCapacity, // max_num_ref_frames
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
         pD3D12Enc->m_BitstreamHeadersBuffer,
         pD3D12Enc->m_BitstreamHeadersBuffer.begin(),
         writtenSPSBytesCount);
   }

   size_t writtenPPSBytesCount = 0;
   UINT pic_parameter_set_id = pD3D12Enc->m_upH264BitstreamBuilder->GetPPSCount();
   pD3D12Enc->m_upH264BitstreamBuilder->BuildPPS(*profDesc.pH264Profile,
         *codecConfigDesc.pH264Config,
         *currentPicParams.pH264PicData,
         pic_parameter_set_id,
         active_seq_parameter_set_id,
         pD3D12Enc->m_BitstreamHeadersBuffer,
         pD3D12Enc->m_BitstreamHeadersBuffer.begin() + writtenSPSBytesCount,
         writtenPPSBytesCount);

   assert(pD3D12Enc->m_BitstreamHeadersBuffer.size() == (writtenPPSBytesCount + writtenSPSBytesCount));
   return pD3D12Enc->m_BitstreamHeadersBuffer.size();
}